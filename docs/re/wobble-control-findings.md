# Wobble control and initialization evidence

Scope: read-only extracted-resource inspection, direct symbol/constant reads and
raw disassembly of the hash-pinned RC-20 1.0.5 x86_64 reference. No new REA
session, original resource-script execution or complete plugin execution.

## Controls and physical mappings

Sources below are in `analysis/unpacked/guispec/`.

| Control | Normalized default | Physical metadata / meaning | Source |
| --- | --- | --- | --- |
| MacroWobbleEnable | 0 | copied to Wobble_Enable | Config.lua:173, :651–660 |
| MacroWobbleAmount | 0 | percent, scaled by Magnitude | Config.lua:173, :546–547 |
| Wobble_Flux | 0 | percent | Config.lua:173, :530 |
| Wobble_Xfade | 0.5 | wow left, flutter right triangle | Config.lua:281 |
| Wobble_Rate | 0.2 | Hz anchors 0.1 / 1 / 4 | Config.lua:282 |
| Wobble_Rate2 | 0.294 | Hz anchors 6 / 10 / 20 | Config.lua:283 |
| Wobble_Stereo_Enable | 0 | 0 mono, 1 stereo | Config.lua:82, :285 |
| Wobble_StereoPhase | 0 | stereo toggle maps 0→0, 1→0.4 | Config.lua:284 |
| Wobble_Mix | 1 | percent | Config.lua:286 |
| Wobble_Depth | unscaled creation 1 | physical anchors 0 / 0.4 / 0.8 | Config.lua:268 |
| Wobble_Depth2 | unscaled creation 1 | physical anchors 0 / 0.1 / 0.2 | Config.lua:269–270 |

The host declarations are Config.lua:117–123, with Amount declared earlier at
:103. GUI bindings to rates, mix, stereo and balance are GUI.lua:49–53. Its
display helper confirms wow=min(1,2−2x), flutter=min(1,2x)
(FX_GUIBoot.lua:227–234); the native triangle is already independently verified
and preserves `(1-x)+(1-x)` arithmetic rather than subtracting doubled x.

Use the existing recovered Scale_Fit3 metadata conversion, not a guessed
exponential. For anchors lo,mid,hi, compute `k=(mid-lo)/(hi-lo)` and rational
curve `C(x,k)=(x*k)/(((k+k-1)*x+1)-k)`, then `C*(hi-lo)+lo`, preserving float
rounding. Depth anchors imply k=0.5, hence linear physical depth conversions
0.8*x and 0.2*x. The rate defaults evaluated through the original isolated
Scale_Fit3 helper are **0.3720930218696594 Hz** wow and
**7.999029159545898 Hz** flutter. These are metadata conversion observations,
not full-plugin audio measurements.

Flux-zero normalized targets:

```text
amount_scaled = Magnitude * MacroWobbleAmount
wow_weight, flutter_weight = native_triangle(Wobble_Xfade)
Wobble_Depth = wow_weight * amount_scaled
Wobble_Depth2 = flutter_weight * amount_scaled
```

These are normalized model values before physical depth scaling. Mix and the
rate controls do not have macroType and are not reduced by Magnitude. Stereo
phase mapping is linear with native syncFromTo's clamp and interpolation.

## Flux routing

The shared group scaling is the previously native-verified curved triple:

```text
Wobble_Flux_Scaled = (Magnitude * Wobble_Flux) * C(MacroWobbleAmount, .99)
```

See FX_AudioBoot.lua:22–28. There are two separate bipolar generators:

| Target | Chaos | Decay | Smooth | Local Flux gain | ModStyle |
| --- | --- | --- | --- | --- | --- |
| Wobble_LfoNoise | 0.43783780 | 0.9054 | 0.90 | 0.18918 | 0.85 |
| Wobble_Depth2 | 0.43783 | 0.986486 | 0.8324 | 0.2 | 0.95 |

Declarations: Config.lua:255–265 and :269–279. Generic generator wiring is
FX_AudioBoot.lua:39–82. Gain is group Flux_Scaled times the local gain above;
Drive's shared output multiplier 16 is applied before modulation.

