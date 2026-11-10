# RC-20 reconstruction status

## Digital 0.3.0, Wobble 0.2.0 and Magnetic 0.2.0

The C11 Digital engine and Linux CLAP now implement the recovered `cBitColor`
stereo path with both Focus and Cut modes, native decreasing Fit3 curves,
normalized rate smoothing, internal LFO, gain compensation and reset behavior.
The CLAP adds Amount, Rate/Bits Balance, Magnitude and Enable alongside the
physical study controls. Flux now uses two native-verified generators at the
recovered sample_rate/64 update rate, once per 64-frame batch. It has explicit
study seed controls because production relation-table seed indices are unknown.
IDs 1000–1019 remain stable; Flux/seeds use 1020–1022. Version 1/2 states load
into state format 3 with defaults for new controls.

The isolated native Digital kernel comparison passes 90 cases / 368,640 samples
with maximum and RMS error 0 and no unequal samples. Cases cover 44.1/48/96 kHz,
Focus/Cut, physical and normalized control paths, internal modulation,
batch-aligned parameter updates and reset. Inputs include tones, impulse pairs,
silence, transients and signals exceeding nominal full scale.

Independent native comparisons also pass:

| Port | Tested scope | Result |
| --- | --- | --- |
| Fit3 / log curves | 4,004 evaluations | Float bits identical |
| Triangle / curved triple / modulation styles | 21,665 cases | Float bits identical |
| FromTo mapping | 20,007 evaluations | Float bits identical |
| Composite Flux + Digital | 3,072 ticks / 393,216 audio samples | Zero error; noise state bits identical |
| NoiseGen reset / params / values | 2,176 cases, 6,528 updates, 417,792 values | Output bits and 40-byte state identical |

The CLAP validator passes 33 tests; 11 tests skip unsupported optional
extensions. Release and ASan/UBSan CTests pass. Host checks include lifecycle,
repeated activation, separate instances, short state reads/writes, rejected
truncated state, old-state migration, seeded Flux state round-trips, in-place stereo, sample-offset events and
same-time macro events in either order. DSP partition/reset checks cover both
filter modes, normalized controls and LFO across arbitrary buffer sizes. Flux
host checks also compare irregular versus 64-frame buffers and repeatable reset,
including disabled Digital intervals while the model noise generators advance.

## What the comparisons establish

- Reference: supplied RC-20 1.0.5 macOS x86_64 slice, identified in
  [reference-manifest.json](reference-manifest.json).
- Linux runs isolated hash-pinned machine-code functions with synthetic state,
  reconstructed callbacks and system libm. No plugin constructor, macOS runtime,
  original editor, resource scripts or installer executes.
- Original Digital process: `Block::cBitColorBlock::processAudio`, `0x345df0`.
  Original Scale_Fit3/Scale_Log callbacks execute for normalized controls.
  Updates occur at the native kernel's 64-frame boundaries; the synthetic model
  explicitly supplies the resulting parameter/cache values.
- The tests establish kernel and helper behavior. Original Model observer
  invalidation, full dependency propagation and host automation timing are not
  reproduced by these fixtures. The CLAP applies host events at sample offsets;
  that adapter behavior is tested against the C engine.
- Macro wiring and native arithmetic now compose with original Drive and
  Digital in the Flux harness. That harness settles upstream notifications
  explicitly before each tick; it does not reconstruct the complete ModelStore
  scheduler. Full original macro-driven host renders remain pending.
- The shared NoiseGen and relation functions are connected to Flux in the CLAP.
  Native generator seeds are complete relation-record indices. Production
  indices remain unknown; the study explicitly defaults to seeds 0/1. The
  recovered engine calls sync before each 64-frame render at sample_rate/64,
  and also during prepare. Study startup/reset semantics remain distinct from
  the original's unverified model initialization sequence.
- CLAP Bypass freezes both Digital and Flux. Macro Enable freezes the Digital
  kernel while Flux advances. Physical mode freezes Flux. These host adapter
  choices are tested; complete original graph lifecycle behavior is unverified.
