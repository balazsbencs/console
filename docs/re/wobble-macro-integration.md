# Wobble macros and Flux, 0.2.0

Wobble now exposes Amount, Wow/Flutter Balance, Magnitude, Flux and Stereo
through a recovered normalized macro layer. Direct kernel mode remains the
default, preserving existing sessions. The helper is independent C11 code in
`src/dsp/wobble_macro.{h,c}` and uses the native-verified relation and NoiseGen
primitives retained from the earlier REA investigation.

## Signal path

Let `W,F` be the native triangle weights from Balance, `A=Magnitude*Amount`,
and `G=(Magnitude*Flux)*C(Amount,.99)` using the native rational curve. The
settled graph is:

- Wow depth = `W*A` before the kernel's physical .8 depth scale.
- Flutter baseline = `F*A` before the kernel's physical .2 depth scale.
- Wow generator: gain `G*.18918`, chaos .43783780, smooth .90, decay .9054.
- Flutter generator: gain `G*.2`, chaos .43783, smooth .8324, decay .986486.
- Both generators are bipolar, receive sample_rate/64 and publish value*16.
- Wow noise = style .85 modulation of baseline zero: clamp output to [-1,1].
- Wow noise scale = `G*W`, independent of the generator's own gain.
- Flutter depth = style .95 modulation of its baseline:
  `clamp(clamp01(noise+1)*baseline,0,baseline)`. Flux only reduces flutter depth.
- Stereo maps 0/1 to phase offset 0/.4 through the native FromTo operation.

Wow and flutter rate, Mix and Enable retain their direct values. Magnitude does
not scale these controls. Source resource declarations and line references are
in [wobble-control-findings.md](wobble-control-findings.md).

## C API and CLAP contract

Zero-initialize `rs_wobble_macro`, reset it with two explicit signed seeds, tick
it once per 64 audio frames, then apply its targets to `retro_wobble_params`.
The apply function preserves rate, mix and enable. Controls must be finite
[0,1], with a positive sample rate; this low-level helper assumes its input
contract. The CLAP validates all exposed controls.

IDs 2000–2009 and their defaults remain unchanged. Added controls are:

| ID | Control | Default |
| --- | --- | --- |
| 2010 | Control Mode: 0 direct, 1 macro | 0 |
| 2011 | Amount | 0 |
| 2012 | Wow / Flutter Balance | .5 |
| 2013 | Magnitude | 1 |
| 2014 | Flux | 0 |
| 2015 | Stereo: 0 mono, 1 stereo phase | 0 |
| 2016 | Wow Flux Seed | 0 |
| 2017 | Flutter Flux Seed | 1 |

Balance 0 selects wow, 1 selects flutter, and .5 gives both full weight.
Macro mode replaces the two direct depths, Stereo Phase, LFO Noise and Noise
Scale with mapped values. Direct rates, Mix and Enable work in both modes.
Seeds are integers 0–65535 in the CLAP; defaults 0/1 are explicit study choices.

The adapter preserves its Flux clock across arbitrary host buffers. Enable
keeps that clock running while the kernel performs its disable fade and dry
return. Bypass freezes both kernel and Flux; direct mode pauses Flux. Changing
either seed restarts both generators and their clock. Activation and full reset
restart the study trajectory. These lifecycle and mode-switch choices have not
been matched to the complete original model's startup/reset sequence.

RSWB state version 2 saves all 18 controls. Version 1 states load their original
ten controls and default the new eight. State stores controls/seeds, not ongoing
delay, smoother or RNG history. A reset restarts the saved trajectory.

## Validation and fidelity boundary

The [native composite comparison](wobble-macro-comparison.md) passes 54 scenarios
over 9,437,184 stereo audio samples with maximum error zero. It independently
checks both original generator states, their outputs and all five mapped
targets, using original native relation/Drive/Wobble functions with explicit
seeds and manually settled inputs. It does not execute the original full plugin.

The host test checks nondefault macro controls, disable/re-enable while Flux
advances, bypass pauses, Amount and Stereo changes, simultaneous seed changes
in either order, full reset after warmup, and irregular versus 64-frame buffers.
It compares the CLAP with the standalone C composition. Old-state migration,
state round trips and existing direct-mode automation remain covered.

Production generator seeds are indices in the complete serialized relation
table. Those indices and the original model's notification/startup timing remain
unknown. See [seed derivation](flux-seed-findings.md). This milestone implements
the recovered settled Wobble graph; it does not establish complete-plugin host
render equivalence or equivalence to other RC-20 versions/platform builds.
