# Drive Cores 0.2.0

`RetroStudyDriveCores.clap` packages the three recovered numerical cores used by
RC-20's `magicDistort` processor: Tube Pair, Air Pressure and Iron Transformer.
It provides the original direct-core mode plus a recovered stereo Processor
mode. See [Processor mode](magic-processor-implementation.md) for frequency
splitting, output filtering, gains, smoothed mix and new controls. The complete
RC-20 Distortion chain is still being reconstructed.

[Distortion control findings](distortion-control-findings.md) records the actual
six selectable type IDs, chain gains, filters, routing and Flux mappings.
Tube Pair maps to internal type 5, Air Pressure to 6, and Iron Transformer to 7;
the standalone Character control instead uses the compact study indices 0/1/2.
Crunch, Heavy and Zap use the other `distort` processor family and are pending.

## API and host behavior

The opaque `retro_drive` API in `src/dsp/drive.h` provides create/set/prepare/
process/reset/destroy. Process is in-place stereo float32 with arbitrary frame
counts and no allocation. Prepare allocates Air Pressure's delay rings; control
updates recompute numerical coefficients while retaining each core's histories.
All per-instance API calls must be serialized. Prepare accepts 1 kHz–1.536 MHz.

| CLAP ID | Control | Range / default |
| --- | --- | --- |
| 4000 | Amount | [0,1], default .5 |
| 4001 | Character | 0 Tube Pair, 1 Air Pressure, 2 Iron Transformer; default 0 |
| 4002 | Mix | [0,1], default 1 |
| 4003 | Enable | 0/1, default 1 |
| 4004 | Bypass | 0/1, default 0 |

Mix is a study linear dry/wet blend applied after the numerical core. Mix zero
keeps the active core running. Enable zero and Bypass copy dry and freeze all
histories. Switching Character freezes the inactive cores and resumes the
selected core's retained history; Amount updates coefficient sets for all
three. Activation and reset clear all core histories and Air Pressure's delay
rings. This deterministic study reset is compared with freshly zeroed native
histories, not claimed to reproduce original plugin reset timing.

State format 2 (`RSDC`) stores all eleven controls and loads old format 1
with the new controls defaulted. The original format stored five controls in explicit little-endian
records, with short-read/write support and atomic rejection of malformed state.
Sample-offset host events are supported. The module uses a host's generic
parameter editor. Amount is the normalized inner numerical setup parameter;
it does not include RC-20's per-character chain gain or Amount remapping.

## Core evidence and validation

REA 6.8.0/Ghidra recovered original numeric setup, process/shape, outer
CalcInternals, ResetAudio and Process routines. Retained local results are
`analysis/re/dist-*.json`; Evidence was exported as `distortion-evidence.json`
and the session closed into `distortion.rea`. The hash-pinned reference slice
is identified in [reference-manifest.json](reference-manifest.json).

- [Tube Pair](tube-pair-findings.md): five first-order stereo filters and two
  gain controls around asymmetric atan/linear stages. Original numeric setup
  at 0xb1ab0 and sample process at 0xb2550. GCC and Clang each match 80 scenarios,
  425,984 output samples and 221,512 complete coefficient/history comparisons.
- Air Pressure: a delay difference feeds double-precision atan and an integrator,
  with smoothed gain/decay controls and a high-pass output. Setup at 0xb1cf0,
  process at 0xb22f0. GCC and Clang each match 64 scenarios, 262,144 samples,
  all 262,144 compared numeric states, and delay-ring contents. Native fixture
  pointers/masks are reconstructed with power-of-two rings large enough for
  all interpolation taps; the original allocator is not invoked.
- [Iron Transformer](iron-core-findings.md): integrated atan and its difference,
  with five first-order filters. Numeric setup at 0xb1f60; inner sample path
  inlined in ShapeMono2/ShapeStereo2. GCC and Clang each match 43 scenarios and
  352,256 samples. Channel-zero numerical state matches all 42 floats;
  channel-one output is checked against an independent native mono fixture.
  The fixture isolates outer splitting and reads inner output before outer
  DC/mix arithmetic, as detailed in the findings.

Across the three cores this is 187 scenarios / 1,040,384 output samples per
compiler with zero error. Cases include low/high sample rates, Amount endpoints,
coefficient changes preserving history, study history reset, impulses, DC,
transients, random inputs and tiny values. Original math imports are verified:
0x578a2c is double exp, 0x57895a is double atan. Reference and port use Linux libm.

`tests/drive_host.c` covers five rates and all characters, state I/O/rejection,
warmed reset, inactive histories, in-place versus separate output, irregular
buffers, simultaneous controls and sample-offset automation. C API invalid
parameter rejection is checked without altering accepted processing state.
Machine-readable reports and source hashes are in
[drive-cores-validation.json](drive-cores-validation.json).

## Remaining work

The original core comparisons exclude the outer path, which is now separately
recovered and tested in Processor mode. Remaining work includes chain pre/post
gains and EQ, the other `distort` processor family, macro and Flux routing, and
original ModelStore scheduling. The inspected Magic path has no internal
oversampling stage; see the [bounded investigation](distortion-oversampling-findings.md).
Both study modes process at the host sample rate and report zero adapter latency. It is not yet an
RC-20 character preset or complete Distortion replacement. Original macOS math
rounding and full host reference renders remain unverified. CI builds universal
arm64/x86_64 bundles and tests the adapter on the runner architecture; a real
DAW smoke test remains pending.
