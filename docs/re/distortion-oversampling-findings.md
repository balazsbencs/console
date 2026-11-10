# Distortion oversampling investigation

**The inspected Magic Distortion path processes one numerical-core sample per
input sample. No oversampling stage appears in its wrapper, immediate plug
chain adapter or block adapter.** Adding oversampling to that reconstructed
path would currently introduce behavior unsupported by the reference evidence.
This is a bounded result about those layers, not a claim that no code anywhere
in the product ever changes sample rate.

Scope: retained REA evidence and read-only raw disassembly of RC-20 1.0.5
x86_64, SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
No new REA session, original constructors, scripts or complete plugin ran.

## Numerical wrapper

Retained evidence:

| Function | Address | Local evidence |
| --- | --- | --- |
| cAEMagicDistortion::Process | 0xaf570 | dist-magic-process.json, ev_bdfbd6742e433a3a00528c9fad60c287d0cdd2c81fd4aa0ba1448448749c4f70 |
| ResetAudio | 0xaef90 | dist-magic-reset.json, ev_b428c1fc9a10a45414aff05e0c44910d936f886e64bb8013e00c73985f772c55 |
| CalcInternals | 0xafc70 | dist-magic-calc.json, ev_f500e9c6180c0966d1317252b148a4fadfe7fb00c00ed56bd0673f5ecd398baa |
| GetLatency | 0xaf490 | dist-magic-latency-raw.asm |

All local evidence paths are under `analysis/re/`.

Process records the incoming sample count and dispatches the same count and
input pointers to ShapeMono/ShapeStereo for local shape 0, 1, 2 or 3. Its other
branches handle bypass/silence, dirty-coefficient recalculation and small-state
rounding. There is no intermediate upsampled buffer, multiplied sample count,
rate-conversion call or matching decimation stage in that wrapper.

ResetAudio stores its supplied sample rate directly at state +0x608. Its reset
work initializes filters and clears histories. CalcInternals passes that stored
rate unchanged into TubePair/AirPressure/IronTransformer setup, including the
multi-core local shape 3. There is no hidden rate multiplier between the stored
rate and those setup calls. Constructor defaults set the stored rate to 44.1
kHz, but that default does not establish the rate used by an actual host.

GetLatency is `xor eax,eax; ret` at 0xaf494–0xaf497: it reports zero samples.
Zero latency alone would not prove the absence of oversampling; here it
corroborates the directly observed processing path.

## Per-sample shape loops

Raw listings are saved as
`analysis/re/dist-magic-shape{0,1,2,3}-stereo-raw.asm`.

- ShapeStereo<0> (0xb0f30) calls TubePair process once for each channel at
  0xb108f and 0xb10a6. Each loop advances both pointers by four bytes and
  decrements its sample counter by one at 0xb11df–0xb11e7.
- ShapeStereo<1> (0xb1200) calls AirPressure process once per channel at
  0xb135f/0xb1376, then advances four bytes/decrements one at
  0xb14af–0xb14b7.
- ShapeStereo<2> (0xb14d0) implements Iron's numerical work inline, with atan
  imports in the two channel paths. Its loop advances four bytes/decrements
  one at 0xb1a8b–0xb1a93.
- ShapeStereo<3> (0xb0890) combines multiple numerical stages per input sample;
  its Air/Tube calls and inline Iron calculations do not create additional
  time samples. The final pointer/counter updates remain four bytes/one sample
  at 0xb0f0c–0xb0f14. Resource Dist types 5/6/7 select local shapes 0/1/2,
  so shape 3 is not selected by those front-panel options.

Focus split filters, post filters, gain smoothing, Air's delay state and
repeated nonlinear stages must not be mistaken for sample-rate conversion.
Their stateful behavior still needs preservation in the outer reconstruction.
Only these stereo loops were audited explicitly for this conclusion; no new
claim about every mono/classic distortion implementation follows from them.

## Immediate runtime adapters

The resource creates Dist as `cPlugChain` with `plugChainDist`
(FX_GUIBoot.lua:777), containing gain, bpPeak, distort and magicDistort rows.
No explicit resampler row is present in that declaration.

`Block::cPlugChainBlock::prepare` at 0x3410a0 stores the supplied rate at +0xf8
and forwards the same floating-point argument to scAEPlugChain::ResetAudio,
with batch size **64** (0x3410bf). Its processAudio at 0x341260 handles input
silence/gain and calls scAEPlugChain::Process with sample count **64** at
0x34136e–0x341375. The helper at 0x341450 is used for batch gain scaling; this
investigation does not depend on inferring its internals from its call alone.

scAEPlugChain::ResetAudio (0xa3f30) iterates child engine instances and forwards
the saved sample-rate argument unchanged. scAEPlugChain::Process (0xa4000)
iterates child instances and forwards its original count unchanged to each
virtual Process call. The separate-engine stereo branch may invoke left/right
engines individually, each on the same count; this is channel splitting. The
shared-instance branch uses both channel pointers with that same count.
Neither branch expands or contracts the time axis.

Raw paths:
`dist-block-prepare-raw.asm`, `dist-block-process-raw.asm`,
`dist-chain-reset-raw.asm`, `dist-chain-process-raw.asm` under `analysis/re/`.

## Wider-symbol search and limits

A complete retained symbol-name search for oversampling, upsampling,
downsampling, resampling, halfband, polyphase and rate-conversion names found
no identified oversampler attached to the Magic Distortion path. Explicit
audio conversion names do exist in the separate PSPVerbPlugIn engine:
inputDownsample2x/outputUpsample2x at 0x188a90/0x188c20, and corresponding 4x
functions at 0x188dd0/0x188f80. Other matches include image/JPEG resampling and
cResamplingAudioSupplier file loading. Their presence elsewhere does not show
a call from Dist. Absence of a matching symbol name would also not exclude an
unnamed/inlined implementation; the local control-flow audit is stronger
support for the bounded conclusion above.

The provenance of the sample rate supplied by the outer plugin/host/container
was not traced end-to-end here. An external host could itself render at a
higher rate. Dynamic engine factory registration, the complete classic
Crunch/Heavy/Zap path and every other plugin subsystem were not exhaustively
audited. No product-wide statement about oversampling is warranted.

For current status wording, the supported pending work is **outer filters,
original gain/mix routing and control mappings/scheduling**. An unconditional
“oversampling pending” label should be removed for the inspected Magic path
unless additional positive evidence establishes such a layer.