- Comparisons use Linux libm for native and ported functions. They do not
  establish macOS libm rounding or Windows/macOS cross-build equivalence.

Wobble is now a second independent C/CLAP module, matching 46 native cases /
3,670,016 audio samples with zero error. It includes wow/flutter oscillators,
variable delay and interpolation, smoothing, stereo phase and enable fades.
Amount/Balance/Magnitude, Stereo and the two Flux paths now compose with that
kernel, matching 54 native scenarios / 9,437,184 samples with zero error, using
explicit seeds and settled notifications. Production seed indices and full
original model timing remain open. See [Wobble implementation](wobble-implementation.md),
[macro integration](wobble-macro-integration.md) and
[wobble-validation.json](wobble-validation.json).

Magnetic has an independent C kernel and CLAP with direct inputs and recovered
Amount/Balance/Magnitude/Flux/Dropouts mappings. Its direct kernel matches 76
native scenarios / 6,619,136 samples; the four-generator macro composition
matches 60 scenarios / 10,223,616 samples, both with zero error under GCC and
Clang. Production seeds and original model timing remain open. See
[Magnetic implementation](magnetic-implementation.md),
[macro integration](magnetic-macro-integration.md) and
[validation](magnetic-validation.json).

Drive Cores 0.4.0 exposes the recovered Tube Pair, Air Pressure and Iron
Transformer numerical cores. Across 187 scenarios / 1,040,384 samples per
compiler, outputs match the isolated native cores with zero error under GCC
and Clang. Processor mode now adds the recovered stereo frequency split,
output filtering, gains and smoothed mix; see
[Magic processor](magic-processor-implementation.md). The inspected path has no
internal oversampling stage. Chain mode now adds the three-character gain stages and settled Flux/Mix
routing with independent engine histories. See [Chain integration](dist-chain-integration.md).
Classic mode now adds the recovered Crunch/Heavy/Zap single-band processors,
including focus filters, output lowpass, band gains, Mix and Reference Level.
Their original preset EQ/gain chains and complete host graph timing remain
pending. See [Classic implementation](classic-implementation.md).
See [Drive Cores implementation](drive-cores-implementation.md) and
[validation](drive-cores-validation.json).

GitHub Actions builds all four Linux modules and universal arm64/x86_64 macOS
bundles. Host tests run on the CI runner architecture; the workflow verifies
both Mach-O slices and preserves bundle structure in uploaded ZIPs. The
complete RC-20 chain remains pending.

## Recovered module graph

`FX_GUIBoot.lua` defines `masterFXSpec` and its connections. Main serial order:
input gain → Wobble → Distortion blend → Digital → Space → optional master
filters/tone → Magnetic → Width → output gain. Noise has switchable pre/post
routing, with a sidechain-driven compressor in each noise container.

| User-facing effect | Actual block(s) | Implementation status |
| --- | --- | --- |
| Noise | cNoiseFX + cSimpleComp, pre/post routing | Graph recovered; DSP pending |
| Wobble | cWobbler | Stereo kernel and seeded macro/Flux graph native-tested; original model timing pending |
| Distortion | cPlugChain + cGain blend | Three Magic chains and three Classic processors; Classic preset EQ/gain chains and original graph timing pending |
| Digital | cBitColor | Focus/Cut, curves, LFO, dynamic updates/reset and seeded Flux implemented; full model pending |
| Space | cResoX | Graph recovered; DSP pending |
| Magnetic | cFlutter | Stereo kernel and four-generator macro graph native-tested; original model timing pending |
| Global | cPlugChain filters/tone + cStereoWidth + graph gains | Graph recovered; DSP pending |

The binary also contains generic `cSampleDegrader` and `cSimpleReverb` blocks.
They are not the Digital and Space implementations selected by this graph.
Their exploratory decompilation remains in `analysis/re/` for provenance.

