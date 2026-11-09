# Digital Flux integration, 0.3.0

The C library and CLAP now combine the recovered macro arithmetic, two shared
NoiseGen instances and normalized Digital processing. This implements the
settled Flux signal path with explicit generator seeds. It does not yet match
the complete original model's boot, reset or host notification sequence.

## Recovered cadence and signal path

`cCottonAudioEngine::prepare` reads `DoSyncEveryBatch` into engine+0x170.
`FX_AudioBoot.lua` creates this control as 1. Before each 64-frame
`RenderOneBatch`, `process` calls the Lua model connection's first virtual
method with sample_rate * 0.015625. Its vtable identifies that method as
`sendNotifications(float,bool)` at 0xef090. This calls `cModelSync::doSync`,
which drives the noise records, then flushes notifications until quiescent
(up to 16 passes). `ModelSyncDriveEnable` defaults to 1 in engine init.

Sources retained under `analysis/re/`:

| Observation | REA Evidence |
| --- | --- |
| Engine process and per-batch callback | ev_df6d0bba08d9f3da8bee0001e46c48e7c65ca41ab5c66fae19db133808f96b6a |
| Prepare / DoSyncEveryBatch | ev_cdd731860810e396ddd44f7a0c8e6635049a5e6ac1525d76135a4de6b64e5ca1 |
| Engine init / drive enable | ev_e1f7fcb8d5fadbc8ef6805f810cacc2e983cb5073c562537a0665b1fb52e020e |
| Relation construction / index seeds | ev_806eab5bc84ae12a5d4451ea36ba7d363be6a56b78b58f8bcf05cb9359b16c4d |
| Sync drive and notification flush | ev_3aebb6d89864f52e6c31946f2482335d08e842ae93f86f456883134e15c26b93 |

Raw assembly independently confirms the callback's rate multiplication and
vtable entry. The original also calls sync during prepare. The study's explicit
reset/activation sequence is not a recovered reproduction of that boot path.

The recovered settled path is:

1. `scaled_flux = (Magnitude * Flux) * C(Amount, .99)` using the native rational
   curve in `rs_relation_mul_triple`.
2. Bit generator: gain=scaled_flux*.3, chaos=0, decay=0,
   smooth=.94847450187182, bipolar=true.
3. Rate generator: gain=scaled_flux*.2, chaos=0, decay=0,
   smooth=FromTo(scaled_flux,0,1,.9484,.85,.5), bipolar=true.
4. Each generator receives update rate=sample_rate/64 and produces one value
   per tick. The relation Drive publishes GetValue()*16.
5. Balance's triangle weights multiply Magnitude*Amount. Each result adds its
   generator output and clamps to [0,1], before Digital's decreasing Fit3 curves.

The two generators have independent state. [Seed findings](flux-seed-findings.md)
establish that native seeds are indices in the complete serialized relation
table. That table is assembled through Lua `pairs()` iteration, so declaration
order does not establish its emitted order or the exact Digital seeds.

## C API and CLAP choices

`src/dsp/digital_flux.{h,c}` exposes `rs_digital_flux_reset`,
`rs_digital_flux_tick` and `rs_digital_flux_targets`. Zero-initialize a state,
reset it with explicit seeds, and tick it once before every 64-frame audio
batch. Get normalized targets, set `retro_digital_params.normalized=1`, and
process the batch with the Digital engine. Callers retain partial-batch progress
across arbitrary buffers. All macro controls must be finite [0,1], with a
positive sample rate. The low-level helper does not validate those contracts.

CLAP adds Flux (1020), Flux Bit Seed (1021) and Flux Rate Seed (1022). Seeds are
integers 0–65535; defaults 0/1 are study choices, not recovered production values.
They can be replaced when the actual emitted relation indices are captured.
The saved state includes controls/seeds, not ongoing audio or RNG histories.
Version 1/2 states load with Flux=0 and the default seeds.

The adapter keeps its Flux clock across host buffer boundaries and parameter
events. Seed changes restart both generators and their clock. Activation and
reset restart the study trajectory. In macro mode, Enable=0 copies dry audio
and freezes the Digital kernel while Flux continues, reflecting the separation
between model sync and the block's disabled path. Bypass freezes both; physical
control mode freezes Flux. These adapter lifecycle choices are explicit and
have not been matched to the complete original host lifecycle.

## Validation and remaining gaps

[Native composite comparison](flux-native-comparison.md): 3,072 ticks, 6,144
noise outputs, 6,144 targets, all noise state bytes, and 393,216 stereo audio
samples match the isolated original functions with zero error. The comparison
supplies explicit seeds and manually settles upstream notifications before each
Drive call. It does not execute the original full scheduler.

Host checks compare CLAP output against the C composition across irregular
buffers and 64-frame buffers, reset both trajectories, and disable/re-enable
Digital while Flux advances. Version 3 state round-trips retain non-default
Flux and seeds; both previous state formats load. Release and ASan/UBSan pass,
as does the CLAP validator (33 pass, 11 optional-extension skips).

Remaining work: capture the actual emitted relation specification, establish
boot/reset sequencing and automation-to-Drive timing, then compare original
macro-driven host renders. Other effects and the complete signal chain remain
unimplemented.
