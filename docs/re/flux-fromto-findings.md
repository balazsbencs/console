# Digital Flux-to-smoothing relation

The embedded Lua helper begins at raw reference offset **0x63d569**:

```lua
syncFromTo = function(from,fromMin,fromMax,to,toMin,toMax,curve)
  -- object-existence checks omitted here
  return { 'f', from, tonumber(fromMin), tonumber(fromMax), to,
           tonumber(toMin or 0), tonumber(toMax or 1), tonumber(curve or 0.5) }
end
```

This is read as text from the supplied hash-pinned x86_64 Mach-O, not executed.
Its native operation is lowercase **`f` (0x66)**. In
`cRelationSpecHandler::objectChanged` (0xf0d70), the jump table at 0xf19b0
selects **0xf0f88** for that operation. Raw instructions are retained in
`analysis/re/relation-change-raw.asm`, and REA's decompilation record is
`analysis/re/relation-change.json`, Evidence
`ev_2264fcc5e0de6b5be48dd1a2e3722e322008f89988a36f337d31eb583c94db5e`.
The decompiler loses most floating-point arithmetic around callbacks; the
following layout and arithmetic come from instructions and isolated execution.

The handler stores records at `handler + 0x20 + record_index * 0x70`:

| Record offset | Value |
| --- | --- |
| +0x00 | source model ID, int |
| +0x08 | destination model ID, int |
| +0x0c | fromMin, float |
| +0x10 | fromMax, float |
| +0x14 | toMin, float |
| +0x18 | toMax, float |
| +0x1c | curve, float |
| +0x60 | operation 0x66, int |

Every operation below is a scalar float operation with a rounded result, in
this exact order (parentheses express operation order, not double arithmetic):

```c
z = (source - fromMin) / (fromMax - fromMin);
z = clamp01(z);
denominator = curve + curve;
denominator = denominator + -1.0f;
denominator = denominator * z;
numerator = z * curve;
denominator = denominator + 1.0f;
denominator = denominator - curve;
shaped = numerator / denominator;
result = toMax - toMin;
result = result * shaped;
result = result + toMin;
```

Normalization/clamping occupies 0xf0f96–0xf0fc7, rational curve arithmetic
0xf0fd6–0xf0fff and output interpolation 0xf1003–0xf1019. Unlike curved triple
multiplication, this operation has **no explicit curve=0.5 bypass**. For finite
normalized z and curve 0.5, the intermediate `(z*0.5f)/0.5f` equals z, so the
Digital declaration reduces to this float calculation:

```c
float z = clamp01(digital_flux_scaled);
float smooth = (0.85f - 0.9484f) * z;
smooth = smooth + 0.9484f;
```

The declaration is
`analysis/unpacked/guispec/Config.lua:342`; dispatch adds the destination model
name into that specification in `FX_AudioBoot.lua:85–88`. It controls
`Digital_DegradeSR_F_Smooth`. The separately declared creation default 0.837
is superseded when this relation evaluates. At Flux zero the mapped value is
0.9484f; use the expression above for other points to preserve float rounding.

`tools/compare_native_fromto.py` executes the original isolated handler against
synthetic ctypes ModelStore get/set callbacks, reusing the hash-pinned image
mapping. It now compares against the built C `rs_relation_from_to` implementation, covering
10,007 Digital mapping cases (including out-of-range source values) and 10,000
cases with non-unit source ranges, varying output endpoints and four interior
curve settings. All **20,007 results are bit-exact, maximum error 0**. Reproduce:

```sh
python3 tools/compare_native_fromto.py --library build-release/libretro_dsp.so
```

Results are retained in `analysis/re/native-fromto-comparison.json`. The C implementation is in `src/dsp/relations.c`; native raw assembly was
examined without opening an additional REA session. This establishes the
relation arithmetic and its consumed record layout; it does not execute the
specification serializer, prove real graph evaluation order, or validate
nonfinite inputs, zero source width or singular curves 0/1.
