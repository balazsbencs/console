# Classic Distortion band splitter

The `cAESingleBandDistortion` helpers `SplitStereo4` (`0xa8fc0`) and
`SplitMono4` (`0xa8de0`) split four consecutive frames into low, middle and
high bands. The helper contains two sequential first-order recurrences with
explicit float rounding operations. Waveshaping happens outside this helper.

Reference: supplied RC-20 1.0.5 x86_64 Mach-O, SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
Raw disassembly is retained locally in `analysis/re/classic-split-raw.asm`.
REA `SplitStereo4` evidence is
`ev_0f0e9214272c5012e798373ee8e9f8713f51e3835b8144aedecab49e54bcf6e4`;
`SplitMono4` evidence is
`ev_5a6d766e3280d5ccdfb1ad7815098bd579006da9c78475538f5c3efe84aedeba`.
The returned pseudocode corroborates the raw instruction/state mapping below.

## State and operation order

Only six floats in the native object are read or written by the stereo helper:

| Offset | Meaning |
| --- | --- |
| `+0x48` | Low-band coefficient |
| `+0x4c` | Middle-band coefficient |
| `+0x50` | Left low-band history |
| `+0x54` | Left middle-band history |
| `+0x58` | Right low-band history |
| `+0x5c` | Right middle-band history |

These map directly to the 24-byte `rs_classic_split_state`. For each input
sample, compute the low-band recurrence from the input minus low history,
subtract low from input, then compute the middle recurrence from that
remainder minus middle history. High is the remainder minus middle.

Each recurrence first adds the exact float `9.999999747378752e-05f` to the
history difference, stores/reloads that float, then adds its negative. It
multiplies by the corresponding coefficient and adds the preceding history.
The apparently cancelling additions affect rounding and are retained using a
volatile intermediate. Builds must retain float operation order and disable
fused multiply/add, as the project already does.

Although the native entry point accepts four-float vector references, the
machine code unrolls ordinary sequential recurrences. It does not introduce a
cross-frame approximation or independent filter lanes. The stereo helper
interleaves left/right work; the channels share coefficients and have separate
histories, so the C helper can process each channel in order. Only the final
histories are stored back by the native four-frame helper. The mono helper
updates the left histories and leaves right histories unchanged.

## C interface

`rs_classic_split_sample` processes one stereo frame without buffering or
latency. `rs_classic_split` returns all three bands for four stereo frames;
`rs_classic_split_mono` implements the original mono helper. Coefficient
calculation and full distortion processing remain separate responsibilities.

The C helpers use ordinary float pointers and do not require SIMD alignment.
The original functions use aligned vector loads/stores, so the isolated
comparison fixture supplies 16-byte-aligned buffers and the original nine
pointer arguments for the stereo entry point.

## Numerical validation

`tools/compare_native_classic_split.py` maps the hash-checked original image and
calls only the two numerical split helpers with explicitly initialized
coefficients and histories. No constructor, initializer, resource script,
plugin entry point or host runs.

GCC and Clang 19 each passed 32 scenarios, 131,072 four-frame groups,
2,359,296 band-output values and 786,432 state-float comparisons with zero
error and identical float bit patterns. This covers mono/stereo, zero and
nonzero initial histories, coefficient endpoints 0 and 1, near-endpoint and
interior coefficients, dynamic coefficient changes, sine/random inputs,
signed zeros, tiny values, large bipolar impulses and silence tails. Stereo
cases alternate the four-frame C function with four individual sample calls,
proving the zero-latency interface against the original helper too.

Local reports:
`analysis/re/native-classic-split-comparison.json` and
`analysis/re/native-classic-split-comparison-clang.json`. These observations
establish the splitter for finite coefficients in 0–1; they do not establish
original coefficient setup, nonlinear shapes, gain stages, the outer mix or
the complete Distortion module.
