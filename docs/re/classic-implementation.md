# Classic Distortion processor

Drive Cores 0.4.0 adds the recovered `cAESingleBandDistortion` stereo processor
as Processing Mode 3, Classic. Character 0/1/2 selects Crunch/Heavy/Zap. These
are the first three Distortion characters selected by the supplied resource
configuration. The existing Core, Processor and Chain modes retain their
Tube Pair/Air Pressure/Iron Transformer selection and IDs.

This milestone implements the processor, not the original character presets:
their pre/post peak EQ, additional gain stages, macro mappings and original
notification timing remain outside Classic mode. Magnitude, Flux and Seed
still apply only to Chain mode. Factory defaults are explicit study choices.

## Recovered processing

`src/dsp/classic.c` ports parameter calculation, two-stage focus splitting,
three nonlinear shapes, output lowpass, dry/wet mix and processed/passed-band
gains. `classic_split.c` preserves the splitter's sequential float recurrence.
The physical C API accepts Amount/Mix as percentages, three frequencies in
20–20000 Hz and three dB controls in −100–100 dB. The CLAP adapter normalizes
Amount and Mix to 0–1 and adds Reference Level as ID 4014. It is used only in
Classic mode. The full range of 1000–1536000 Hz sample rates is validated.

Crunch applies a cubic-like saturator with recovered amount-dependent drive
and normalization. Heavy uses a biased rational transfer with an SSE
reciprocal estimate followed by one Newton correction on x86. Zap applies
truncated-integer wrapping and the recovered polynomial. The integer
conversion explicitly models the SSE indefinite result for out-of-range
values, avoiding undefined C casts. An unexposed fourth native shape is
outside this port.

Operation order matters: Heavy computes its numerator before multiplying the
corrected reciprocal, the wet output is mixed before its rounded history is
reused, and splitter histories are rounded once at the end of each native
processor call. The adapter preserves the native 64-frame reference cadence
regardless of host partitions. It uses the native vector transfer arithmetic
for every sample, including small host buffers; it does not select the
original scalar remainder's different Heavy division expression. No buffering
or added latency is introduced. Reset clears all six histories and restarts
the 64-frame cadence. Enable/Bypass freeze this study processor's history.

On non-SSE platforms Heavy uses ordinary division. This retains the recovered
rational transfer but does not reproduce the CPU-specific SSE approximation.
The arm64 build and host contract tests establish functionality, not bit
identity with the original x86_64 binary. Linux comparisons also bind the
original imported math calls to Linux libm, rather than establishing macOS
libm equivalence.

## Evidence and verification

The reference is the supplied RC-20 1.0.5 x86_64 Mach-O, SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
[Control findings](classic-control-findings.md) and
[splitter findings](classic-split-findings.md) record addresses, resource
settings and REA Evidence IDs. Local complete evidence is retained in
`analysis/re/classic-evidence.json`; `classic.rea` retains the query snapshot.
The project-local REA session was exported and closed.

`tools/native_classic_probe.py` executes isolated original SetParam,
ResetAudio, CalcInternals and Process64 methods against a synthetic 128-byte
object, with verified mask constants copied into their BSS locations. No
original constructor, global initializer, full plugin or resource script is
executed. Silence shortcuts are disabled for these numerical comparisons.
`tools/compare_native_classic.py` checks both audio and every relevant
coefficient/history after setup and each block, including changes to controls,
character, reset, silence tails and irregular C-call partitions.

See [validation results](classic-validation.json) for compiler-specific
comparison counts, host contract checks and validator results. These results
do not establish complete original Distortion-chain equivalence.

The CLAP exposes fifteen controls and saves state format 4. Historical formats
1, 2 and 3 load with existing values preserved and added controls defaulted.
CI builds all four effects for Linux and as universal arm64/x86_64 macOS CLAP
bundles, verifies bundle versions and both Mach-O slices, and uploads ZIPs
that retain the macOS bundle directory structure.
