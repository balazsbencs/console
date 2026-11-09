# Digital macro and Flux resource evidence

This resource-only ledger predates the native primitive checks. The triangle,
Fit3/log curves, curved triple product, modulation styles and NoiseGen are now
implemented and native-verified. See [STATUS.md](STATUS.md),
[relations-findings.md](relations-findings.md) and
[noisegen-findings.md](noisegen-findings.md). Remaining integration gaps are
production seed indices, boot/reset sequencing and full model propagation
timing. Digital 0.3.0 integrates Flux with explicit study seeds at the recovered
64-frame cadence; see [flux-integration.md](flux-integration.md). The unresolved
statements below describe the original resource-only investigation boundary.

Scope: read-only inspection of the extracted RC-20 resource Lua. No scripts or
plugin code were executed for this note. Paths and line numbers below refer to
`analysis/unpacked/guispec/`. This is resource evidence, not a new REA native
Evidence record or an observation of full-plugin execution.

## Observed controls and defaults

| Control | Default | Source |
| --- | --- | --- |
| Magnitude (`MasterMacroScaler`) | 1 | `Config.lua:521–522` |
| Digital Enable / Amount / Flux | 0 / 0 / 0 | `Config.lua:175` |
| Balance (`Digital_Xfade`) | 0.5 | `Config.lua:353` |
| Smooth / Mix | 0 / 1 | `Config.lua:322–323` |
| Filter mode (`Digital_FocusMode`) | 0 = Focus; 1 = Cut | `Config.lua:86`, `Config.lua:324` |
| DSP Focus (`Digital_Focus`) | 1, inverted from filter mode | `Config.lua:325` |
| Filter low / high | normalized 0 / 1; log 20–20000 Hz | `Config.lua:354–355` |

The Amount, Magnitude, Smooth, Mix and Flux display metadata use 0–100 percent;
the actual stored/model values are normalized. Host declarations are Magnitude
(`Config.lua:100`), DIGI Amount (`Config.lua:105`) and the eight DIGI subsidiary
controls (`Config.lua:132–139`). Boolean controls have normalized automation
mirrors and enum quantization (`Config.lua:604–610`).

## Amount, balance and enable

`Config.lua:546–547` explicitly wires and documents:

```c
amount_scaled = magnitude * amount;
```

The balance triangle has rate as its first output and bits as its second output
(`Config.lua:353`, expanded into `syncTriangleCrossFade` in
`FX_AudioBoot.lua:95–98`). The GUI's independent display helper explicitly uses
these equations (`FX_GUIBoot.lua:237–244`):

```c
rate_weight = min(1, 2 - 2 * balance);
bits_weight = min(1, 2 * balance);
```

Thus endpoints mean rate only / bits only, and the center means full weight for
both. Multiplication of each unscaled macro target by `amount_scaled` is
explicitly documented in `Config.lua:583–584` and repeated for modulation input
in `FX_AudioBoot.lua:49–51`. The proposed Flux-zero mapping is therefore:

```c
sr_normalized   = amount_scaled * rate_weight;
bits_normalized = amount_scaled * bits_weight;
```

This last combination is a strongly supported reconstruction; the actual native
triangle primitive still needs confirmation. Neither target has an explicit
`defaultValue` (`Config.lua:332`, `Config.lua:343`); initial creation falls back
to zero (`Config.lua:571–573`), before sync propagates the triangle.

Enable copies directly from `MacroDigitalEnable` to `Digital_Enable` through
the generic macro loop (`Config.lua:651–660`). It does not multiply the amount
or Flux declarations. Native disable behavior (state freezing, reset or tails)
is not established here.

Physical metadata is bits `(min=24, fitMiddle=6, max=1)` and rate Hz
`(min=44100, fitMiddle=200, max=44.1)` (`Config.lua:332`, `Config.lua:343`). These
are decreasing nonlinear curves. Lua does not supply their exact conversion;
linear interpolation or a guessed exponential must not be represented as the
recovered mapping. Filters explicitly request log mapping.

