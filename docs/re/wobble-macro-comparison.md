# Settled Wobble macro and Flux comparison

`tools/compare_native_wobble_macro.py` compares the reconstructed macro helper
and Wobble C kernel against isolated original RC-20 1.0.5 x86_64 functions.
It uses the hash-pinned reference
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
The original plugin, constructors and extracted resource scripts are not run.

## Independent reference path

The test reconstructs two synthetic relation records connected to a fake model
store. It executes original `objectChanged` (`0xf0d70`) for multiply, curved
triple, triangle, modulation and from/to mapping; original relation `Drive`
(`0xd6fd0`) updates the original two noise generators. Original reset
(`0x1a5980`) supplies explicit seeds. The original Wobble audio entry
(`0x350c20`) runs through `native_wobble_probe.py`, which reconstructs isolated
prepare state and disables visualization accumulation.

The reference path does not call the ported macro, noise or relation helpers.
Both paths do bind system libm imports, as in the existing native probes.

On every 64-frame tick, the test checks:

- Both generator outputs, including Drive's factor of 16, by float bit pattern.
- All 40 bytes of each generator state.
- Normalized wow depth, flutter depth, wow perturbation, perturbation scaler,
  and stereo phase against original relation outputs by float bit pattern.
- Preservation of direct wow/flutter rate, mix and enable fields by the macro helper.
- Stereo audio output from the C kernel against the isolated original kernel.

The formulas and resource evidence are documented in
[wobble-control-findings.md](wobble-control-findings.md). In particular, wow
noise has a separate group-Flux × unscaled-wow-weight scaler, while flutter's
style 0.95 modulation can only reduce its scaled baseline. These are distinct
paths rather than a common additive depth modulation.

## Coverage and reproduction

The default suite uses 44.1, 48 and 96 kHz, with seed pairs `(0, 1)` and
`(-1, 2147483647)`. Eight static scenarios run 1,024 ticks each and cover zero
Flux, zero Amount, zero Magnitude, both balance endpoints, full controls,
partial controls, and a small nonzero Amount. The ninth scenario runs 4,096
ticks of changing controls with 31-tick plateaus and repeated boundary values.
The stereo toggle takes both values. Inputs combine distinct stereo sinusoids
and periodic opposite-polarity impulses. Full-control scenarios additionally
require each noise stream to attain positive and negative values.

```sh
python3 tools/compare_native_wobble_macro.py \
  --library build-release/libretro_dsp.so \
  --report analysis/re/native-wobble-macro-comparison.json
```

`--static-ticks` and `--dynamic-ticks` permit smaller diagnostic runs. The
library path can point to another build; native execution requires Linux x86_64
and the retained reference artifact. This tool is not part of portable CTest.

## Recorded result

The default suite passed all **54 scenarios**, **73,728 ticks**, **147,456 noise
outputs**, **5,898,240 generator-state bytes**, **368,640 target comparisons**,
and **9,437,184 audio samples** with zero control or audio error. Generator
states and target float patterns match exactly. The machine-readable report is
`analysis/re/native-wobble-macro-comparison.json` (local analysis artifact).

## Limits

The test manually settles all coefficient notifications and inputs before each
Drive call. It therefore verifies the settled graph and composite audio, not
the production model scheduler, startup notification ordering, or automation
between original notification passes. The two seeds are explicit study inputs;
actual serialized production relation indices remain unknown. Native prepare
state is reconstructed rather than obtained by invoking plugin initialization.
The composite comparison fixes direct rates to their original defaults, mix to
one, and enable to one; the separate kernel comparison covers their wider
ranges and transitions.
