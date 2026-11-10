# Chain gain stage

The Distortion chain's literal `gain` selects `cAEGainPlug`, whose two physical
parameters are `gain` (linear multiplier) and `db`. It is a different factory
entry from `xdgain`.

Reference: supplied RC-20 1.0.5 x86_64 Mach-O, SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.

## Static evidence

- `CreatePlug` (`0xa3510`) reads the names table at VM `0x768810` and dispatches
  through the relative jump table at `0xa3c24`. Entry zero points to the string
  `gain` at `0x6356bc` and branch `0xa3719`, which allocates `0x20` bytes,
  installs the `cAEGainPlug` vtable at `0x7689b8` and calls its shared-pointer
  wrapper `0xb2e80`. Entry one is `xdgain`, branch `0xa3778`, a different type.
  The inline constructor copies four floats `(1, 0, 1, 1)` from `0x5e5310`
  into linear gain, dB, current gain and target gain.
- REA SetParam (`0xa9cc0`), evidence
  `ev_4ad5c1b891f538a62358847d11ce3897ca8cc11a3b5731d17f5aeec140330c4f`:
  `target = expf((db * 0.05f) * 2.3025851f) * linear_gain` with the native
  float operation order.
- REA ResetAudio (`0xa9c10`), evidence
  `ev_5888403a09faf9ec0f3bba56d4db496162c0cd371e1bbff8fa9c04b6555963cd`:
  the sample-rate and frame-count coefficient formulas below, then current
  gain snaps to target. The decompiler misidentifies the method's register
  arguments; the raw instructions and numerical probe establish their use.
- REA Process (`0xa9d30`), evidence
  `ev_87690ad83070daee1c862c87535c7e2d8ae6d4d7d84866ac37e2c6977b9b8db5`,
  forwards to ProcessGain (`0xa9d90`), evidence
  `ev_570de1d1e5f98d0fa8d13ff567a4bf00798fd727fbb6b364ae779d35f887fdd4`.
- `_GLOBAL__sub_I_PlugChain.cpp` at `0xb37e0`, inspected statically, copies
  the 16-byte constant at `0x5e1070` to VM `0x789cc0` (`0xb3808`–`0xb380f`).
  Its float values are `(0, 0.25, 0.5, 0.75)`. The native probe copies these
  verified bytes directly; it never runs a global initializer.

## Numerical behavior and C contract

The sample coefficient is
`1 - expf(min(40 / sample_rate, 0.49999f) * -6.2831855f)`.
A separate coefficient for the native silence shortcut uses
`frames * 10 / sample_rate` in place of `40 / sample_rate`.

At the beginning of Process, target magnitudes below `1e-9f` snap to zero;
values within `1e-9f` of unity snap to one. If the distance from current gain
to that target is below `1e-4f`, current gain snaps to target and the block is
multiplied by a constant. Unity bypass preserves samples, while settled zero
fills buffers with positive zero.

Otherwise, the native SIMD loop advances a smoother once per four samples:
`delta = (target - current) * coefficient`. Four gains are `current` plus
`delta` times `(0, .25, .5, .75)`, then current advances by delta. Both audio
channels share this gain. This differs from an ordinary smoother updated on
every sample. No additional denormal cleanup is applied at the block end.

`rs_gain_process` preserves the original 64-frame Process cadence across any
C call partitions, including splits inside four-sample groups. A new target
latches at the next 64-frame boundary. `rs_gain_reset` snaps current to target
and starts a new boundary, matching the numerical ResetAudio contract. Start
with zeroed storage, set linear gain and dB, then prepare. `prepare` accepts
1,000–1,536,000 Hz; both physical gain inputs accept -100–100, matching the
native reported ranges. The normal chain uses linear gain 1 and dB controls.

The C interface processes actual audio and does not expose the native silence
flags. Native silence shortcuts are disabled in the comparisons; their
coefficient is retained but their shortcut branch is not part of this API.

## Validation

`tools/compare_native_gain.py` executes only original numerical SetParam,
ResetAudio and Process on synthetic state, with verified interpolation bytes
and libm/memset imports. Constructors, global initialization, hosts, resource
scripts and complete plugin entry points are not executed.

GCC and Clang 19 each passed 240 scenarios and 3,932,160 audio samples with
zero sample error. The current/target gains and both coefficients match bit
for bit after every native block. Cases span ten sample rates, mono/stereo,
linear and dB endpoints, tiny targets near the snap threshold, dynamic ±22 dB
and ±18 dB changes, reset, signed-zero inputs and irregular C partitions
`[1, 7, 13, 3, 29, 11]` against original 64-frame calls. Local reports are
`analysis/re/native-gain-comparison.json` and
`analysis/re/native-gain-comparison-clang.json`. Linux libm is bound to both
sides; this does not establish Apple's libm or complete-chain agreement.
