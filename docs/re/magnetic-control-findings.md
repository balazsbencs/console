# Magnetic controls and initialization evidence

This is a read-only inspection of retained decoded resources, the original
symbol table, embedded helper declarations and raw x86_64 disassembly. No new
REA session, original scripts, plugin constructor or complete plugin was run.
Reference: RC-20 1.0.5 thin image SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
All Lua line numbers below refer to `analysis/unpacked/guispec/`.

## Front panel and ordinary macro controls

| Control | Normalized default | Mapping / evidence |
| --- | --- | --- |
| MacroMagneticEnable | 0 | Config.lua:178; copied to Magnetic_Enable by generic enable wiring :651–665 |
| MacroMagneticAmount | 0 | Config.lua:107, :178; Magnitude scales it through MasterMacroScaler, :546–547 |
| Magnetic_Xfade | .5 | Config.lua:386; triangle outputs WearDepth_Unscaled then Depth_Unscaled |
| Magnetic_WearPanEnable | 0 | Config.lua:433; MonoStereoSpec options, exposed :151 |
| Magnetic_Rate | .5 | Config.lua:446; normalized Fit3 anchors 6 / 20 / 30 Hz |
| Magnetic_DropOutsAmount_GUI | 0 | Config.lua:407; normalized percent, exposed :153 |
| Magnetic_Flux | 0 | Config.lua:178, :530; exposed :154 |

GUI.lua:125–128 binds the stereo button, rate knob, dropout knob and balance
slider directly to those controls. GUI.lua:177 binds the large Amount knob.
FX_GUIBoot.lua:247–254 labels the balance ends **Wear** and **Flutter**, in that
order. It is not a wow/flutter balance.

Write `M = MasterMacroScaler`, `A = MacroMagneticAmount`, `F = Magnetic_Flux`,
and `B = Magnetic_Xfade`. The settled ordinary macro graph is:

```text
amount_scaled = M * A
wear_weight, flutter_weight = native_triangle(B)
Magnetic_WearDepth = wear_weight * amount_scaled
Magnetic_Depth = flutter_weight * amount_scaled
```

The triangle preserves the previously recovered float arithmetic:
left is one below .5 and `(1-B)+(1-B)` otherwise; right is `B+B` below .5 and
one otherwise. Both weights equal one at the center.

The rate does not have macroType and is not scaled by Magnitude. Its .5
default maps to the **20 Hz** middle anchor through native Scale_Fit3.
`Magnetic_Enable` has a local construction declaration of one at Config.lua:385,
but the settled front-panel macro enable defaults to zero; these must not be
confused.

## Group Flux and four free-running generators

FX_AudioBoot.lua:22–28 sets TotalFlux from MasterMacroScaler, then derives:

```text
group_flux = (M * F) * C(A, .99)
C(x,k) = (x*k) / (((k+k-1)*x+1)-k)
```

All four Magnetic stochastic parameters declare `macroGroup="FreeRunning"`
and **do not have macroType**. FreeRunning has `disableFluxScaling=true`
(Config.lua:181). Consequently FX_AudioBoot.lua:51–63 takes its unscaled branch
and initializes each generator gain from its own local Flux value. It does not
multiply those gains by group_flux. Some coefficient controls are independently
mapped from group_flux, as listed below.

| Parameter | Baseline ModulateIn | Chaos | Decay | Smooth | Local gain | Bipolar | ModStyle |
| --- | --- | --- | --- | --- | --- | --- | --- |
| DepthSteerSig | .5 | 0 | .5 | mapped .83→.5 | mapped knob anchors .14/.17/.5 | true | .85 |
| DropOutSteerSig | 0 | .424324 | .656756758 | mapped .3→.05, curve .15 | .87837785 | false | .75 |
| WearGain | 0 | 0 | .5 | mapped .91→.4 | .44 | true | .85 |
| WearPan | 0 | 0 | .5 | .837 | .40 | true | .85 |

