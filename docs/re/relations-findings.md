# Digital relation primitives

`src/dsp/relations.{h,c}` implements the native handler's triangle (`X`),
curved triple multiplication (`M`) and modulation (`b`) operations. Evidence:
`analysis/re/relation-change.{json,c}` (REA Evidence
`ev_2264fcc5e0de6b5be48dd1a2e3722e322008f89988a36f337d31eb583c94db5e`) plus raw objdump disassembly in
`analysis/re/relation-change-raw.asm`. Original function is
`cRelationSpecHandler::objectChanged(int,int)`, address 0xf0d70, in the
hash-pinned RC-20 1.0.5 x86_64 slice.

The raw text jump table at 0xf19b0 gives case entries X=0xf0ef8, M=0xf0e05,
b=0xf1348. `objectChanged` uses RDI=handler, ESI=changed parameter ID,
EDX=relation record index. The synthetic handler stores ModelStore* at +0x10
and record zero at +0x20, with 0x70-byte records. A record contains int source A
at +0, source B at +4, destination at +8, source C/modstyle at +0xc, three
float curve parameters at +0x10,+0x14,+0x18, and operation int at +0x60.
Triangle instead uses destination left/right IDs at +0xc,+0x10.

The ModelStore vtable callbacks are `float get(store*,int)` at +0x38 and
`void set(store*,int,float)` at +0x30. Comparison uses destination ID -1 and
captures it in the fake setter; this suppresses the handler's optional verbose
tracing after setting. The inspected function contains no dependency cascade
at its end. Real ModelStore setter notifications and dependency scheduling
remain outside this experiment.

## Formulas

For finite normalized inputs, triangle is:

```c
left  = x < 0.5f ? 1.0f : (1.0f - x) + (1.0f - x);
right = x > 0.5f ? 1.0f : x + x;
```

This confirms rate as the left balance output and bits as right, as wired by
`Config.lua:353`. Native triangle does not clamp out-of-range inputs.

The triple operation independently curves each input, then multiplies left to
right. Each curve is exactly bypassed when k=0.5:

```c
C(x,k) = x                       // k == 0.5f
C(x,k) = (x*k) / (((k+k-1)*x+1)-k) // otherwise
result = (C(a,ka)*C(b,kb))*C(c,kc);
```

Preserve the float arithmetic shown in the implementation and disable FP
contraction. With the Digital declaration's curve values `.5,.5,.99`, this is
`(Magnitude * Digital_Flux) * C(Digital_Amount,.99)`. It is a rational Amount
fade, not plain triple multiplication. No input/output clamp occurs in M.

Modulation branches use these inclusive upper thresholds:

| Style | Result |
| --- | --- |
| <= 0.25 | clamp01(input * (mod + 1)) |
| <= 0.5 | clamp01(input * (1 - mod)) |
| <= 0.6 | clamp01(input * mod) |
| <= 0.75 | clamp01(input + mod) |
| <= 0.85 | clamp(input + mod, -1, 1) |
| <= 0.9 | clamp(input - mod, input * 0.1, input) |
| <= 0.95 | clamp(clamp01(mod + 1) * input, 0, input) |
| otherwise | clamp01(input - mod) |

The final threshold is **0.95**, correcting decompiler pseudocode which printed
0.9 for the comparison with constant at 0x5e7254. Digital's style 0.75 therefore
adds the bipolar generator output to normalized input and clamps to [0,1].

## Verification

`tools/compare_native_relations.py` executes only the original isolated handler
against a synthetic fake ModelStore and compares C outputs bit for bit. It does
not execute plugin constructors, entry points or resource scripts. Validation
covers 1,009 triangle inputs, 4,256 curved triples (including the Digital curves)
and 16,400 modulation inputs across all thresholds and neighboring float
values. Seeded random inputs supplement explicit boundaries. Maximum error is
0; all outputs are bit-exact. Results:
`analysis/re/native-relations-comparison.json`.

Reproduce from the repository root:

```sh
cmake --build build-release -j2
python3 tools/compare_native_relations.py --library build-release/libretro_dsp.so
```

The port requires no libm. These stateless functions can be used directly by a
normalized Digital control adapter. Verification does not establish graph
scheduling, update cadence, caller seeds, nonfinite input semantics, singular
curves at endpoints k=0/1, or full-plugin Flux audio equivalence.