Wobble_LfoNoise has no macroType. Its baseline ModulateIn is copied from the
unscaled default 0, and style 0.85 gives `clamp(noise,-1,1)`. A **separate**
Wobble_LfoNoiseScaler equals `Wobble_Flux_Scaled * Wobble_Depth_Unscaled`
(Config.lua:255). Thus the wow perturbation has a generator gain depending on
Flux plus this additional Flux/balance scaler. Its use inside native process
must be preserved; it is not simply modulation of wow depth.

Wobble_Depth2 has macroType Wobble, so its baseline is the scaled flutter
triangle weight. Style 0.95 means
`clamp(clamp01(noise+1)*baseline,0,baseline)` under the verified native handler.
It only reduces the nonnegative flutter baseline. This differs materially from
Digital's additive style 0.75.

The complete serialized relation indices that seed either Wobble generator are
unknown; table `pairs()` order prevents deriving them from declaration order.
See flux-seed-findings.md. Resetting oscillator phases does not reset these
external generator states automatically in the inspected oscillator routine.

## Native initialization and reset boundaries

The original symbol table resolves:

| Address | Function |
| --- | --- |
| 0x34f2d0 | cWobblerBlock constructor |
| 0x3505b0 | prepare(float) |
| 0x350be0 | resetSmoothing() |
| 0x350c20 | processAudio(...) |
| 0x3523c0 | objectChanged(int,int) |
| 0x3525c0 | resetLfoPhase() |

Raw disassembly is retained in `analysis/re/wobble-{constructor,prepare,reset}-raw.asm`.
The reset file includes some subsequent destructor code; that code is not part
of resetLfoPhase, which returns at 0x352719.

Prepare stores sample rate at +0xf8 and reciprocal at +0x4b0
(0x3505c1–0x3505de). At 0x3505fc it computes `sample_rate * 0.02f`, stores this
at +0x4b4, then at 0x350620–0x350650 computes requested delay capacity
`2*trunc(sample_rate*.02f)+1` and rounds up to a power of two. The ring mask is
stored at +0x4e8; buffers at +0x4e0 and +0x4f0 are separately allocated and
zero-filled. Capacity is 2048 floats per channel at 44.1/48 kHz, 4096 at 96 kHz.
This capacity is an allocation fact, not a claim about reported audio latency.

Prepare and resetLfoPhase both zero the 16 bytes at +0x580..+0x58f and initialize
four oscillator phase values at +0x510,+0x530,+0x550,+0x570 to **0.75f**.
resetLfoPhase then obtains the parameter at +0x3c0 through helper 0x931a0; when
it is nonzero, it adds that value to phase +0x530 and wraps into [0,1) by repeated
subtract/add-one operations (0x35260c–0x352709). This is consistent with the
stereo-phase declaration, but constructor parameter binding should corroborate
that field identity before treating it as a typed offset. The other three
phases remain 0.75. No RNG seed operation or delay-buffer clearing appears in
resetLfoPhase. Constructor initializes both delay pointers/masks to zero and
sets four oscillator function pointers to 0x135330.

resetSmoothing returns immediately when flag +0x4bc is false; otherwise it sets
six dirty bytes at +0x5b4,+0x5c8,+0x5dc,+0x5f0,+0x604,+0x618
(0x350be4–0x350c17). It does not itself zero histories or delay buffers. Prepare
initializes six smoother coefficients using expf and sets six cutoff fields to
float bits 0x3c8637bd; the process port must recover the exact smoother behavior
and coefficient interpretation.

## Remaining questions

Process analysis must establish oscillator waveform/interpolation, delay read
index and phase operation order, mono/stereo channel handling, parameter
physical units, enable transition behavior, slew smoothers, modulation history,
and any fixed latency policy. Full reset semantics require tracing callers of
resetLfoPhase/resetSmoothing; neither is a complete buffer clear. Native
constructor/metadata bindings should confirm each parameter wrapper offset.
Flux scheduling and actual production seed indices remain independently open.
