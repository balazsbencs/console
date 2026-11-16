# Classic character chains

Drive Cores 0.5.0 adds Processing Mode 4, Classic Chain. Character 0/1/2 selects
Crunch/Heavy/Zap with the recovered pre/post peak EQ, gain stages, Amount
mappings and seeded Dist Flux. Mode 3 remains the direct Classic processor;
Modes 0–2 retain their existing behavior. No parameter IDs were added.

## Signal path and mappings

The wet path is global −12 dB → shared classic Amount gain −19→0 dB → selected
pre EQ → selected Classic processor → selected post EQ → shared classic Amount
gain +19→0 dB → global +12 dB. Each gain row uses the recovered native gain
smoothing. Each character has separate EQ and processor histories; the
bracketing classic gain pair is shared, matching its resource rows.

Crunch uses a 900 Hz, Q .4 peak pair at ±(10→12 dB). Heavy uses the same
frequency/Q and anchors with the native `sym .15` Amount mapping. Zap uses
850 Hz, Q .2 and ±(10→16 dB). Processor Amount, reference, processed-band gain
and output lowpass follow the resource rows rather than the direct processor
study controls. See [mapping evidence](classic-chain-findings.md) and
[peak EQ evidence](peak-eq-findings.md).

Parenthesized `(Amount)` links use the same numerical normalized input as
`Amount`; the parser stores a separate flag that the inspected numerical
update/scaling path does not read. Heavy's whole-interval rational mapping
is distinct from a generic symmetric easing function. At normalized Amount
.5 its engine Amount is about 15 percent, not 50 percent.

The recovered external scalar routing remains:

```text
A = Magnitude * Amount
wet_mix = Mix * FromTo(A,0,1,0,1,.99)
wet_input = input * (flux_input_gain * wet_mix)
output = input * (1-wet_mix) + classic_chain(wet_input) * flux_output_gain
```

Wet Mix acts before the nonlinear chain. The shared Dist Flux generator and
its reciprocal gain mappings reuse the native-tested functions from Chain
mode. Flux Seed zero is an explicit study default; the production serialized
relation index is unknown.

## Timing, controls and state

Amount, Character, Mix, Magnitude, Flux, Flux Seed, Low/High Frequency and
Enable latch at each 64-frame Classic Chain tick. Disabled batches copy input
and advance Flux while freezing EQ, gain and processor histories. Bypass and
other modes pause the entire Classic Chain clock; returning resumes it. Reset
restarts the clock, generator and all histories. Inactive character histories
pause independently. Frequency controls accept physical Hz and do not claim
to reproduce the original GUI knob's interpolation.

Processed Band Gain, Passed Band Gain, Output Lowpass and Reference Level
remain direct-processor study controls; Classic Chain derives its own values
from the resource. The EQ adapter retains original coefficient interpolation,
four-frame rounding and bypass transitions within a virtual 64-frame cadence.
Arbitrary host partitions therefore preserve output without adding latency.
The setter batches changed physical EQ controls; the resource keeps each EQ's
frequency/Q fixed while Amount changes its dB target.

State format 5 stores the existing fifteen controls. Formats 1–4 migrate with
existing values preserved and missing fields defaulted. Historical modes keep
their numeric values and Character meanings. Heavy on non-SSE platforms still
uses division in the nonlinear processor; x86-reference bit identity is not
claimed for arm64.

## Validation and boundaries

`tools/compare_native_classic_chain.py` independently composes the original
numerical peak EQ, Classic, Gain, NoiseGen, relation and parameter-scaling
methods with synthetic state. It compares the resulting audio and scalar
routing/processor targets, including Amount/character/frequency/seed changes,
disabled periods, silence tails, reset and irregular C-call partitions.
No original constructor, resource script, plugin host or full graph scheduler
executes. Original numerical math imports are rebound to Linux libm.

The external connection gains here are settled constants per 64-frame tick.
Original connection interpolation, full notification order, boot sequencing,
graph summation and complete host renders remain open. This milestone
establishes the recovered classic path under the documented adapter, not
complete RC-20 plugin equivalence. Machine-readable results are in
[classic-chain-validation.json](classic-chain-validation.json).

The actual REA EQ records were recovered from the completed local session,
merged with the existing evidence, validated through REA import, and exported
as `analysis/re/classic-chain-evidence.json` (98 records). The original client
had ended before snapshot export; this bundle retains the genuine results
without claiming a new provider snapshot.
