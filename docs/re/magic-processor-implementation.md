# Magic Distortion stereo processor

Drive Cores 0.2.0 adds a Processor mode around the previously recovered Tube
Pair, Air Pressure and Iron Transformer numerical cores. The C11 implementation
in `src/dsp/magic.c` reconstructs the selected stereo `magicDistort` processor's
frequency split, output low-pass, band gains and smoothed mix. The complete
RC-20 Distortion chain and its front-panel Amount/Mix/Flux routing remain pending.

## Native processing path

Retained REA evidence covers `cAEMagicDistortion::CalcInternals` (0xafc70),
`ResetAudio` (0xaef90), `Process` (0xaf570), and ShapeStereo 0/1/2 at
0xb0f30/0xb1200/0xb14d0. Raw stereo listings corroborate arithmetic order where
Ghidra expressions reorder additions. Native SetParam dispatch, FindParam's
string table and GetParamRange's tables establish physical input fields.
The exact reference hash and Evidence IDs are recorded in
[drive-cores-validation.json](drive-cores-validation.json).

For each channel, the processor low-passes input at Low Frequency, then
low-passes the residual at max(Low Frequency, High Frequency). The latter band
feeds the selected numerical core. Remaining low/high content is recombined
with Passed Band Gain. The nonlinear result passes through Output Lowpass and
Processed Band Gain. The native Mix input is smoothed by a separate first-order
filter, then blends those branches with the dry input. This is not the study's
simple post-core Mix operation.

The two split coefficients and output coefficient use `1-expf(-2*pi*f/rate)`,
clamping normalized cutoff to native float32 .49999. The mix smoother has its
own native 50 Hz coefficients and mixed single/double precision boundaries.
Setup preserves histories. The reference's small `+1e-4`/`-1e-4` rounding steps
are retained in split/output recurrences and in Process's final history cleanup.
Gains use native float expf dB conversion. Parenthesization of combined
mix/gain products and sums is preserved.

The original block adapter calls Process in 64-frame batches. The C processor
keeps that cleanup cadence internally, so ordinary host buffer partitions do
not change its samples. A parameter event updates the effective coefficients
at its host offset while retaining the cadence; that sample-offset behavior is
a study adapter contract, separate from the original ModelStore scheduler.

Iron's stereo assembly uses the same numerical filter association as its mono
core, despite differently ordered Ghidra expressions. An independent stereo
fixture verifies this; `rs_iron_transformer_process_stereo` retains the existing
per-channel arithmetic. See [Iron findings](iron-core-findings.md).

## Controls and compatibility

The CLAP retains IDs 4000–4004 (Amount, Character, Mix, Enable, Bypass). New IDs:

| ID | Control | Range / default |
| --- | --- | --- |
| 4005 | Processing Mode | 0 core, 1 processor; default 0 |
| 4006 | Low Frequency | 20–20000 Hz; default 20 |
| 4007 | High Frequency | 20–20000 Hz; default 20000 |
| 4008 | Processed Band Gain | -100–100 dB; default 0 |
| 4009 | Passed Band Gain | -100–100 dB; default 0 |
| 4010 | Output Lowpass | 20–20000 Hz; default 20000 |

Core mode preserves the 0.1.0 numerical-core behavior and simple Mix. Processor
mode uses the full stereo path above. Normalized Amount/Mix are multiplied by
100 for native physical engine inputs; the native coefficient calculation then
applies its own .01 factors. This deliberate rounding differs from feeding
normalized Amount directly into a core. Low > High follows the native max rule.
The six added controls affect Processor mode only.

Enable zero and Bypass copy dry and freeze histories. Switching modes freezes
the inactive backend and resumes its history when selected again. Activation
and full study reset clear both backends and restart the 64-frame cleanup
clock. All controls are saved in `RSDC` state format 2; format 1 states restore
the original five controls and default the six additions to Core mode.

The standalone `retro_magic` API uses physical Amount/Mix [0,100], Character
0/1/2, frequencies in Hz and band gains in dB. It accepts distinct stereo
float32 buffers in place and arbitrary frame counts, allocates only during
prepare, and requires serialized per-instance calls. Silent input still runs
the processor; original silent-batch shortcuts are outside this comparison.
Only resource-selected local shapes 0/1/2 are implemented. Local shape 3 exists
in the binary but is not selected by the three corresponding RC-20 characters.

## Oversampling correction and remaining boundary

The earlier milestone listed oversampling as an assumed missing layer. The
[oversampling investigation](distortion-oversampling-findings.md) instead shows
that the inspected Magic path, its immediate plug chain and block adapters
forward sample rates/counts unchanged and process one core sample per input
sample/channel. GetLatency returns zero. No internal oversampling is added to
the reconstruction. This bounded finding does not establish the complete
outer host/container sample-rate provenance or every other processor's behavior.

Processor mode still excludes plugChainDist's -12/+12 dB staging, per-character
gain curves, external Mix before the nonlinear chain, Flux gain modulation,
other Crunch/Heavy/Zap engines, and original notification/boot sequencing.
The core Amount slider is not yet the original character's full front-panel
mapping. Reference fixtures use synthetic state and Linux libm; macOS math
rounding, complete plugin renders and real DAW behavior remain unverified.

## Reproduction

```sh
python3 tools/compare_native_iron_stereo.py --library build-release/libretro_dsp.so
python3 tools/compare_native_magic.py --library build-release/libretro_dsp.so
```

The full-processor fixture reconstructs numerical histories and Air's delay
rings, runs original CalcInternals and Process with active stereo buffers, and
compares complete audio output with C. It does not invoke original constructors,
resource scripts, allocators or full plugin entry points. Study reset is checked
against fresh synthetic native state. Tests cover static settings, extreme band
gains, reversed cutoffs, uncommon sample rates, dynamic controls/character
changes, reset and irregular C buffer partitions. Both GCC and Clang pass
252 scenarios / 2,064,384 audio samples with zero error and identical float patterns. The independent Iron stereo check passes
42 scenarios / 688,128 samples per compiler, with exact numerical state.
Counts and source hashes are in [drive-cores-validation.json](drive-cores-validation.json).


## Chain mode follow-up

Drive Cores 0.3.0 adds a separate [Chain mode](dist-chain-integration.md) with
three independent processor instances, original bracketing gains and settled
Flux/Mix routing. The Processor mode documented here retains its direct-control
behavior. Full original graph transition scheduling and the classic characters
remain pending.