## Flux wiring

The active setup enables both macro paths (`Config.lua:7–8`), with the audio
path selected when `AudioThreadSyncSpec` exists (`Config.lua:518`). Sync runs
every batch (`FX_AudioBoot.lua:7`). The global setup is:

```text
TotalFlux = MasterMacroScaler
Digital_Flux_Scaled = syncMulTriple(TotalFlux, Digital_Flux,
                                  MacroDigitalAmount; 0.5, 0.5, 0.99)
```

The exact call and comment about fading Flux near zero Amount are in
`FX_AudioBoot.lua:22–28`. The three numeric arguments are native curve controls;
their meaning is unresolved by the extracted Lua. In particular it is not
established that this primitive is a plain triple multiplication.

For each of BitDepth and DegradeSR, `FX_AudioBoot.lua:39–51` and `:64–82` wire:

```text
parameter_F_Gain = Digital_Flux_Scaled * parameter_Flux
parameter_ModulateIn = parameter_Unscaled * MacroDigitalAmountScaled
parameter = syncModulateAndClamp(parameter_ModulateIn,
                                 parameter_F_NoiseOut,
                                 parameter_F_ModStyle)
parameter_F_NoiseOut = syncNoiseGen(parameter_F_Gain, parameter_F_Chaos,
                                  parameter_F_Smooth, parameter_F_Decay,
                                  bipolarNoise=true, optional reset counter)
```

Both targets have independent generator declarations with Chaos=0, Decay=0,
ModStyle=0.75, Smooth=0.94847450187182, and bipolar noise. Their local depth
scales differ: bits Flux=0.3 (`Config.lua:332–341`), rate Flux=0.2
(`Config.lua:343–352`). The shared fallback fields are Gain=0.05,
NoiseOut=0 (`Config.lua:185–192`); Gain is overwritten by the product above.

Rate generator smoothing also has an explicit dynamic mapping from
`Digital_Flux_Scaled`, with endpoints 0.9484 and 0.85 and curve argument 0.5
(`Config.lua:342`, dispatched in `FX_AudioBoot.lua:85–88`). Its separately
declared default is 0.837, distinct from the generator initializer; the final
settled value depends on sync propagation. Do not silently choose one as a
runtime-observed default.

Other fixed DSP controls are LFO rate=0, LFO depth=0, AA2=0.5, AA3=0.65,
RoundError=0.01 and Feedback=0 (`Config.lua:326–331`). Flux here modulates the
two normalized parameters externally; it is separate from cBitColor's internal
rate/depth LFO controls.

## Proposed C integration and remaining gaps

Add a separate normalized front-panel adapter above the existing physical-value
DSP API. Keep Amount, Balance, Magnitude, Enable, Smooth, Mix, Mode and Filter
controls in that adapter, preserving the physical study parameters for existing
kernel comparisons. First implement Flux=0 using the recovered triangle and
native-confirmed metadata conversions. Preserve distinct normalized and physical
values so Flux can later operate before metadata conversion.

For Flux support, recover native `syncMulTriple`, `syncModulateAndClamp`,
`syncNoiseGen`, `syncFromTo` and their scheduling/dependency rules. No definitions
of these primitives were found in the extracted Lua tree; the scripts only
declare their calls. Source insertion order puts modulation before generation,
but `pairs()` iteration and native scheduling mean one cannot infer the actual
batch evaluation order or a one-batch noise delay from this alone. Generator
algorithm, seed/reset state, clamping style, update interval and smoothing curves
remain unknown. The packaged audio-test script itself documents that sync drive
must run for modulation changes to propagate (`FX_AudioTests.lua:74–75`,
`:89–99`), supporting the need to preserve scheduling and reset behavior.

Question ledger: Amount/Enable declarations answered; balance formula supported
by display code pending native corroboration; exact metadata curves unresolved;
Flux graph and constants answered; Flux arithmetic/noise/timing unresolved.
