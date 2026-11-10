# Classic processor native comparison

`tools/native_classic_probe.py` and `tools/compare_native_classic.py` compare the
ported Crunch, Heavy and Zap numerical processor against the original
`cAESingleBandDistortion`. This includes its parameter conversion, coefficient
setup, split filters, nonlinear shape, output filter, band/dry mixing and
64-frame history cleanup. It does not execute the full Distortion graph or
production model scheduler. The fourth native shape is outside this port's
three-character API and this comparison.

The hash-pinned reference is the supplied RC-20 1.0.5 x86_64 Mach-O, SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.

## Evidence and fixture

The fixture creates a zero-filled 128-byte synthetic object and executes only
these original numerical methods:

| Method | Address | REA evidence |
| --- | --- | --- |
| SetParam | `0xa5d80` | `ev_4b0fe17f30cca56b2fdf9f25e6a956735fedbe1e66f6328dbba34142561b1990` |
| ResetAudio | `0xa5d10` | `ev_0dc97f1d1400ee7625c8ed6418495fd9a3689b8811fd8381dee1e6040b4eb028` |
| CalcInternals | `0xa6080` | `ev_6ec6b40a111994f75ce1ee878414776c08e4947fccfe78dedb70d58993f1a38d` |
| Process | `0xa5e30` | `ev_41770ff79f2db0191b01003c11bd4baebb5bc6ca083a64d4ea730945ee27196d` |
| ShapeStereo0 | `0xa7290` | `ev_13d4edb62593bd5d4b487efab4fdbe1dd8c76270b4ee5aeef5ed3a8f0695154f` |
| ShapeStereo1 | `0xa7960` | `ev_65df15f0be9e5292f50bc3692a0929bd3eac851f66c6144f07fe18a583b65fd9` |
| ShapeStereo2 | `0xa8020` | `ev_e280cc14e957a1e1c553ac9c78fa167cfd78d24d9fc26d37c3d796d6342bbd16` |

The normal global initializer at `0xb37e0` sets mask data used by the SIMD
shapes. The fixture copies only the statically verified mask values/constants
into their BSS locations, without executing that initializer: 20 bytes of
all-one bits at `0x789c90`, the sign mask from `0x5e1060` at `0x789cb0`, and
the interpolation vector from `0x5e1070` at `0x789cc0`. No original constructor,
resource script, plugin entry point or audio host runs. Both native silence
flags are false during every comparison.

The native methods receive 16-byte-aligned, distinct stereo buffers in
64-frame calls. C calls alternate between whole 64-frame buffers and
partitions `[1, 7, 13, 3, 29, 11]`. Thus the comparison checks that C preserves
the original end-of-shape cleanup cadence across arbitrary host partitions.
That cleanup rounds the four split-filter histories at the end of a native
shape call; the output-filter histories are rounded on each sample.

In addition to every audio sample, a read-only ctypes view of the private C
struct compares these fields bit for bit after setup and every native block:
all four band/dry/reference gains, both split coefficients and four split
histories, all four shape coefficient slots, output-filter coefficient and
both output histories. Unused shape slots remain part of this check when
switching between characters.

## Results and scope

GCC and Clang 19 each passed 240 scenarios, 1,966,080 audio samples and 265,200
coefficient/state float comparisons with zero error and identical float bit
patterns. Each scenario runs 64 blocks. Coverage includes:

- Crunch, Heavy and Zap, including dynamic character changes.
- Ten rates: 1,000, 8,000, 22,050, 44,100, 44,117, 48,000, 48,001, 96,000,
  192,000 and 1,536,000 Hz.
- Eight initial parameter sets, amount/mix endpoints, reversed focus bounds,
  ±100 dB distorted/pass gain and reference level endpoints, and low output
  cutoff.
- Dynamic amount, mix, gains, reference level and frequency controls, native
  numerical reset, random/sine inputs and silence tails.

Local reports are `analysis/re/native-classic-comparison.json` and
`analysis/re/native-classic-comparison-clang.json`; they retain the complete
parameter/rate grid. Reproduce with:

```sh
python3 tools/compare_native_classic.py --library build-release/libretro_dsp.so
python3 tools/compare_native_classic.py --library build-clang/libretro_dsp.so --report analysis/re/native-classic-comparison-clang.json
```

The Heavy shape uses the x86 SSE reciprocal estimate plus one Newton step.
The x86 C implementation preserves those instructions and their operation
order. The ARM implementation uses the recovered division transfer function;
ARM bit identity is unverified. Linux libm is bound to both reference and C,
so these tests do not establish Apple's libm identity, original startup or
silence-optimization behavior, the full Distortion chain or oversampling.
