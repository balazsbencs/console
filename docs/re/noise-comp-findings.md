# Noise sidechain compressor

`noise_comp.c` reconstructs the numerical stereo sidechain configuration of `Block::cSimpleCompBlock`. The packaged Noise graph connects the texture output to `SComp.in` and the original input to `SComp.insidechain` (`FX_GUIBoot.lua`, `getNoiseFXSpec`, lines 736–758). This study processes a supplied texture signal.

Reference: RC-20 1.0.5 x86_64 Mach-O SHA-256 `f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.

## Evidence and numerical layout

Original functions are constructor `0x3651f0`, prepare `0x3657a0`, processAudio `0x3659b0`, and empty objectChanged `0x3660a0`. Raw corroboration is retained in `analysis/re/noise-comp-raw.asm` and `noise-comp-filter-raw.asm`. The previously retained REA filter setup result `eq-filter-setup.json` identifies `Filters::BiQuadFilter::Params::Setup` at `0x1a4ff0`, evidence `ev_c8d4ac9ab936129469fadd1ecaa5d35cb96581bd2415cbedb61b63be33515152`. Retained compressor results are:

- `comp-process.json`: `ev_404c0d525f18272a26ebb13021d2405066b793cc008f900daf61699ed6b137c6`.
- `comp-prepare.json`: `ev_d8fa01453fd9a5dd60e5c5ac0cb76158f5e4590d46a4e7192d0996651dbeeb76`.
- `comp-ctor.json`: `ev_28d854d7edacfc12faf461fec36fdd422c224b7bcba6c3d098d944f5b6ce540c`.
- `comp-change.json`: `ev_bc672bd922fa80f8e6e7d8055db3a0703014881ba3e750cb67c093c79493f6ac`.

The eight plain model wrappers, spaced by 0x38, start at 0x108: Enable, Threshold, Ratio, Attack, Release, Averaging, GainComp, HiPassSidechain. Their normalized values are cached at wrapper+0x30. Sidechain configuration sets object+0xfc to 1. Detector and filter numeric state occupies 0x338..0x397; the C state preserves this 96-byte numerical layout.

## DSP behavior

The stereo sidechain can pass through a second-order 80 Hz highpass with Q=.707. Setup uses the original normalized-frequency clamp at .49, single-precision sine/cosine, and the original coefficient division order. The highpass retains two input and two output histories per channel and rounds each output through `(y+1e-4)-1e-4`. Its histories advance even while compressor Enable is 0; bypassing HiPassSidechain freezes those filter histories.

Peak detection uses `max(abs(left),abs(right))`. Nonzero Averaging selects RMS: square both channels, add them, and smooth their sum with the pole `float(exp(double(-1/(sample_rate*.005f))))`, followed by `sqrtf`. The RMS detector uses the sum of the two channel powers. A 1e-25 offset protects the power, amplitude and envelope paths at their original positions.

Threshold maps to `-48*normalized_threshold` dB. Ratio maps to `expf(normalized_ratio*2.079441547393799f)`, giving 1..approximately 8. The amount above threshold is floored at zero. The attack time is `(.6847460865974426*Attack+.022360680624842644)^2` seconds; the release time is `(1.314213514328003*Release+.1)^2`. Their envelope poles use single-precision division followed by double `exp`, then float conversion. Increasing detector level selects attack; otherwise it selects release.

The smoothed level above threshold multiplies `(1/ratio)-1`. If GainComp is positive, an additional offset is `(.25*threshold_db)*((1/ratio)-1)`. Double `pow(10,db/20)` converted to float produces one gain applied equally to the two texture channels. The original 16-batch meter counter is preserved; model meter publication is omitted.

Disabled compression returns the texture unchanged and resets envelope and RMS power to 1e-25. The highpass can continue advancing during these disabled intervals. Input activity is held at 1 in this API and native fixture; original inactive-main-input behavior is not claimed.

## C API and defaults

Call `rs_noise_comp_set` with `rs_noise_comp_defaults`, then `rs_noise_comp_prepare`. The public stack state owns all memory. `rs_noise_comp_process` accepts stereo texture buffers, independent const stereo sidechain buffers and an arbitrary frame count. A NULL sidechain channel means zeros. Controls latch every 64 frames, retaining original numerical batch cadence with no added latency. Prepare supports 1000..1536000 Hz. Reset reproduces a fresh numerical prepare with current parameters and performs no allocation.

The isolated study uses the Noise resource settings and enables compression, with resource values Threshold 0, Ratio .5, Attack 0, Release .2, Averaging 0, GainComp 0 and HiPassSidechain 1. The original resource graph defaults SComp_Enable to 0 and the front panel macro toggles it together with Noise. All C parameters are finite normalized 0..1; Enable is restricted to 0/1. These bounds belong to the C study API.

## Validation

`native_noise_comp_probe.py` constructs only synthetic model caches, a sidechain source record, stereo buffers and numeric state. It executes the isolated original prepare/process functions and binds math imports to Linux libm. No constructor or complete plugin host executes.

`compare_native_noise_comp.py` passes 126 scenarios per compiler, comparing 2,064,384 output sample values and 1,548,288 native state bytes with zero differing bits on GCC and Clang. It covers nine rates from 1000 through 1536000 Hz, including 44117 and 48001, normalized endpoints, peak/RMS detection, gain compensation, highpass transitions, disabled intervals, dynamic attack/release/ratio/threshold changes, reset, silence and sidechain amplitudes up to 100. NULL sidechains are checked against original zero buffers. Native 64-frame calls match C calls partitioned as 64 or `[1,7,13,3,29,11]`.

Reports: `analysis/re/native-noise-comp-gcc-comparison.json` and `native-noise-comp-clang-comparison.json`. The claim is limited to the sidechain numerical compressor, active input batches, omitted meter publication, explicit synthetic buffers and shared Linux libm. It does not establish full Noise graph scheduling, texture playback, original inactive-input shortcuts or cross-platform libm bit identity.
