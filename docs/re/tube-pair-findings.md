# Isolated TubePair numerical port

The C implementation in `src/dsp/tube_pair.c` reconstructs only the numerical
`cAEMagicDistortion::tubePair` setup/process boundary from RC-20 1.0.5 x86_64.
Reference SHA-256:
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
The raw instruction listings are retained locally at
`analysis/re/tube-setup-raw.asm` and `analysis/re/tube-process-raw.asm`.

| Original function | Address |
| --- | --- |
| tubePair::setup(float sample_rate, float amount) | 0xb1ab0 |
| tubePair::process(float sample, int channel) | 0xb2550 |

No original plugin constructor, host, resource script or complete distortion
engine executes in the comparison. `NativeBitColor` provides only the existing
hash-pinned image loader. Mach-O indirect symbol records, inspected with
`llvm-objdump --macho --indirect-symbols`, identify **0x578a2c as double exp**
and **0x57895a as double atan**. The latter is not an exponential import.

## State and processing

The original numerical state is exactly 148 bytes: five seven-float stereo
first-order filters, followed by two scalar gains. Each filter stores three
coefficients, two channel output histories and two channel input histories.

```c
typedef struct { float state[37]; } rs_tube_pair;
void rs_tube_pair_setup(rs_tube_pair *, float sample_rate, float amount);
void rs_tube_pair_reset(rs_tube_pair *);
float rs_tube_pair_process(rs_tube_pair *, float sample, unsigned channel);
```

Channels are zero/one; controls must be finite, sample rate positive, and
Amount within [0,1]. Setup recalculates coefficients and gains while preserving
histories. Reset explicitly clears each filter's four history floats and
preserves coefficients/gains. This is the study API's state-level reset
contract, not a claim about a separately invoked original TubePair reset
routine. Initial callers should zero the whole state before setup.

Setup derives a drive gain from `exp(3*Amount)*30*.01` and compensation from
`(Amount+1)/drive`, preserving the native mixture of float and double rounding.
Two 50 Hz low-pass filters smooth these gains independently for each channel.
The audio path is drive → asymmetric nonlinearity → compensation → high-pass,
then drive → asymmetric nonlinearity → compensation → high-pass → low-pass.
The nonlinear operation preserves native branching:

```text
positive x:    -x
nonpositive x: float32(atan(float64(-x)))
```

The first high-pass cutoff is 25 Hz; the second is `Amount*50+7` Hz. The final
low-pass cutoff is `Amount*(-5000)+9000` Hz. Cutoffs are limited to
`sample_rate*float32(.499)`. The high-pass updates add and subtract float32
`.0001` with an intermediate float rounding; this must not be algebraically
removed. All filter addition orders, setup float casts, exponent constants and
double calls are retained.

## Native comparison

`tools/compare_native_tube_pair.py` binds only the original isolated setup and
per-sample process functions and their double libm imports. It checks every
coefficient/history word and each output by float bit pattern. Coverage:

- Eight sample rates: 1k, 22.05k, 44.1k, 48k, 96k, 192k, 384k and 1.536 MHz.
- Nine static Amounts, an additional changing-Amount scenario at every rate,
  and a dense 1,001-value setup grid.
- Zero and signed-zero input, impulses, DC, distinct stereo sinusoids, random
  input, ±100 overloads and very small signals.
- Repeated setup with populated histories and explicit history-only resets.

GCC and Clang 19 each pass **80 scenarios, 8,432 setups, 88 resets, 425,984
samples and 221,512 complete state comparisons**, with **zero error** and
identical coefficient, history and output patterns. Output digest:
`57b320514215d2d45d078fc48ce05c5fee0fc574ac5e127b5c5a8ed178743d6c`.

```sh
python3 tools/compare_native_tube_pair.py \
  --library build-release/libretro_dsp.so \
  --report analysis/re/native-tube-pair-comparison.json
```

The test requires Linux x86_64 and the retained reference. System libm is used
for the original imports and the C port. This comparison establishes the
isolated numerical boundary; it does not establish equivalence of the RC-20
Dist effect, its focus filtering, external -12/+12 dB attenuation, Amount's
additional ±22 dB gains, outer Mix routing, Flux, parameter smoothing or
startup/model scheduling. Those surrounding paths remain separate milestones.
