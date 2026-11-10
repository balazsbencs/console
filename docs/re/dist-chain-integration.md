# Three-character Distortion chain and Flux

Drive Cores 0.3.0 adds Chain mode for the resource's Tube Pair, Air Pressure and
Iron Transformer paths. It connects the recovered gain processors, per-character
Amount mappings, independent Magic processor instances, one Flux generator and
its reciprocal pre/post gain mappings. Crunch, Heavy and Zap remain pending.

## Resource chain and scalar targets

The decoded chain declaration and control sources are documented in
[distortion-control-findings.md](distortion-control-findings.md). All three
characters have global -12 dB before their engine path and +12 dB afterward.
Tube Pair additionally has +22*Amount dB and its negative around its engine.
Iron uses a rational .4 Amount curve for both its +/-18 dB pair and engine
Amount. Air's engine Amount maps to 0–75 percent; Tube maps to 0–100 percent.
The applied Amount here is Magnitude multiplied by the Amount knob.

The actual `gain` factory row selects `cAEGainPlug`, separately from `xdgain`.
Its targets reset to their current accepted gain, then smooth with exponential
steps and four-sample interpolation. The interpolation lanes are 0/.25/.5/.75,
verified from the constant copied by native initialization. The target/snap
decision latches per 64-frame Process call. Details and native comparisons are
in [gain-findings.md](gain-findings.md).

Native `scAEPlugChain::CreateFromString` raw disassembly identifies `sym` as
scale enum 5 (0x9fe96–0x9ff02). Record construction writes source range 1 at
node+0x2c (0xa0cb7), endpoints at +0x38/+0x3c, scale enum +0x40 and curvature
+0x44. UpdateParamFromXDParam passes node+0x24 into the scaling function at
0x9f090. Its linear and symmetric cases therefore consume normalized source
values. The symmetric case uses the same recovered rational curve, with native
float arithmetic and output-range limits. Native composite tests use that
original scale function for the engine and bracketing gain targets.

The REA decompiler's CreateFromString result stops at an incorrectly inferred
non-returning allocation import. The parser observations above come from raw
machine-code inspection, not a claim that the incomplete pseudocode establishes
the full parser. Relevant raw listings and genuine retained REA results remain
local under `analysis/re/`; the validated Evidence export is
`dist-chain-evidence.json`.

## Flux and external routing

One bipolar noise generator supplies `Dist_InGain_Feed`. Its Chaos is zero,
Decay .405, and output uses original Drive's factor 16. The settled source graph
uses group Flux `(Magnitude*Flux)*C(Amount,.99)` to derive two FromToKnob values:
gain anchors 0/.16/.36 and smoothing anchors .913/.913/.1, both with half curves
.5. The noise is added to baseline .5 and clamped to [0,1].

Opcode K maps the feed to pre/post gains with (-3,20) and (3,-20) dB endpoints,
using separate lower/upper curves and a unity midpoint. It is not a single
linear dB sweep. Both mappings are separately native-tested; see
[knob-gain-findings.md](knob-gain-findings.md). Their float products can differ
slightly from one, so the implementation preserves both calculations.

The settled external routing is:

```text
A = Magnitude * Amount
wet_mix = Mix * FromTo(A,0,1,0,1,.99)
wet_input = input * (flux_input_gain * wet_mix)
wet_output = selected_chain(wet_input)
output = input * (1-wet_mix) + wet_output * flux_output_gain
```

The wet Mix factor enters before the nonlinear chain. Each selected path uses
its own persistent Magic processor, and each bracketing gain row has its own
history. Switching character freezes inactive engine/gain histories rather than
sharing one outer filter state across all three characters.

External connection gains in this milestone are explicit, settled constants
for each 64-frame batch. This implements the recovered resource formulas and
permits native DSP composition checks. It does not reconstruct original graph
connection interpolation, notification order, zero-buffer optimization or full
host routing. Those timing details require original complete-graph evidence.

## Host controls and lifecycle

Processing Mode 2 (`Chain`) uses Amount, Character, Mix, Low/High Frequency and
three added controls:

| CLAP ID | Control | Range / default |
| --- | --- | --- |
| 4011 | Magnitude | [0,1], default 1 |
| 4012 | Flux | [0,1], default 0 |
| 4013 | Flux Seed | integer [0,65535], default 0 |

Modes 0 Core and 1 Processor retain their behavior. Processed/Passed Band Gain
and Output Lowpass remain Processor-mode study controls; Chain uses each
resource engine's fixed defaults instead. Physical Low/High Frequency inputs
are retained in Hz; they directly supply the engine frequencies rather than
claiming the original GUI knob-position curve.

All Chain control targets, including effective Enable, latch at its next
64-frame tick. Disabled Chain batches copy dry audio and keep Flux advancing,
while chain gain and engine histories pause. Bypass and other processing modes
pause the entire Chain clock. Returning to Chain resumes the partial clock.
A seed change restarts the generator at the next tick. Full reset and activation
restart both noise and all DSP histories; gain histories snap to their reset
targets like the isolated native gain reset. These are explicit study adapter
rules, not the original host's notification timing.

State format 3 stores fourteen controls. Version 1 (five controls) and version 2
(eleven controls) load with additions defaulted, retaining their previous Core
or Processor mode. The original production seed is a serialized relation-record
index that remains unknown; the exposed default zero is a repeatable study seed.

The C implementation is `src/dsp/dist_chain.{h,c}`, with a standalone opaque
API and separate Flux tick/target functions for native comparison. Processing
is stereo float32, accepts arbitrary host buffer sizes and allocates only at
create/prepare. Per-instance API calls must be serialized.

## Reproduction and fidelity boundary

```sh
python3 tools/compare_native_gain.py --library build-release/libretro_dsp.so
python3 tools/compare_native_knob_gain.py --library build-release/libretro_dsp.so
python3 tools/compare_native_dist_chain.py --library build-release/libretro_dsp.so
```

The combined fixture uses original numerical gain engines, original Magic
stereo processors, original relation arithmetic/parameter scaling and original
noise Drive with synthetic state and manually settled inputs. It does not run
resource scripts, constructors, a complete plugin, or the original graph
scheduler. Source hashes and check summaries are recorded in
[drive-cores-validation.json](drive-cores-validation.json).

The remaining Distortion work includes the classic engine family, complete
original connection gain transitions and summation order, production seed/boot
sequence and full host reference renders. The inspected Magic chain has no
internal oversampling stage, as documented in the
[bounded audit](distortion-oversampling-findings.md). Linux native comparisons
do not establish original macOS math rounding or cross-build reference identity.


Both GCC and Clang comparisons pass 108 scenarios / 41,472 ticks / 5,308,416
audio samples per compiler with zero audio/control error. All 1,658,880 compared
noise-state bytes and 373,248 target values match bit for bit. Tests cover three
rates, two seeds, all three initial characters, zero Flux/Amount/Magnitude,
static full/partial settings, dynamic character/frequency/Amount/Mix changes,
seed swaps, disabled intervals, reset and irregular C buffer partitions.

Host tests pass all three modes across five rates, character changes, boundary
and sample-offset events, state migration, invalid controls and reset under GCC,
Clang and ASan/UBSan. The validator reports no failures: 32 passes, 11 skips and
one denormal timing warning (2.06x) while other native checks were running. The
isolated follow-up passes at 1.67x; the existing FTZ/DAZ guard remains enabled
and host floating-point state restoration is checked by the host test.
