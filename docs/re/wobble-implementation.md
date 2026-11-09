# Wobble kernel and CLAP, 0.2.0

`src/dsp/wobble.c` ports the stereo `cWobblerBlock::processAudio` path from the
supplied RC-20 1.0.5 x86_64 reference at 0x350c20. It is exposed independently
as `RetroStudyWobble.clap`, with a host generic parameter editor. The complete
RC-20 graph remains pending. Version 0.2.0 adds the recovered settled Wobble
macro/Flux graph; see [macro integration](wobble-macro-integration.md) for its
controls, comparisons and remaining seed/timing gaps.

## Recovered processing

Two `cLFO::Sine` oscillators per stereo channel drive wow and flutter. Their
native polynomial at 0x135330 is reproduced with its original float operation
order. Wow has stereo phase control; flutter starts with equal phases in both
channels. Oscillators start at phase .75 and evaluate before phase advances.

Depth and rate metadata use the recovered Fit3 conversion:

| Input | Low / middle / high physical values |
| --- | --- |
| Wow Depth | 0 / .4 / .8 |
| Flutter Depth | 0 / .1 / .2 |
| Wow Rate | .1 / 1 / 4 Hz |
| Flutter Rate | 6 / 10 / 20 Hz |

Six control smoothers operate at 64-frame boundaries using a fixed-point scale
of 16384, a small snap threshold and bounded steps. Mix, external wow noise and
its bias interpolate over each batch. The native 3 dB mix compensation and
enable transition fades are retained. The kernel accepts an external bipolar
noise value and its scale; it does not generate that Flux stream itself.

The stereo delay requests `2 * trunc(sample_rate * .02f) + 1` samples, with each
ring rounded up to a power of two. The delay target has a 10 Hz smoothing pole.
Readback uses linear interpolation below one sample and during disable fades;
otherwise it uses the native four-point cubic. The rings advance before input
is written, then the interpolated delayed signal mixes with dry input.

`reset_phase` reproduces the native phase reset: oscillator phases and delay
target filter histories reset; delay audio remains. `reset_smoothing` marks
the six smoothers for immediate target initialization when the engine has
started. The study's full `reset` additionally clears rings, positions, fades,
control histories and partial-batch progress. That full host reset is a study
contract, not a claimed original graph lifecycle reconstruction.

## API and controls

The opaque C API has create/destroy, set, prepare, reset, reset_phase,
reset_smoothing and process calls. Allocation occurs during create/prepare and
destroy; process allocates nothing. Set/prepare failures leave existing usable
state intact. A caller serializes operations for each instance. Stereo buffers
may process in place; partial buffers preserve the 64-frame control clock.
Parameter changes restart the adapter's control interval at the next sample.

The ten direct CLAP controls expose normalized Wow/Flutter Depth and Rate, Stereo Phase [0,.4],
Mix [0,1], LFO Noise [-1,1], Noise Scale [0,1], Enable and Bypass. IDs are
2000–2009. RSWB state version 2 stores those plus eight macro controls, and
loads old version 1 states with new controls at defaults. Defaults deliberately
enable an audible study effect; they are not an RC-20 factory preset.

Enable follows the kernel's ten-batch disable fade and subsequent dry return.
Bypass copies dry audio and freezes the study engine. No host compensation
latency is reported: the changing delay is the effect. This does not establish
the original full plugin's latency policy.

## Evidence and validation

REA results are retained in `analysis/re/wobble-{process,prepare,constructor,
reset-phase,reset-smoothing,sine,param-change}.json`, plus raw assembly.
The session is exported as `wobble-evidence.json` and `wobble.rea` and closed.
`docs/re/wobble-validation.json` records source hashes and Evidence IDs.

`tools/native_wobble_probe.py` maps the same hash-pinned original slice used
for Digital. It manually reconstructs stereo delay buffers, smoothers,
oscillators and cached parameter values. Visualization accumulation is disabled
and model setter callbacks are dummy bindings. It executes no plugin
constructor, original prepare allocation, resource script or host entry point.
Native rate cache invalidation and enable countdowns are supplied explicitly.

`tools/compare_native_wobble.py` compares 46 cases, totaling **3,670,016 audio
samples**, with maximum absolute error 0 and no unequal samples. Cases cover
44.1/48/96 kHz, depth/rate extremes, stereo phase, mix, external noise input,
zero audio, impulses, changing controls, disable/re-enable and native phase /
smoothing resets. A long low-rate case exercises oscillator wraps. Both GCC
and Clang builds are checked locally; native reference comparisons use Linux
libm and do not establish macOS libm or ARM rounding equivalence.

Host tests cover ABI, short state streams and atomic rejection, nondefault
controls, sample-offset/grouped automation, repeated activation, full reset
after warmup, in-place stereo and irregular buffers versus the standalone C
engine at five sample rates. Release and ASan/UBSan pass. CLAP 0.2.0 validation passes 33 checks and skips 11 optional-extension checks,
with no failures or warnings in the recorded run. The adapter guards FTZ/DAZ
and host tests verify restoration of the host's MXCSR.

The native kernel probe always marks input batches active. Original silent-batch
optimization and complete ModelStore notification timing remain outside these
comparisons. Wobble Amount/Balance/Magnitude and both Flux routes now have a
separate [composite comparison](wobble-macro-comparison.md). Resource
mapping details and those remaining questions are in
[wobble-control-findings.md](wobble-control-findings.md).

Reproduce with the original reference present locally:

```sh
cmake --build build-release -j2
python3 tools/compare_native_wobble.py --library build-release/libretro_dsp.so
```