Sources: Config.lua:388–400, :410–420, :421–432, :434–444. Baselines originate
from each parameter's defaultValue and the creation of ModulateIn at
FX_AudioBoot.lua:39–40. The FreeRunning branch does not overwrite those
baselines with macro-scaled values. DropOutSteerSig omits bipolarNoise, so
`syncNoiseGen` receives false/nil; the other three explicitly request true.

After original Drive's shared output multiplier 16, the existing recovered
modulation styles yield:

```text
DepthSteerSig   = clamp(.5 + depth_noise, -1, 1)
WearGain        = clamp(wear_noise, -1, 1)
WearPan         = clamp(pan_noise, -1, 1)
DropOutSteerSig = clamp(dropout_noise, 0, 1)
```

In particular, Magnetic Flux zero does **not** stop these noise generators.
The local gains remain nonzero. Flux changes the depth steering gain/smoothing
and wear gain smoothing; macro Amount separately controls effect depths.
Actual use of steering and wear values belongs to the kernel analysis.

DepthSteerSig_F_Gain declares `audioSyncFromToKnob` with group_flux and anchors
`.14, .17, .5`, with lower/upper curves `.5, .5` (Config.lua:388). Embedded
helper text at image offset `0x63d70d` confirms argument order
`syncFromToKnob(from,to,toMin,toMid,toMax,minCurve,maxCurve)` and native relation
opcode `'F'`. These are knob low/middle/high output anchors, not input range
`.14..17`. A direct native test of this previously unused `'F'` opcode should
establish its exact interpolation and float operation order before porting it.
The two other group mappings are ordinary `syncFromTo` with curve .5:
DepthSteerSig smooth .83→.5 and WearGain smooth .91→.4.

## Dropout graph

Config.lua:403–410 declares a separate path for the dropout knob `D_gui`:

```text
amount_mult1 = FromTo(amount_scaled, 0,1, 0,1, .98)
amount_mult2 = amount_mult1 * M
D = D_gui * amount_mult2
Magnetic_DropOutsAmount = D
Magnetic_DropOutsMin = FromTo(D, 0,1, .1,.68, .5)
Magnetic_DropOutsMax = FromTo(D, 0,1, .25,.88, .5)
DropOutSteerSig_F_Smooth = FromTo(D, 0,1, .3,.05, .15)
```

`FromTo` clamps the input fraction to [0,1], applies the rational curve C,
then maps to the destination interval with the recovered float operation
order. The .98 curve is already close to full scale at moderate Amount: in
real arithmetic C(.5,.98)=.98. It fades toward zero near zero Amount; it is
not the same as linearly multiplying dropouts by the Amount knob. Magnitude
participates both inside amount_scaled and as the explicit second multiplier.
Neither balance weight appears in this graph.

Dropout noise gain and decay do not change with D or group Flux. D changes its
smoothing, minimum and maximum thresholds, and the kernel's dropout Amount
input. The resulting relationship between those inputs and output gain still
requires process analysis.

## Native construction, prepare and reset

Raw disassembly is saved locally as
`analysis/re/magnetic-{constructor,prepare,reset}-raw.asm`. Native symbol names:

| Address | Symbol |
| --- | --- |
| 0x352d40 | Block::cFlutterBlock constructor |
| 0x353a60 | prepare(float) |
| 0x353c00 | resetSmoothing() |
| 0x353c30 | processAudio(...) |
| 0x354bd0 | objectChanged(int,int) |

The constructor wrapper bindings (name strings at 0x650a52..0x650d0f and
call sites 0x353379..0x35351a) corroborate these object-relative addresses:

| Offset | Parameter |
| --- | --- |
| +0x108 | Enable |
| +0x140 | Depth |
| +0x178 | DepthSteerSig |
| +0x1b0 | Rate (metadata wrapper) |
| +0x250 | StereoPhase |
| +0x288 | WearGain |
| +0x2c0 | WearPan |
| +0x2f8 | WearPanEnable |
| +0x330 | WearDepth |
| +0x368 | DropOutSteerSig |
| +0x3a0 | DropOutsAmount |
| +0x3d8 | DropOutsMin |
| +0x410 | DropOutsMax |

Some constructor wrapper defaults differ from the settled Lua graph; cached
bindings should therefore be initialized from declared settled controls rather
than assumed to represent constructor audio behavior.

