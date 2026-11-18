# Space numerical DSP

`src/dsp/space.c` reconstructs the stereo numerical `cResoXBlock` kernel. It uses the shared numerical layout in `space_internal.h` and the independently verified allocation/setup implementation in `space_prepare.c`. The C implementation executes no native code. `tools/native_space_probe.py` executes only isolated original numerical helpers against synthetic state and zero-filled allocations for comparison.

The reference is RC-20 1.0.5 x86_64 Mach-O SHA-256 `f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.

## Evidence

Retained REA process result `analysis/re/space-process.json` identifies `Block::cResoXBlock::processAudio` at `0x35c900` (evidence `ev_84b005b5fde6f6a611aa3ecb6fb18c337418b16eff4242da87649ecd7778e3bb`). Prepare at `0x35be80` is in `space-prepare.json` (`ev_6d71bd0dc396852169dbaee30b8ec86643d0ed30ba94e6d5123847e72bf2e51e`). Clear at `0x361840` is in `space-clear.json` (`ev_f8d43fc17ecbeda44d6b288b1232033114245be6258a4b0928bc815a529fb5da`). Other retained results cover constructor, resetSmoothing, changed, allpass Prepare/Reset and delay SetLength. Raw instruction listings corroborate expression order where vector decompilation reassociates floating point operations.

## Recovered signal path

The two predelay rings receive either a mono average or dual-mono crossfeed (`.75*own + .25*other`). A four-tap cubic interpolator reads a smoothed predelay. Optional polynomial sine modulation adds a unipolar offset; its amplitude interpolates over 64 frames. Noise is an external physical control, and changes the oscillator increment at sine sign crossings. A zero modulation amount freezes oscillator phase.

Each active wet bank contains 12 parallel negative-feedback comb resonators. StereoMode selects one shared bank or two independent banks. Each resonator filters its tap through a highpass blend (`.6*tap + .4*highpass`) and a lowpass blend (`.7*blend + .3*lowpass`) before multiplying feedback and subtracting it from the predelayed input. Four optional serial allpasses follow the summed resonator output. Enabling both allpasses and ReverbMode scales the resonator input by `.15`.

Pitch lengths are computed from MIDI notes 23..34, with opposite signed detune for the two banks when StereoMode and ReverbMode both equal1. Setup reserves capacities using notes 22..33. Detune/mode changes reset delay lengths and clear their active storage. Decay feedback is `powf(10, (((decay-1)*48)/(sample_rate/period_samples))/20)`.

Level maps through `v=(level*.15)/((level*-.7+1)-.15)`, then dry=`cos(v*pi/2)` and wet=`sin(v*pi/2)*.2`. Changes interpolate gains using fractions 1/64 through 64/64. Disable returns dry and clears delay storage on the disable transition; Level0 also clears delay storage. Filter histories and ring positions follow the original clear behavior. The parameter named EnableFeedBacks controls visualization collection/publication; it does not switch off the audible resonator feedback loop.

## API and timing

`retro_space_set` accepts 14 physical controls; frequencies use Hz and predelay uses ms. Changes latch on an internal 64 frame cadence. Processing accepts arbitrary buffer sizes without added latency, and retains original fixed-batch control smoothing and mix interpolation. Prepare supports 1000..1536000 Hz and owns allocation. Process and reset allocate no memory.

`retro_space_reset_smoothing` reproduces the original smoothing reset, including a pending predelay control. Public `retro_space_reset` provides a deterministic full study rewind: it restores the numerical fresh-prepare state with current controls while reusing all allocated storage. This deliberately has stronger semantics than original `clearDelayLines` plus `resetSmoothing`, which preserve ring positions and filter histories.

The numerical prefix mirrors offsets through `0x1160`; it contains no executable C++ object. Pointer slots address C-owned buffers. Float expression ordering, cubic left/right multiplication order and four-lane resonator summation order are retained to reproduce original rounding. The C core omits visualization-only meter histories and model publication.

## Validation and scope

`tools/compare_native_space.py` compares original 64 frame calls against C calls partitioned as 64 or `[1,7,13,3,29,11]`. It checks stereo samples, audible smoothing/filter state, every active delay buffer, capacities, lengths and relative ring positions. Scenarios include all 14 controls, disabled and zero-level states, endpoint decay/frequencies/delay, dual mono, stereo detune, mode/allpass transitions, wobble, dynamic parameter changes, pending-control smoothing reset, and full reset against freshly prepared synthetic native state. The 1536000 Hz default scenario runs 65536 frames; the 384000 Hz default scenario runs 32768 frames to reach the longest comb/allpass returns.

Reports are `analysis/re/native-space-gcc-comparison.json` and `analysis/re/native-space-clang-comparison.json`. Each compiler passed 110 scenarios, 1,966,080 stereo sample values and 21,835,421,840 compared state bytes with zero differing bits. Both original and C resolve scalar math through Linux libm; this is not evidence of cross-platform libm identity. The original active-input marker is held at 1, so the original silence-tail shortcut is outside this comparison. No original constructor, full plugin host, script graph, GUI routing or meters are included in the fidelity claim.
