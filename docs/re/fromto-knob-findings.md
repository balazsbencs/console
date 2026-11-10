# Native FromToKnob relation

Recovered `cRelationSpecHandler::objectChanged(int,int)` opcode `'F'` (0x46)
from RC-20 1.0.5 x86_64. Retained REA pseudocode:
`analysis/re/relation-change.json`, evidence
`ev_2264fcc5e0de6b5be48dd1a2e3722e322008f89988a36f337d31eb583c94db5e`.
The decompiler drops floating-point calculations across model virtual calls;
raw instructions at **0xf116c–0xf1209** establish the arithmetic. The function
jump table at 0xf19b0 dispatches its first entry to 0xf116c.

Reference SHA-256:
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
The embedded Lua helper at file offset 0x63d70d declares:

```text
syncFromToKnob(from,to,toMin,toMid,toMax,minCurve,maxCurve)
```

It emits opcode F with those arguments. The native serialized record stores
anchors in a different order. For handler record `r = this+0x20+index*0x70`:

| Offset | Meaning |
| --- | --- |
| r+0x00 | Source model index |
| r+0x08 | Destination model index |
| r+0x0c | Low output anchor |
| r+0x10 | High output anchor |
| r+0x14 | Middle output anchor |
| r+0x18 | Lower-half curvature |
| r+0x1c | Upper-half curvature |
| r+0x60 | Opcode 0x46 |

The API added to `src/dsp/relations.h` uses the natural low/middle/high order:

```c
float rs_relation_from_to_knob(float input, float low, float middle, float high,
                               float lower_curve, float upper_curve);
```

For input below .5, select low→middle, the lower curvature, and `x=input`.
Otherwise select middle→high, the upper curvature, and `x=input+(-.5)`.
Then execute, preserving float rounding after each operation:

```text
x = x+x
numerator = x*k
denominator = (k+k+(-1))*x
denominator = denominator+1
denominator = denominator-k
y = numerator/denominator
output = (to-from)*y+from
```

The native opcode does **not clamp input**, unlike ordinary FromTo. It also
does not bypass the rational arithmetic when curvature equals .5. Callers
must supply finite controls and avoid rational poles outside the usual [0,1]
input domain. Zero/one curvature degeneracies are outside the tested contract.

## Magnetic mapping

Config.lua:388 sets depth steering noise gain using group Flux as input, low
`.14`, middle `.17`, high `.5`, and both curves `.5`. Native observations:

| Group Flux | Gain (float32) |
| --- | --- |
| 0 | 0.14000000059604645 |
| .25 | 0.1550000011920929 |
| .5 | 0.17000000178813934 |
| .75 | 0.3349999785423279 |
| 1 | 0.5 |

Thus the low→middle half changes gain slowly; the upper half changes it more
strongly. These are output anchors, not an input interval .14→.17.

## Native comparison

`tools/compare_native_fromto_knob.py` constructs one synthetic F record and
calls the original handler through fake model get/set callbacks. It compares
**361,146 outputs** by exact float bit pattern. Coverage includes seven anchor
triples (Magnetic, bipolar, reversed, flat and nonmonotonic), all 49 pairs of
curves selected from .001/.01/.15/.5/.85/.99/.999, a dense 0..1 grid and adjacent
float values around .5 and 1, linear extrapolation cases, and 16,384 seeded
random configurations. Both GCC Release and Clang 19 produce zero error and
identical output patterns. Result digest:
`e4a89b9022301fde1cdc9e0db57ec7a1549de29f2d3820d0846c69abdc5dbe9b`.

```sh
python3 tools/compare_native_fromto_knob.py \
  --library build-release/libretro_dsp.so \
  --report analysis/re/native-fromto-knob-comparison.json
```

The second report is `analysis/re/native-fromto-knob-clang-comparison.json`.
These isolated native tests require Linux x86_64 and the retained reference;
they do not execute original scripts, plugin initialization or production
notification scheduling. Only existing retained REA evidence was reused; no
additional REA session was opened.