`Config.lua` contains 58 literal host automation declarations, indexed in
[automation-declarations.json](automation-declarations.json). These declaration
indices are not yet verified VST parameter IDs; the study uses distinct CLAP
parameter IDs 1000–1019.

## Evidence and reproducibility

REA 6.8.0 with Ghidra 12.1.4 analyzed the thin slice through a project-local MCP
client. Ghidra and JDK 21 were reused from the existing local cache. No global
MCP registration changes were made. The session was exported and closed.

Local retained artifacts:

- `analysis/re/evidence.json` and `reference.rea`: initial 31 Evidence records
  and provider-neutral retained-query snapshot.
- `analysis/re/digital-v3-evidence.json` and `digital-v3.rea`: expanded Evidence
  export and snapshot. Snapshots retain queries, not a complete Ghidra database;
  uncached queries need the engine again.
- `analysis/re/bitcolor-*.json`, `scale-*.json`, `noisegen-*.json`,
  `relation-*.json`, `model-sync-*.json`: static findings and assembly.
- `analysis/re/digital-v3-comparison.json`, `native-curves-comparison.json`,
  `native-relations-comparison.json`, `native-noisegen-comparison.json`: native
  comparison reports against the built Release C library.
- `analysis/re/native-fromto-comparison.json` and `native-flux-comparison.json`:
  FromTo and composite Flux/Digital checks.
- `analysis/re/clap-validator-v3.json`: validator results for Digital 0.3.0.
- `analysis/unpacked/`: CRC-verified extracted resources and manifests.

Implementation notes: [macro resource wiring](digital-macro-findings.md),
[native relation primitives](relations-findings.md),
[native NoiseGen](noisegen-findings.md), [Flux integration](flux-integration.md),
[seed derivation](flux-seed-findings.md). Machine-readable check results and
source/artifact digests: [validation.json](validation.json).

Key REA Evidence IDs:

| Observation | Evidence ID |
| --- | --- |
| Binary overview | ev_f3fc84b2fead91d9561ee798ad3513badd414cd8ea0eeb8d2640f0d888c499a1 |
| Digital process pseudocode | ev_a4512656d87dc79258d3a2590448b67b9169d89394fdd7c4d88524d7273f375a |
| Fit3 scaling | ev_9c3ccc65878e66e1694d13c327c6d05d11aed146b64b4f593f854a3c2f5d7f66 |
| Log scaling | ev_747dbc579cd5a9445f13354081db48fb9bbba62f415dc5cacb30df81698de0d2 |
| Relation arithmetic | ev_2264fcc5e0de6b5be48dd1a2e3722e322008f89988a36f337d31eb583c94db5e |
| Noise generator value | ev_1afa07fad8515c406f89e27218b1ae31878ccb9d1777c1adbdbe1b517ad935f2 |
| Model sync | ev_3aebb6d89864f52e6c31946f2482335d08e842ae93f86f456883134e15c26b93 |
| XPak XOR reader | ev_434fff2bda143c412109a7d0ebfbcdede990355482c8ccd13d404f96d1da14ea |

The script extraction and Linux native-kernel experiments are local tooling
observations, separate from REA's static Evidence. Ghidra reported malformed
instructions and relocation issues in some other functions, including the
root-construction region. Full-plugin execution has not been observed.

## Next work

1. Capture the emitted relation specification to establish original Digital
   seed indices and full dependency order. Match boot/reset sequencing.
2. Capture original macro-driven Digital renders to validate graph notification
   timing and enable/filter-mode transitions beyond settled kernel fixtures.
3. Integrate the classic Distortion preset EQ/gain chains and match complete original graph
   transition timing. Port cResoX Space and Noise/sample paths.
4. Integrate the global graph and original control mappings into a separate
   complete-effect CLAP, then validate full-chain reference renders in a host.
5. Confirm Windows/macOS reference equivalence and add platform builds and a
   real DAW smoke test. macOS CI now supplies universal builds and host tests.
   The supplied Windows DLL is extracted and hashed but
   has not been executed or cross-build matched.
