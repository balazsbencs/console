# Distortion chain peak EQ

The `bpPeak` entries surrounding Crunch, Heavy and Zap use native
`cAEBiQuadPlug` type 4. `src/dsp/peak_eq.c` ports its stereo numerical processor,
including coefficient interpolation, zero-gain bypass fades and filter-history
rounding. It processes arbitrary C buffer partitions with no added latency.

Reference: supplied RC-20 1.0.5 x86_64 Mach-O, SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.

## Evidence

| Native method | Address | REA evidence |
| --- | --- | --- |
| UpdatePrivates | `0xad290` | `ev_2dd9d55f7e4fe5e4bf558408afae66f31f787b4835705cc11dea00d17454ab5c` |
| ResetAudio | `0xab970` | `ev_9fc8f67456ca11250f361f57446843476bd5efea3396e667ca5c4ddad71b9689` |
| SoftResetAudio | `0xaba50` | `ev_80aa45d181ca5a9a20a9eba8357b9853a5d074cf5a0efd92065b44aad675a8db` |
| SetParam | `0xabae0` | `ev_b8424cdedfa23e9957ca948cb4dd8903a3ed659b1644fe1076484825682bd258` |
| Process | `0xabb30` | `ev_6492658705f56429db5b1d2d0c6535c8d6eb42a69c2e501b7529ddedfde05c3b` |
| ProcessStereo4 | `0xad8b0` | `ev_6954df7b2867eb27fadff6396437282a19c03b4677a58101c1afa49a43c2c7e0` |

The `CreatePlug` factory's `bpPeak` branch at `0xa3972` allocates 160 bytes,
installs the `cAEBiQuadPlug` vtable and sets type 4 at `+8`. The extracted
`FX_GUIBoot.lua` Distortion chain configures 900 Hz/Q 0.4 for Crunch/Heavy and
850 Hz/Q 0.2 for Zap, with positive EQ before and negative EQ after each
nonlinear processor. Their gain targets vary with Amount.

Native `GetParamRange` at `0xab8b0` reads minimums `(20, .01, -100)` and maximums
`(20000, 10000, 100)` for frequency, Q and dB from `0x5e5c48`/`0x5e5c54`.
Raw assembly corroboration is retained locally as `peak-update-raw.asm`,
`peak-process-head-raw.asm` and `peak-stereo4-raw.asm` in `analysis/re`.

## Numerical behavior

Normalize frequency by sample rate and clamp it to `[4.2e-5, .49]`. Below the
lower bound, native code also moves Q 6% toward 0.707, with a short transition
between normalized frequencies `3.125e-5` and `4.2e-5`.

With angular frequency `w = normalized_frequency * 2π`, compute
`alpha = sinf(w) * .5 / Q` and `A = powf(10, dB * .025)`. Before division by
`a0 = 1 + alpha/A`, coefficients are:

- `a1 = -2*cosf(w)`, `a2 = 1 - alpha/A`.
- `b0 = 1 + alpha*A`, `b1 = a1`, `b2 = 1 - alpha*A`.

The C implementation preserves the native float constants and operation
order, including multiplication by the reciprocal of `a0`. Each channel has
separate two-input/two-output histories. Original `ProcessStereo4` unrolls four
sequential samples and rounds only the final two output histories using
explicit `+1e-4` then `-1e-4` float operations. Output samples themselves remain
unrounded. At the end of each native Process call, history magnitudes below
`1e-9f` become positive zero.

Changed controls start coefficient interpolation lasting
`int(powf(2*normalized_frequency, .1771f) * int(sample_rate*.075f))` frames.
The interpolation advances once before each original 64-frame Process call.
A dB magnitude below `1e-4f` selects bypass. Entering/leaving it crossfades over
`floor(int(sample_rate*.01f)/64)*64` frames; wet weights advance in four-frame
groups inside each call. The port preserves both the 64-frame and four-frame
cadences when C calls split either boundary.

## API and validation

Zero initialize `rs_peak_eq`, set its physical controls, then prepare it.
`rs_peak_eq_set(s, frequency, q, db)` atomically updates changed controls;
`rs_peak_eq_prepare(s, rate)` matches numerical ResetAudio;
`rs_peak_eq_reset(s)` matches SoftResetAudio. Process accepts distinct in-place
stereo buffers. Control batches are expected at 64-frame boundaries, as used
by the reconstructed Distortion chain. The normal preset stages have fixed
frequency/Q and varying dB, so each batch changes only the gain parameter.

`tools/native_peak_eq_probe.py` executes original numerical setup/reset/process
on a synthetic zero-initialized 160-byte object, with libm and a stack-guard
binding. No constructor, initializer, resource script, plugin entry point or
host runs. The fixture applies final physical control values before calling
original UpdatePrivates once; it does not simulate ordering of separate
parameter notifications.

GCC and Clang 19 each passed 128 scenarios, 4,194,304 audio samples and
5,242,880 native-state byte comparisons with zero error and identical bit
patterns. The comparison checks every audio sample and all 160 native-layout bytes after
setup, control changes and each Process64 call. Cases cover static preset EQs,
zero-gain bypass, both fade directions, interrupted fades, coefficient ramps,
low-frequency correction/high-frequency clamp, Q/dB endpoints, reset, sine and
random input, silence tails and C partitions `[1,7,13,3,29,11]`. Native silence
shortcut flags remain false. GCC/Clang reports are retained locally as
`analysis/re/native-peak-eq-comparison.json` and
`analysis/re/native-peak-eq-comparison-clang.json`.

Linux libm is bound to both sides. These tests do not establish Apple's libm
identity, original constructor/startup, separate-notification timing, the full
model scheduler, silence shortcuts, full Distortion graph or oversampling.
