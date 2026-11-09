# Shared NoiseGen primitive port

`src/dsp/noisegen.c` implements the three isolated functions at 0x1a5980
(Reset), 0x1a59b0 (SetParams), and 0x1a5c80 (GetValue). Evidence is retained in
`analysis/re/noisegen-{reset,params,value}.json`; params/value assembly resolves
incorrect decompiler call signatures. The primitive port reused Evidence from the active parent REA session.

The actual signatures are:

```c
void NoiseGen_Reset(cNoiseGenData *, int32_t seed);
void NoiseGen_SetParams(cNoiseGenData *, float gain, float chaos,
                        float smooth, float decay, bool bipolar, float rate);
float NoiseGen_GetValue(cNoiseGenData *);
```

GetValue returns a float in XMM0, despite Ghidra's uint pseudocode return type.
SetParams uses SysV-compatible RDI for state, ESI for bool and XMM0–4 for its
five floats. The C API uses an equivalent 40-byte `rs_noisegen` layout:

| Offset | Value |
| --- | --- |
| 0x00 | decay envelope magnitude |
| 0x04 / 0x08 | two low-pass history samples |
| 0x0c / 0x10 | two uint32 RNG words |
| 0x14 | bipolar byte; three reserved bytes |
| 0x18 / 0x1c | decay and smoothing coefficients |
| 0x20 / 0x24 | distribution shaping and corrected output gain |

Reset clears the three history floats, seeds x=0x67452301 and
`y=(uint32_t)seed*0x401 ^ 0x90325477`, and preserves parameters, mode and padding.
Each GetValue updates `x ^= y; y += x` with uint32 wraparound, then interprets y
as int32 before float conversion. No signed overflow is used in the port.

Assembly and original image constants recover the omitted double pow exponents:
8.0 for shape gain, −1.7000000476837158 for level compensation, 3.0 for decay
compensation, and 0.05000000074505806 for bipolar smoothing compensation. The
float powf exponent is 3.0. Float arithmetic and double pow conversion points
are preserved; compile without fast math or FP contraction.

`tools/compare_native_noisegen.py` reuses the existing hash-pinned native image
loader and binds the additional double-pow stub at 0x578c96 to system libm. It
compares Reset state, all SetParams state bytes, every GetValue output bit and
all evolving state bytes. The grid includes both polarities, signed seeds,
44.1/48/96 kHz, boundary/interior chaos, smoothing and decay settings, zero and
nonzero gain, followed by reset preserving coefficients. Another 256 seeded
random cases include continuous parameters and 192 kHz.

Verified optimized build: 2,176 cases, 6,528 parameter updates, 417,792 returned
samples; maximum absolute error 0, all state bytes equal. Results are saved in
`analysis/re/native-noisegen-comparison.json`. Reproduce from repository root:

```sh
cmake --build build-release -j2
python3 tools/compare_native_noisegen.py --library build-release/libretro_dsp.so
```

The CMake DSP library includes noisegen.c and links libm; Digital 0.3.0 also uses the generators in the CLAP, with explicit study seeds. Zero-initialize each state once, call reset with the original caller's seed,
then set parameters before obtaining values. Keep one state per original sync
noise generator. This API's sample-rate argument is the caller-supplied update
rate: the primitive divides its shaping coefficients by that number. Do not
assume the full audio sample rate when integrating a control-rate Flux caller;
its call site must establish the rate and update cadence. The Digital engine
call site is now traced: sample_rate/64 once per 64-frame render batch, as
documented in [flux-integration.md](flux-integration.md).

Limits: this comparison uses synthetic standalone state and Linux system libm
for both native and C functions. It establishes primitive equivalence, not the
original host's math-library rounding, Flux macro evaluation order, caller seeds,
control interval or complete-plugin audio equivalence. Out-of-range controls,
nonfinite inputs and invalid sample rates are outside the comparison.
