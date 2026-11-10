# Classic Distortion metadata, state and execution

This read-only investigation covers the native cAESingleBandDistortion engine
and decoded Crunch/Heavy/Zap resource rows in RC-20 1.0.5 x86_64. Reference
SHA-256: `f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
Raw listings are retained under `analysis/re/classic-*-raw.asm`. The parent
investigation owns REA queries and the numerical port; no additional REA
session, constructor, global initializer or complete plugin was executed here.

## Parameters, object layout and factory defaults

The engine allocation in CreatePlug is **0x80 bytes** at 0xa3992. Its inlined
constructor runs 0xa399c–0xa3a1c; see `classic-factory-defaults-raw.asm`.
FindParam's static name pointers are at VM address 0x73cee0, range arrays at
0x5e5c60/0x5e5c90, and GetParamRange reads them at 0xa5c40. SetParam's switch
at 0xa5d80 corroborates the following bindings:

| Index | Native name | Offset | Range | Factory default |
| --- | --- | --- | --- | --- |
| 0 | amount | +0x1c | 0..100 | 0 |
| 1 | distDB | +0x20 | -100..100 | 0 |
| 2 | distLP | +0x70 | 20..20000 Hz | 20000 |
| 3 | highFreq | +0x28 | 20..20000 Hz | 20000 |
| 4 | lowFreq | +0x2c | 20..20000 Hz | 20 |
| 5 | mix | +0x30 | 0..100 | 100 |
| 6 | passDB | +0x24 | -100..100 | 0 |
| 7 | refLevel | +0x34 | -100..100 | 0 |
| 8 | shape | +0x0c | 0..3 | 0 |

Other factory fields: dirty byte +0x08=1, sample rate +0x10=44100,
batch size +0x14=0, stereo flag +0x18=0. The constructor copies four floats
from 0x5e52f0 into +0x1c..+0x28, then supplies lowFreq=20 and mix=100.
Derived coefficients/shape parameters and audio histories are initialized to
zero. The vtable pointer written into the object is 0x7688e0.

SetParam stores integer shape using cvttss2si, and marks the object dirty only
when the effective shape or scalar parameter changes. It does not itself clamp
the public metadata range. New C validation may enforce an explicit caller
contract, but that must be distinguished from observed native behavior.

Derived fields used by CalcInternals:

| Offset | Meaning |
| --- | --- |
| +0x38 | dry gain: 1 - mix*.01 |
| +0x3c | processed-band gain: expf(distDB*.05*ln(10))*mix*.01 |
| +0x40 | passed-band gain: expf(passDB*.05*ln(10))*mix*.01 |
| +0x44 | reference gain: expf(refLevel*(-.05)*ln(10)) |
| +0x48 | first split one-pole feed |
| +0x4c | second split one-pole feed |
| +0x50/+0x54 | left split histories |
| +0x58/+0x5c | right split histories |
| +0x60/+0x64/+0x68/+0x6c | shape-specific constants |
| +0x74 | processed-output low-pass feed |
| +0x78/+0x7c | output low-pass histories |

The first feed is `1-expf(min(lowFreq/SR,.49999)*(-2*pi))`. The second uses
`max(highFreq/SR,lowFreq/SR)` before the same .49999 limit. This preserves the
native effective ordering when low exceeds high. The output low-pass feed uses
distLP/SR with that same limit. Float constants are native approximations,
including .01=0.009999999776482582, ln(10)=2.3025851249694824,
.49999=0.49998998641967773 and -2*pi=-6.2831854820251465.

The shape switch jump table at 0xa6384 is **not in source-block address order**:
shape 0→0xa61ee, 1→0xa6304, 2→0xa624b, 3→0xa6291. Shape 0 derives its drive
from .5*expf((amount*.01)*ln(50)); shape 2 from
(expf((amount*.01)*ln(12))*.2)*reference_gain. Shape 1 uses an exponential
with amount*(-.013) and ln(100); shape 3 uses
(amount*(-.06)+1)*ln(2). The remaining compensation constants and exact native
rounding must follow the retained instructions, rather than treating these
four branches as interchangeable clipping curves.

## Reset, dispatch and sample cadence

ResetAudio at **0xa5d10** updates stored sample rate/batch size and marks dirty
if either changed, stores the stereo flag, and clears six history floats:
+0x50,+0x54,+0x58,+0x5c,+0x78,+0x7c. SoftResetAudio at **0xa5d50** clears
those same histories without changing metadata. Neither routine restores
parameter defaults or clears the shape constants. GetLatency at **0xa5d70**
returns literal zero.

Process at **0xa5e30** returns on count zero. A changed count or dirty flag
causes CalcInternals at **0xa6080** to run, then processing continues. A true
bypass argument clears all six histories and returns. A silence flag permits
a similar clear/skip when every history magnitude is at most 1e-6; otherwise
residual state still processes. Ordinary completed processing clears the
caller's silence flag.

Mono/stereo selection follows whether the left and right pointers are equal.
The shape 0/1/2/3 functions are:

| Shape | Mono | Stereo | RC-20 resource use |
| --- | --- | --- | --- |
| 0 | 0xa63a0 | 0xa7290 | Crunch |
| 1 | 0xa6760 | 0xa7960 | Heavy |
| 2 | 0xa6b30 | 0xa8020 | Zap |
| 3 | 0xa6f20 | 0xa8770 | no active DistTypeSpec entry |

The native stereo routines process groups of **four consecutive time samples**
with packed SSE, invoking SplitStereo4 at **0xa8fc0**. The input loads are
movaps, so synthetic probes must supply 16-byte-aligned channel buffers.
There is then a scalar remainder path for counts not divisible by four.
These are four adjacent samples, not four oversampled points. SplitMono4 is
at **0xa8de0**. Packed/scalar arithmetic order can affect rounding; a purely
scalar reimplementation requires direct comparison before claiming equality.
The scalar split updates include add/subtract-.0001 round-trip operations.

The immediate cPlugChainBlock passes 64-frame batches to scAEPlugChain, as
already recorded in distortion-oversampling-findings.md. The classic Process
wrapper itself forwards its received count unchanged into the chosen shape.
No sample-rate multiplier or interpolation/decimation layer appears in this
inspected wrapper; this does not establish a product-wide oversampling policy.

## Required global SIMD initialization for isolated probes

The raw loader does not execute C++ global initializers. In this binary the
sign-mask at **0x789cb0** is initially BSS zero, yet all four stereo shape
routines load it for absolute-value work. Forgetting this initialization changes
the waveform while leaving many paths apparently runnable.

The leaf initializer **_GLOBAL__sub_I_PlugChain.cpp at 0xb37e0–0xb3817** writes:

| Destination | Value |
| --- | --- |
| 0x789ca0 | one uint32 0xffffffff |
| 0x789c90 | four uint32 0xffffffff |
| 0x789cb0 | 16-byte copy of 0x5e1060: four uint32 0x80000000 sign bits |
| 0x789cc0 | 16-byte copy of 0x5e1070: float vector [0,.25,.5,.75] |

See `classic-global-init-raw.asm`. The second vector is a phase/ramp vector,
**not an absolute-value mask**. A synthetic numerical harness can write these
observed constants directly without invoking any constructor. The sign mask
uses andnps for absolute value: `~0x80000000 & sample_bits`.

## Crunch, Heavy and Zap resource configuration

Resource source: FX_GUIBoot.lua:381–574 `plugChainDist`; all physical engine
frequency controls map normalized FreqLo/FreqHi through 20..20000 Hz log.
Config.lua:291 defaults the overall Dist selector to **5 TubePair**, so none of
these classic shapes is the resource-selected initial type. Macro enable and
Amount both default zero; Mix defaults one. Dist_Amount's unscaled value is
one, then Magnitude×MacroDistAmount scales it.

| Internal Type / name | Local shape | Engine amount | refLevel | distDB | distLP |
| --- | --- | --- | --- | --- | --- |
| 0 Crunch | 0 | Amount, implicit native range 0..100 | -10 | (Amount) 0→-3 | (Amount) 20000→5000 |
| 1 Heavy | 1 | Amount 0→100 sym .15 | -15 | (Amount) 0→-3 sym .15 | (Amount) 20000→7000 sym .15 |
| 2 Zap | 2 | Amount, implicit native range 0..100 | Amount 0→-6 | (Amount) 0→-1 | (Amount) 10000→3000 |

All three have shared outer -12/+12 dB attenuation/boost plus Amount-mapped
-19→0 dB before the classic EQ/engine and +19→0 dB afterward. Crunch has
900 Hz/Q .4 peak pre-EQ +10→+12 dB and inverse post-EQ -10→-12 dB.
Heavy uses those same anchors with sym .15 mappings. Zap uses 850 Hz/Q .2
+10→+16 dB pre-EQ and -10→-16 dB post-EQ, both lin mappings. The two 850 Hz
EQ rows also match internal type 3, but no actual distortion engine row is
active for type 3 in this resource. Type 4 is likewise not exposed.

The engine `mix Mix` and `passDB -100` rows are commented out. Thus default
engine mix remains 100 and passDB remains 0, while user Mix is supplied through
container routing before/after the nonlinear chain. Parenthesized `(Amount)`
rows and `sym` conversion still require chain-parser evidence; do not assume
these are ordinary linear normalized mappings. See distortion-control-findings.md
for the full graph and Flux gain routing.

## Retained REA records

The parent investigation subsequently materialized these Ghidra records in
`analysis/re/`; they corroborate the raw instruction addresses above.

| Record | Evidence ID |
| --- | --- |
| classic-process.json | ev_41770ff79f2db0191b01003c11bd4baebb5bc6ca083a64d4ea730945ee27196d |
| classic-reset.json | ev_0dc97f1d1400ee7625c8ed6418495fd9a3689b8811fd8381dee1e6040b4eb028 |
| classic-set.json | ev_4b0fe17f30cca56b2fdf9f25e6a956735fedbe1e66f6328dbba34142561b1990 |
| classic-calc.json | ev_6ec6b40a111994f75ce1ee878414776c08e4947fccfe78dedb70d58993f1a38d |
| classic-stereo0.json | ev_13d4edb62593bd5d4b487efab4fdbe1dd8c76270b4ee5aeef5ed3a8f0695154f |
| classic-stereo1.json | ev_65df15f0be9e5292f50bc3692a0929bd3eac851f66c6144f07fe18a583b65fd9 |
| classic-stereo2.json | ev_e280cc14e957a1e1c553ac9c78fa167cfd78d24d9fc26d37c3d796d6342bbd16 |
| classic-split-stereo.json | ev_0f0e9214272c5012e798373ee8e9f8713f51e3835b8144aedecab49e54bcf6e4 |
