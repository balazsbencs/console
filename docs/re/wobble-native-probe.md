# Isolated Wobble native probe

`tools/native_wobble_probe.py` supplies `NativeWobble`, an isolated Linux x86_64
probe for the hash-pinned original process function at 0x350c20. It reuses the
existing image loader but replaces the synthetic object with Wobble state. It
never executes original plugin entry points, constructors, prepare allocation
code or resource scripts.

Manually supplied state includes cached physical/normalized parameter wrappers,
two zeroed power-of-two delay rings, four waveform pointers to 0x135330,
reciprocal sample-rate values, reset oscillator phases, six smoother cells,
two auxiliary 64-float LFO buffers and a dummy model setter. Stack guard,
memcpy and double pow imports are bound explicitly. Visualization accumulation
is disabled, with its sample interval set to INT_MAX; dummy counter setter
calls still execute. Meter state is not intended to reproduce the original GUI.

The class accepts sample_rate, depth, depth2, rate, rate2, mix, stereo_phase,
noise, noise_scaler, enabled and normalized. `set()` updates cached parameters;
`process()` handles exactly 64 stereo frames using the inherited batch adapter.
With normalized=True, the four metadata-scaled depth/rate parameters use the
original isolated Scale_Fit3 helper for physical cache conversion. `reset()`
executes the original resetLfoPhase and resetSmoothing functions. It intentionally
does not clear delay rings because these native routines do not clear them.

Smoke verification: six scenarios across 44.1/48/96 kHz and stereo phases 0/.4,
32 blocks each, with depth/rate/mix updates and phase/smoother resets. All 24,576
output samples were finite. This establishes a callable synthetic kernel, not
C equivalence, constructor/prepare equivalence or full-plugin behavior.

```sh
python3 tools/native_wobble_probe.py
```

Manual initialization is based on the saved constructor/prepare/reset raw
assembly described in wobble-control-findings.md. Missing constructor-side
model notifications and the deliberately excluded visualization state are
explicit experimental boundaries. Compare the C port using identical initial
states before making production behavior claims.