Prepare stores sample rate at +0xf8 and reciprocal at +0x458, and writes that
reciprocal to oscillator rate fields +0x4ec and +0x50c. Both oscillator function
pointers (+0x4d0/+0x4f0) are set to the polynomial oscillator at 0x135330. Both
phase fields (+0x4e0/+0x500) are initialized to **.25**, unlike Wobble's .75.
At +0x45c it initializes one to 1. It invalidates wrapper cache bytes +0x244,
+0x16c,+0x2b4,+0x2ec,+0x27c.

Three smoother structures begin at +0x460,+0x474,+0x488. Prepare initializes
all cutoff fields to float bits 0x3c8637bd, and calculates a common feed
coefficient using the following actual binary constants and operation order:

```text
cutoff = ((sample_rate * float32(.00005)) * .015625) * 16384
feed = 1 - expf((float32(-pi) * cutoff) / float32(.001))
```

These are stored coefficient fields; do not infer units or intended time
constants from the apparent formula without following smoother use in process.
The source constants are at 0x5f5838,0x5e1228,0x5ea9b0,0x5f0dcc,0x5e21e4.
The prepare routine also derives two visualization counts at +0x5d8/+0x688 by
truncating corresponding stored interval × sample rate × float32(.001).
It does not allocate audio delay buffers in this routine.

`resetSmoothing` returns immediately if byte +0x49c is zero. Otherwise it sets
only the three dirty flags +0x470,+0x498,+0x484 to one (0x353c04–0x353c22).
It does not reset either oscillator phase or external noise generators, and
must not be treated as a full history clear.

## Remaining fidelity questions

The parent kernel investigation must establish oscillator increment/use,
stereo-phase handling, wear gain and pan curves, dropout threshold/saturation
arithmetic, smoothing operation order, and enabled transition behavior. The
exact `'F'` FromToKnob relation still needs native validation before a Magnetic
macro port. Production seeds are initialized from serialized relation record
indices under the already recovered rule, but actual Magnetic record indices
have not been captured. Original startup notification order and complete
scheduler behavior remain outside these settled-graph observations.

## Kernel comparison follow-up

`tools/compare_native_magnetic.py` now compares the C port against
`native_magnetic_probe.py`. Both GCC Release and Clang 19 builds passed **76
cases and 6,619,136 audio samples** per build with **zero error and zero unequal
samples**. Local reports are `analysis/re/native-magnetic-comparison.json` and
`analysis/re/native-magnetic-clang-comparison.json`.

Coverage comprises 23 static settings at 44.1/48/96 kHz, six 2,048-batch dynamic
cases with all parameter families, original smoothing reset at batch 300,
study full reset at batch 1,024, and a 4,096-batch run. Three dynamic cases split
each C batch into 1/7/13/3/29/11 frames while the reference executes its native
64-frame batch. Stereo is toggled repeatedly before and after full reset.

The comparison caught a persistent state detail: native mono wear mode updates
left gain at +0x4b0 but leaves right gain +0x4b4 untouched. The latter is still
clamped into the target at 0x35427e–0x354296 and stored into previous right gain
+0x4ac at 0x354a47–0x354a55. Thus the first transition from mono to stereo
interpolates from the untouched initial right endpoint zero, rather than from
the preceding mono gain. The C port now preserves that cache. Full study reset
clears it, matching a freshly reconstructed reference instance.

The native probe uses original `resetSmoothing` only for partial reset. C full
reset is compared with fresh native construction/prepare state, not presented
as behavior of the original partial reset. These tests establish the direct
kernel scope; Magnetic macro/Flux graph execution and production scheduling
remain outside the comparison.

## Macro integration follow-up

The FromToKnob relation is now native-verified in
[fromto-knob-findings.md](fromto-knob-findings.md). The settled four-generator
macro graph and audio composition are implemented and compared in
[magnetic-macro-integration.md](magnetic-macro-integration.md), superseding the
pending macro-port items above. Original production seed indices, initialization
ordering and full notification scheduling remain unresolved.
