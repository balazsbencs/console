# Build the CLAP study modules

This milestone implements the **stereo Digital DSP**, including Focus/Cut,
internal modulation, normalized curves, Amount/Balance/Magnitude mapping and
seeded Flux, in a standalone C11 library and a Linux x86_64 CLAP effect. The complete RC-20
signal chain is still in development. See [re/STATUS.md](re/STATUS.md) for the
exact fidelity boundary and remaining work.

Wobble is now a second independent C DSP module and CLAP. Its oscillator,
variable delay, interpolation, smoothing and enable behavior match the isolated
native kernel across 46 cases / 3,670,016 samples with zero error. Wobble 0.2.0
adds Amount/Balance/Magnitude, Stereo and two distinct Flux
paths, verified over another 9,437,184 composite native audio samples. Its
production seed indices and full original notification timing remain open.
See [Wobble fidelity notes](re/wobble-implementation.md) and
[macro controls](re/wobble-macro-integration.md).

The GitHub Actions workflow builds all six plugins on Linux and macOS. The macOS
job builds universal **arm64 + x86_64** bundles, runs host tests, verifies both
slices and bundle metadata, and uploads ZIPs preserving the `.clap` bundles.
Download them from the `RetroStudy-macOS-universal` artifact on a successful
[workflow run](https://github.com/balazsbencs/console/actions/workflows/build.yml).
Native reference fixtures are local-only and are not required by CI.

Build with GCC or Clang and CMake 3.20 or later:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j2
ctest --test-dir build-release --output-on-failure
```

The modules are `build-release/RetroStudyDigital.clap` and
`build-release/RetroStudyWobble.clap`, plus
`build-release/RetroStudyMagnetic.clap`,
`build-release/RetroStudyDriveCores.clap`,
`build-release/RetroStudySpace.clap` and
`build-release/RetroStudyNoiseDucking.clap`. They are self-contained apart
from the system C/math libraries; none loads the original plugin or its
resource packs. Load it through a CLAP host's scan path and use the host's
generic parameter editor. No custom editor is implemented.

The 23 controls include the original direct DSP controls (Bit Depth, Sample
Rate, Smooth, Mix, Feedback, Filter Low/High, Round Error, Stereo, AA2/AA3,
Bypass), plus Filter Mode, Internal Modulation Rate/Depth, Control Mode,
Amount, Rate / Bits Balance, Magnitude, Enable, Flux, Flux Bit Seed and Flux
Rate Seed.

- Control Mode **0** uses physical Bit Depth and Sample Rate in Hz.
- Control Mode **1** uses Amount, Balance and Magnitude with the recovered
  normalized curves. Amount=0 gives minimum degradation. Balance=0 selects rate
  reduction, 1 selects bit reduction, and 0.5 gives both full weight.
- Filter Mode **0** is Focus, **1** is Cut.
- Enable applies in macro mode. Bypass applies in both modes; either disabled
  path copies dry audio. Enable freezes the Digital kernel while Flux keeps
  ticking; Bypass freezes both.
- Other continuous controls are normalized [0,1], except Filter Low/High in Hz.
  Internal modulation is separate from the Flux noise generators.
- Flux operates in macro mode and ticks once per 64 processed frames, including
  while Enable=0. Its two integer seed controls accept 0–65535 and default to
  **0 and 1**, which are explicit study choices. The original derives seeds from
  relation-table indices that have not been captured. Changing either seed,
  reset, or activation restarts both study generators. Physical mode freezes
  their clock.

Defaults preserve the earlier audible physical study settings. They are not an
RC-20 factory preset. State format 3 retains IDs 1000–1019 and adds Flux/seed
IDs 1020–1022. Version 1 and 2 states load with new controls at defaults. Saved
state stores controls and seeds; reset restarts the modulation trajectory. Audio
is stereo float32.

The C API has no host dependency. Initialize one engine per instance, set its
parameters, then prepare/reset it before processing:

```c
retro_digital dsp = {0};
retro_digital_params params = retro_digital_defaults();
if (!retro_digital_set(&dsp, &params) || !retro_digital_prepare(&dsp, 48000.0f))
    return 1;
retro_digital_process(&dsp, left, right, out_left, out_right, frames);
```

Zero-length calls are allowed. Buffers may be processed in place as matching
left/right input-output pairs. Set/prepare/reset/process calls for one engine
must be serialized by the caller; the CLAP adapter publishes host control
values atomically and applies them on the audio thread.

For ASan/UBSan validation:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DRETRO_SANITIZE=ON
cmake --build build-sanitize -j2
ctest --test-dir build-sanitize --output-on-failure
```

To reproduce the native DSP comparison on Linux x86_64, keep the supplied VST
in place, then run:

```sh
python3 tools/prepare_reference.py
python3 tests/compare_native.py --library build-release/libretro_dsp.so
```

The probe executes only the hash-pinned `cBitColor` machine-code kernel with
synthetic state and Linux libm bindings. It does not initialize the macOS plugin
or run installers. The expanded comparison covers Focus/Cut, internal LFO,
normalized curves, batch-aligned updates and reset in 90 cases. Full-plugin
execution and original model notification timing remain unverified.

The shared scaling curves, macro arithmetic and Flux noise primitives have
separate comparisons against native functions:

```sh
python3 tools/compare_native_curves.py --library build-release/libretro_dsp.so
python3 tools/compare_native_relations.py --library build-release/libretro_dsp.so
python3 tools/compare_native_noisegen.py --library build-release/libretro_dsp.so
python3 tools/compare_native_fromto.py --library build-release/libretro_dsp.so
python3 tools/compare_native_flux.py --library build-release/libretro_dsp.so
```

The composite Flux comparison uses original native relation arithmetic, Drive
and Digital processing with explicit seeds and manually settled notifications.
It matches 393,216 audio samples with zero error. Production seed indices,
startup/reset sequencing and original host notification timing remain open.
See [Flux integration](re/flux-integration.md) for the C API and fidelity scope.

The `analysis/` directory contains local inputs, extracted resources, original
pseudocode, Evidence, snapshots and reports. It is ignored by Git. The build
does not need those files. The original binaries and extracted content are not
embedded in the generated CLAP.

CLAP headers are pinned to upstream 1.2.7; the upstream MIT license and exact
commit are in [third_party/clap](../third_party/clap/PROVENANCE.md).

For a local macOS universal build:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

On macOS each `.clap` is a bundle directory containing `Contents/Info.plist`
and an executable named after the module in `Contents/MacOS/`. Copy the entire
bundle into `~/Library/Audio/Plug-Ins/CLAP/`. CI artifacts are unsigned and
unnotarized. macOS may require local authorization to load a downloaded bundle.

Wobble's original ten controls are Wow Depth, Flutter Depth, Wow Rate, Flutter Rate,
Stereo Phase, Mix, LFO Noise, Noise Scale, Enable and Bypass. Its rate controls
are normalized metadata inputs; physical ranges are .1–4 Hz and 6–20 Hz.
Stereo Phase is [0,.4], LFO Noise is [-1,1], and other continuous controls are
[0,1]. LFO Noise is an externally supplied perturbation value in direct mode. The
defaults make the module audible.

Wobble Control Mode **1** enables Amount, Wow / Flutter Balance, Magnitude,
Flux and Stereo. Amount/Flux/Magnitude and Balance are normalized [0,1].
Balance 0 selects wow, 1 selects flutter, and .5 gives both full weight.
Stereo maps 0/1 to the original 0/.4 phase offset. Two integer seed controls
make Flux repeatable; their defaults 0/1 are study choices because the original
relation-table seed indices are unknown. Enable keeps Flux advancing through
the kernel's disable fade; Bypass freezes both. Direct mode pauses Flux.
Version 1 Wobble states load with new controls at defaults.

To reproduce the settled Wobble macro/native comparison:

```sh
python3 tools/compare_native_wobble_macro.py --library build-release/libretro_dsp.so
```

Magnetic 0.2.0 exposes the recovered periodic gain, wear/pan and dropout kernel
with direct inputs plus Amount/Balance/Magnitude/Flux/Dropouts macro mode and
four free-running generators. It retains IDs 3000–3013 and adds 3014–3023. Rate
maps normalized 0/.5/1 to 6/20/30 Hz. Control Mode defaults to 0 (direct); mode 1
uses the recovered mappings. Four seed controls default to study seeds 0/1/2/3
because original production relation indices are unresolved. Enable keeps the
generators advancing; Bypass freezes them; direct mode pauses their clock.
See [macro controls and lifecycle](re/magnetic-macro-integration.md).
See [Magnetic implementation and controls](re/magnetic-implementation.md) for
ranges, threshold automation safeguards, reset semantics and fidelity limits.

```sh
python3 tools/compare_native_magnetic.py --library build-release/libretro_dsp.so
python3 tools/compare_native_fromto_knob.py --library build-release/libretro_dsp.so
python3 tools/compare_native_magnetic_macro.py --library build-release/libretro_dsp.so
```

Drive Cores 0.5.0 packages the recovered Tube Pair, Air Pressure and Iron
Transformer numerical cores. Character 0/1/2 selects them, Amount is normalized
[0,1], and Mix, Enable and Bypass are study adapter controls (IDs 4000–4004).
Processing Mode 0 preserves the original core-only study. Mode 1 adds the
recovered stereo frequency split, output filter, band gains and smoothed mix
with controls 4005–4010. State formats 1/2/3/4 load with their existing mode; format 5 stores all
fifteen controls. See [Processor mode](re/magic-processor-implementation.md).
Processing Mode 2 adds the three-character Chain, gain staging and settled
Magnitude/Flux routing. Controls 4011–4013 are Magnitude, Flux and Flux Seed.
Seed zero is an explicit study choice. See [Chain mode](re/dist-chain-integration.md)
for its 64-frame control cadence and unresolved original graph timing.
Processing Mode 3 (Classic) selects Crunch/Heavy/Zap with the Character control.
It exposes the recovered single-band processor, reusing frequency limits,
band gains, output lowpass and Mix. Control 4014 is Reference Level in dB,
used only in Classic mode. Magnitude/Flux apply to both Chain modes. Classic
uses native SIMD arithmetic per sample and a fixed 64-frame history-cleanup
cadence, giving zero added latency and stable output across host partitions.
Heavy uses the original SSE reciprocal estimate on x86; other architectures
use division and have no claim of bit identity with the x86 reference.
Defaults are study controls rather than original character presets. Classic
Chain mode 4 now adds the recovered pre/post peak EQ and gains,
resource Amount mappings and seeded Flux for those three characters. Direct
processor band-gain, output-lowpass and reference controls do not override the
chain resource values. Original complete graph timing remains pending. See
[Classic processor](re/classic-implementation.md) and
[Classic chains](re/classic-chain-integration.md).
The inspected Magic path has no internal oversampling stage.

```sh
python3 tools/compare_native_tube_pair.py --library build-release/libretro_dsp.so
python3 tools/compare_native_air_pressure.py --library build-release/libretro_dsp.so
python3 tools/compare_native_iron_transformer.py --library build-release/libretro_dsp.so
python3 tools/compare_native_iron_stereo.py --library build-release/libretro_dsp.so
python3 tools/compare_native_magic.py --library build-release/libretro_dsp.so
python3 tools/compare_native_gain.py --library build-release/libretro_dsp.so
python3 tools/compare_native_knob_gain.py --library build-release/libretro_dsp.so
python3 tools/compare_native_dist_chain.py --library build-release/libretro_dsp.so
python3 tools/compare_native_classic_split.py --library build-release/libretro_dsp.so
python3 tools/compare_native_classic.py --library build-release/libretro_dsp.so
python3 tools/compare_native_peak_eq.py --library build-release/libretro_dsp.so
python3 tools/compare_native_classic_chain.py --library build-release/libretro_dsp.so
```


Retro Study Space 0.2.0 includes the ResoX resonator and all-pass network,
stereo modes, focus filters and predelay. Choose Macro under Control Mode to
use Amount, Magnitude and Flux. Flux Seed makes the modulation repeatable.
The new controls use IDs 5015–5019. RSSP state version 2 loads version 1
sessions in Direct mode. See [Space macro integration](re/space-macro-integration.md).

```sh
python3 tools/compare_native_space_prepare.py --library build-release/libretro_dsp.so
python3 tools/compare_native_space.py --library build-release/libretro_dsp.so
python3 tools/compare_native_space_macro.py --library build-release/libretro_dsp.so
```

## Noise Ducking

Retro Study Noise Ducking 0.2.0 offers two Texture Source settings.

- **Samples** plays the selected local NXAC texture, ducks it with the main
  stereo input, then mixes it into that input. Choose a Noise Type and use
  Amount to set the texture level. White is the default type. Install the
  local files before activation as described below.
- **External** puts the main stereo input through the linked compressor and
  uses its stereo Sidechain input as the key. Route a texture track to Main In
  and the dry signal to Sidechain. A disconnected sidechain supplies silence.

To use Samples, copy your local `*.nxac` files from
`analysis/unpacked/sound/samples.xpak/` to the directory for your platform.
Keep their original names, for example `990101_001_00_v700_Noise [ABC].nxac`
for White.

- macOS uses `~/Library/Application Support/RetroStudy/Noise/`.
- Linux uses `$XDG_DATA_HOME/RetroStudy/Noise/`, falling back to
  `~/.local/share/RetroStudy/Noise/`.
- To override either directory, set `RETRO_STUDY_NOISE_DIR` in the DAW's
  launch environment to the absolute path containing the NXAC files.

The plug-in decodes the files at activation and shares the decoded PCM
between active instances. The last deactivation releases the bank. Restart
the host after changing the files or their directory. A missing or invalid
selected file contributes silence, so the main signal passes through.

Raise Duck from 0 to lower the detector threshold. Bypass passes the main
input through and pauses processing. In Samples mode, Enable off or Amount
zero pauses the texture branch. A newly selected texture starts at frame
zero. The plug-in uses stereo float32 buffers and adds zero latency.

State format `RSND` v2 saves controls 6000–6011. New instances and v1 sessions
start in External. Samples is a standalone dry-plus-texture branch. Follow,
Tone, Flux and routing around the other effects remain pending. See
[compressor findings](re/noise-comp-findings.md),
[Noise controls and routing](re/noise-control-findings.md) and
[NXAC format and playback](re/noise-nxac-findings.md).

```sh
python3 tools/compare_native_noise_comp.py --library build-release/libretro_dsp.so
python3 tools/compare_native_noise_interp.py --library build-release/libretro_dsp.so
python3 tools/compare_native_nxac.py --library build-release/libretro_dsp.so
```
