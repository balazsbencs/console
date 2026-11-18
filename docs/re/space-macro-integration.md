# Space macro and Flux integration

This resource audit specifies the recovered graph and the study adapter's
integration choices separately. It uses Config.lua, FX_AudioBoot.lua and
FX_GUIBoot.lua under `analysis/unpacked/guispec/`, retained relation/noise
recovery, and the physical bindings in `space-control-findings.md`. No complete
original plugin or constructor was run. Reference image SHA-256:
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.

## Recovered graph and arithmetic order

Let `M` be MasterMacroScaler (Magnitude), `A` be raw MacroSpaceAmount and `F`
be Space_Flux. Config.lua:359 declares Space_Level's unscaled value as one
and macroType Space. Generic wiring at :538–547 and :568–584 yields:

```text
amount_scaled = M * A
Space_Level = 1 * amount_scaled
```

FX_AudioBoot.lua:22 copies M into TotalFlux. Its :23–28 group relation is
`syncMulTriple(TotalFlux,Space_Flux,MacroSpaceAmount,Space_Flux_Scaled,
.5,.5,.99)`. Native opcode X, already recovered in `relations.c`, evaluates:

```text
ab = M * F
num = A * .99f
den = (.99f + .99f + -1.0f) * A
den = den + 1.0f
den = den - .99f
group_flux = ab * (num / den)
Space_PreWobbleNoiseAmount = group_flux
```

The last copy is explicitly declared at Config.lua:364 and emitted by
FX_AudioBoot.lua:106–109. The .5 curves on the first two factors take native
identity branches; the third factor curves **raw Amount**. It is not a curve
of `M*A`, and multiplication should retain `(M*F)*curve(A)` order. For example,
M=.5/F=1/A=.5 gives group Flux approximately .495. Replacing only the third
factor with curve(M*A) instead would give approximately .4853. The native
float expression, rather than these rounded illustrative numbers, defines
the port.

Space_Level has no stochastic generator. Its macro target remains M*A,
independently of Flux. No gain-in-dB mapping or opposite compensation gain is
present in this Space graph.

## Free-running stochastic path

Config.lua:365–375 declares Space_PreWobbleNoise with baseline zero and:

| Generator control | Value |
| --- | --- |
| macroGroup | FreeRunning |
| Chaos | .5 |
| Decay | .59 |
| Smooth | .42 |
| Flux / generator gain | .5 |
| ModStyle | .75 |
| bipolarNoise | omitted, therefore false |

FreeRunning has `disableFluxScaling=true` at Config.lua:181. Consequently
FX_AudioBoot.lua:51–63 takes the fixed local-gain branch, setting F_Gain to
the parameter's own Flux=.5. Neither group Flux nor M*A scales this generator
gain. Baseline ModulateIn is created from the parameter's default zero at
FX_AudioBoot.lua:38–40. The ModStyle=.75 path adds and clamps to [0,1]. With
the recovered shared original noise Drive output multiplier 16:

```text
Space_PreWobbleNoise = clamp(0 + 16 * noise_output, 0, 1)
```

The generator can keep evolving at Amount=0, Flux=0 or Magnitude=0; those
settings suppress the separately supplied noise amount. The graph contains no
conditional generator gate for Space_Enable. Original noise-generator history,
random recurrence and interpolation belong to the retained noise primitive;
a new noise algorithm or a host random source would not be equivalent.

## Resource defaults versus direct study controls

Config.lua:358–380 fixes StereoDetune=.3918 and DualMono=1. It also supplies
Decay=.8, StereoMode=0, feedbacks=1, PreDelay=0 ms, log Dampen=100 Hz and
Soften=16000 Hz. Space's front-panel macro defaults are Enable=0, Amount=0
and Flux=0 (:176); Magnitude defaults to one (:521–522). FX_GUIBoot.lua:785
sets addDSPMetaData=false, so these settled resource defaults intentionally
differ from several physical constructor defaults.

The study adapter's Macro mode overrides only Level, PreWobbleNoise and
PreWobbleNoiseAmount. It retains the direct values of every other kernel
control, including Enable, StereoDetune, DualMono, Decay, both filter
frequencies, PreDelay, feedbacks and study Allpass/ReverbMode. Entering Macro
mode therefore does not silently load .3918/1 stereo values or resource
frequency defaults. A caller wanting that particular resource setup must set
those direct controls explicitly. This is a partial reconstruction of the
Space control graph, with a documented direct-control surface; it does not
claim to reproduce the complete original initialization or front panel.

## Adapter timing and persistence contract

Space 0.2 appends these controls to the fifteen existing direct parameters:

| Index / ID | Control | Range | Default |
| --- | --- | --- | --- |
| 15 / 5015 | Control Mode | integer 0 Direct, 1 Macro | 0 |
| 16 / 5016 | Amount | 0..1 | 0 |
| 17 / 5017 | Magnitude | 0..1 | 1 |
| 18 / 5018 | Flux | 0..1 | 0 |
| 19 / 5019 | Flux Seed | integer 0..65535 | 0 |

The study macro clock advances at 64-frame boundaries in both modes and while
Enable=0. Bypass pauses the macro clock and audio engine together. Mode changes
preserve the current partial quantum and generator history. Direct mode ignores
the macro targets while its generator continues advancing, so entering Macro
mode uses that continuing history. At a boundary the stochastic primitive ticks
and its resulting targets are applied before rendering the next quantum.
Equal-time host events are grouped before targets are settled. A seed change
resets the generator when the grouped controls are applied, consistently with
the existing Magnetic study adapter. Full reset restores deterministic history
and clock; ordinary mode changes do not perform a full reset.

These scheduling, mode and seed-reset rules are explicit study integration
choices. FX_AudioBoot.lua:7 requests model/noise sync every audio batch, but that
resource declaration does not by itself establish arbitrary host callback sizes
as original control quanta. The isolated native comparison can validate graph
arithmetic and the chosen 64-frame composition; it cannot prove original
whole-plugin callback scheduling or the production Space seed index.

State magic remains RSSP. Version2 stores all twenty parameters. Version1
states with fifteen direct parameters load the five new defaults above,
including Direct mode, preserving the previous study's audible configuration.
Generator history and the partial audio clock are runtime histories rather
than fields in that parameter-only state format. Production seeds follow the
recovered serialized-record rule; seed0 is an explicit reproducible study
choice, not a claim that the original Space record index is zero.

## Native comparison

The comparison composes a synthetic native relation record, the original
NoiseGen and Drive methods, and the original ResoX audio method. GCC and
Clang each match 1,966,080 audio samples, 46,080 target values and 614,400
generator state bytes across 42 scenarios. The runs include zero Amount,
Magnitude and Flux, changing controls, seed resets, disable and re-enable,
and irregular host partitions. See [validation](space-macro-validation.json).

The fixtures use explicit seeds, a settled 64-frame schedule and Linux libm.
Native meter feedback is disabled during audio comparisons. In a separate
meter-enabled run, the original meter publication read Stereo Mode during a
zero-Level interval and consumed its pending update, so the native delay bank
kept its previous tuning when Level rose. The audio-only C port instead
retunes on resuming processing with the new Stereo Mode. This transition is
outside the bit-exact audio claim.
