# Iron transformer numerical core

The C port in `src/dsp/iron_transformer.c` reconstructs the isolated Iron core
used by `cAEMagicDistortion` mode 2. It does not implement the surrounding
focus splitter, dry/wet and band gains, DC follower, oversampling, Distortion
module macros or complete RC-20 chain.

## Evidence

Reference: supplied RC-20 1.0.5 x86_64 Mach-O, SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.

- REA `ironTransformer::setup(float,float)`, `0xb1f60`, evidence
  `ev_5f04693a5bbe31d485d32d38af88018df4a9ddc7c5d5adf1decc618b4061e2fc`:
  coefficient and target formulas. Raw SSE instructions resolve intermediate
  float/double precision where pseudocode obscures it.
- REA `cAEMagicDistortion::CalcInternals()`, evidence
  `ev_f500e9c6180c0966d1317252b148a4fadfe7fb00c00ed56bd0673f5ecd398baa`:
  mode 2 uses the inner object at outer object offset `+8`; normalized amount
  is its raw percent parameter multiplied by `0.01`.
- Native `ShapeMono2` at `0xb05d0`, retained local disassembly
  `analysis/re/iron-mono-raw.asm`: inlined numerical process and state offsets.
- Mach-O indirect symbols resolve `0x57895a` to **atan**, `0x578a2c` to exp,
  `0x578c96` to pow and `0x578c9c` to powf. No tanh is used by this core.

Static evidence establishes the formulas and call relationships. The probe
below separately establishes numerical agreement for the tested synthetic
states; it does not run a macOS host or establish whole-plugin equivalence.

## State and processing

The inner object occupies `0xa8` bytes: 42 floats. Its first four floats hold
stereo integrated input and previous atan output. Target pregain, postgain and
feedback are at `+0x10`, `+0x14` and `+0x18`. Five seven-float filters begin at
`+0x1c`, `+0x38`, `+0x54`, `+0x70`, and `+0x8c`. Each contains three shared
coefficients followed by two current-output histories and two previous-input
histories, indexed by channel.

For each sample the core smooths pregain and feedback, computes
`integrated = (pregain * input + previous_integrated) * feedback`, then casts
`atan((double)integrated)` back to float. The difference from previous atan
output is multiplied by smoothed postgain and passed through two filters.
The C operation order follows the native scalar SSE instructions, with fused
multiply/add disabled.

`setup` computes coefficients and targets while preserving audio histories.
The C API requires initially zeroed storage and accepts normalized amount
0–1 and sample rates 1,000–1,536,000 Hz. Its `reset` explicitly clears all audio
histories and preserves coefficients/targets. This is the reconstruction's
reset contract; native full-object reset/startup timing is outside this core's
scope.

## Native comparison

`tools/compare_native_iron_transformer.py` maps the hash-checked reference and
binds the four math imports to Linux libm. It executes only original numerical
setup and mono processing with a synthetic zero-initialized object; no plugin
entry points, constructors or resource scripts execute.

To expose just the inner numerical result, the outer splitter is configured
as unity and its middle-band history is set to zero before each sample. This
last step is necessary: even unity splitter coefficients otherwise perform
`(input - previous_mid) + previous_mid`, which can round the input. The probe
reads the final inner filter history before outer DC and dry/wet processing.

GCC and Clang 19 each passed 43 scenarios and 352,256 samples with **zero
sample error**. All 42 inner state floats match bit for bit in channel-zero
cases, including setup changes and history resets. An independent mono native
reference also checks channel-one output. Rates span 1,000, 8,000, 44,100,
48,000, 96,000, 192,000 and 1,536,000 Hz; amounts span 0, 0.001, 0.125, 0.5,
0.875 and 1. Inputs include sine, random bipolar values and silence tails.

Reports remain local in `analysis/re/native-iron-transformer-comparison.json`
and `analysis/re/native-iron-transformer-comparison-clang.json`. These tests do
not establish agreement with Apple's libm, original startup histories, outer
filtering or oversampling.
