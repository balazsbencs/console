# Native Knob2AnyGain relation

Recovered `cRelationSpecHandler::objectChanged(int,int)` opcode `'K'` (0x4b)
from retained RC-20 1.0.5 x86_64 REA evidence and raw disassembly. No new REA
session, original constructor, script or complete plugin was run.

Reference SHA-256:
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
`analysis/re/relation-change.json` retains evidence
`ev_2264fcc5e0de6b5be48dd1a2e3722e322008f89988a36f337d31eb583c94db5e`.
The raw function is `analysis/re/relation-change-raw.asm`; opcode K enters at
**0xf120e**, performs piecewise curve work through **0xf12b5**, and continues
through **0xf1753–0xf1788** for dB conversion and model publication.
The retained decompiler output loses several floating-point calculations at
virtual model calls, so these instructions establish the operation order.

The embedded Lua helper declares
`syncKnob2AnyGain(src,dst,minGain,maxGain,minCurve,maxCurve)`. Config.lua:295–296
uses it for the reciprocal Dist input/output gain controls. The new C API is:

```c
float rs_relation_knob_to_gain(float input, float low_db, float high_db,
                                float lower_curve, float upper_curve);
```

## Record layout and exact calculation

For record `r = handler+0x20+index*0x70`, the effective native fields are:

| Offset | Meaning |
| --- | --- |
| r+0x00 | Source model index |
| r+0x08 | Destination model index |
| r+0x0c | High endpoint dB |
| r+0x10 | Low endpoint dB |
| r+0x14 | Upper-half curvature |
| r+0x18 | Lower-half curvature |
| r+0x60 | Opcode 0x4b |

The layout differs from logical Lua argument order. This reordering is also
visible in the retained relation specification parser
`analysis/re/flux-relation-spec.c`, opcode K's final field stores. The parser
reads encoded fixed-point operands; this API and comparison operate on the
resulting float fields, not on a reimplementation of that string parser.

The knob has a fixed **0 dB middle point**, independent of endpoint dB values.
For `input < .5`, select `k = 1-lower_curve` and `x = input`; otherwise select
`k = upper_curve` and `x = input+(-.5)`. Execute each step at float precision:

```text
x = x+x
numerator = x*k
denominator = (k+k+(-1))*x
denominator = denominator+1
denominator = denominator-k
y = numerator/denominator

lower half: from=0, to=.5, gain_db=-low_db
upper half: from=.5, to=1, gain_db=high_db
z = (to-from)*y+from
gain_db = gain_db+gain_db
exponent = (z+(-.5))*gain_db
exponent = exponent*float32(.05)
output = powf(10,exponent)
```

The inversion of lower curvature is intentional. The opcode does not clamp
input and has no special .5-curve arithmetic bypass. Input outside [0,1]
extrapolates; nonlinear extrapolation can reach rational poles or overflow.
The port's tested contract uses finite controls and curves strictly between
zero and one. No fallback for NaN or singular curves is inferred.

The final conversion uses **single-precision powf**, imported at **0x578c9c**,
not a double power function or an expf/log replacement. Algebraic shortcuts
that remove the intermediate z/.5 operations can change rounding.

## Distortion's reciprocal pair

The real Dist mappings use low/high endpoints **-3/+20 dB** for the pre-gain,
and **+3/-20 dB** for the post-gain, with both curves .5. Native observations:

| Feed | Pre gain | Post gain |
| --- | --- | --- |
| 0 | 0.7079457640647888 | 1.4125375747680664 |
| .25 | 0.8413951396942139 | 1.1885021924972534 |
| .5 | 1 | 1 |
| .75 | 3.1622776985168457 | 0.3162277638912201 |
| 1 | 10 | 0.10000000149011612 |

The lower half spans -3→0 dB and the upper half 0→+20 dB. A single linear
-3→+20 dB mapping would incorrectly produce +8.5 dB at the center. The inverse
pair's float gains are close to reciprocals but their products are not always
exactly one; preserve both native conversions independently.

## Native comparison

`tools/compare_native_knob_gain.py` constructs an isolated K relation record
and invokes original objectChanged through synthetic model get/set callbacks.
No ported relation helper is used to calculate the expected result. The
original powf import and the C implementation both bind system libm.

GCC and Clang 19 each pass **361,137 comparisons**, with **zero error** and
**all float patterns equal**. Coverage includes seven dB endpoint pairs,
including both actual Dist mappings, zero and reversed pairs, ±100 dB
endpoints, all 49 combinations of seven curve values, a dense input grid and
adjacent float values near .5/1, linear extrapolation, and 16,384 seeded random
configurations. Result digest:
`a1394ded0b7e659aed31f0c64837d7222389aa0c938de34cddea21c4320602fb`.

```sh
python3 tools/compare_native_knob_gain.py \
  --library build-release/libretro_dsp.so \
  --report analysis/re/native-knob-gain-comparison.json
```

The companion report is
`analysis/re/native-knob-gain-clang-comparison.json`. Native execution requires
Linux x86_64 and the retained reference image. These results establish the
primitive and real Dist endpoint mappings, not production notification order,
the full Flux/mix graph, chain routing or complete-plugin audio equivalence.
