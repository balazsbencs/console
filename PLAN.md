# RC-20 DSP reconstruction in C and CLAP

## Objective and scope

Build a standalone C DSP library that reproduces the selected RC-20 reference's
audio behavior, then expose it as a CLAP audio effect. Start with host-provided
parameter controls; a custom editor is a later milestone. This document is the
initial plan. Implementation has now started; see progress below.

## Current progress

- REA/Ghidra analysis is working, with reference hashes, an Evidence export and
  a reusable snapshot saved locally.
- Resource packs have been decoded and CRC-verified. Their Lua definitions
  identify the actual module graph and 58 host automation declarations.
- Digital 0.3.0 now includes Focus/Cut, native metadata curves, internal LFO,
  normalized smoothing, Amount/Balance/Magnitude and seeded Flux controls. It matches the
  isolated original kernel in 90 cases / 368,640 samples with zero error.
- Flux is integrated at the recovered 64-frame cadence and passes a composite
  native comparison over 393,216 samples with explicit seeds and settled inputs.
  Production relation-table seed indices, startup behavior and notification
  timing remain open; the study exposes its seed choices.
- Host, sanitizer and CLAP validator checks pass, including version 1 state
  migration and grouped same-time macro events, plus Flux buffer partitioning,
  repeatable reset and disabled-kernel modulation continuity.
- Wobble now has a C core and independent CLAP, matching 46 isolated native
  cases / 3,670,016 samples. Wobble 0.2.0 adds the settled macro/Flux graph,
  matching another 9,437,184 composite samples with explicit seeds. Original
  model timing and production seed indices remain open.
- GitHub Actions builds/tests Linux modules and universal macOS CLAP bundles.
- Noise, Distortion, Space, Magnetic, global routing and complete-plugin host
  renders remain pending.

Build instructions: [docs/BUILD.md](docs/BUILD.md). Detailed evidence, fidelity
scope and next steps: [docs/re/STATUS.md](docs/re/STATUS.md).

Target behavioral equivalence. Do not promise bit-identical output before
recovering floating-point details, random generators, sample assets and state
initialization. Record which behaviors are recovered, measured, approximated or
still unknown.

## Local inventory and initial evidence

- `RC-20 Retro Color.vst/`: extracted macOS VST. Its Info.plist declares 1.0.5.
- `Contents/MacOS/RC-20 Retro Color`: universal i386/x86_64 Mach-O, approximately
  19 MiB. REA opened it and identified SHA-256
  `ba09a41362d37cab7eaf4d8598a2f3f356830ba023cd1f78190d811f4e8b822d`.
- `Setup RC-20 Retro Color v1.0.5.exe`: Windows installer candidate.
- The directory/archive labelled 1.1.1.2 contains additional archives and an ISO;
  its filename alone does not establish the version of any contained DSP binary.
- Other plugin packages (BigSky, DIG, Deco, El Capistan) are separate targets.
- No existing C DSP modules, CLAP wrapper or build system were found.
- A preliminary local strings scan found `cWobblerBlock`, `cNoiseBlock`,
  `cNoiseFXBlock`, `cAESingleBandDistortion`, `cAEMagicDistortion`, and embedded
  Lua-like parameter wiring. These are useful search seeds, not recovered DSP
  algorithms or proof of processing order.

Historical initial blocker (resolved by the project-local Ghidra/JDK route):
REA MCP was connected, but its binary overview returned `provider_unavailable`:
Ghidra is unconfigured; Hopper is absent; IDA and Kuna are unconfigured.
REA also reports that its Ghidra route requires a thin slice for this universal
Mach-O. Decompilation and isolated native DSP comparisons now work; full-plugin
reference rendering remains pending.

## 1. Establish one reference and a working REA analysis route

Use the extracted 1.0.5 macOS binary as the initial static reference. Inventory
the installer payloads without running their installers and identify the actual
Windows plugin versions and hashes. Keep versions separate throughout analysis.

Configure a supported analysis engine, preferably Ghidra for this Linux machine,
and create a separate x86_64 thin Mach-O copy with its own hash and provenance.
Preserve all original inputs. Reopen the thin copy through REA, confirm function
analysis/decompilation works, and export Evidence and an analysis snapshot.

Exit: selected reference identity, reusable REA project and a documented runtime
route. The macOS reference needs a compatible macOS host for execution; a
Windows reference needs a compatible Windows host or a verified Wine setup.
Cross-platform builds must be checked before treating them as interchangeable.

## 2. Recover the DSP and parameter specification

Trace the VST entry point through processor construction, prepare/reset and
audio callbacks. Follow DSP calls, parameter setters and state serialization;
avoid spending the first pass on editor/framework code.

Recover processing order, routing, mono/stereo behavior, dry/wet law, gain
staging, latency, tails, sample-rate dependence, bypass semantics, parameter
ranges/defaults/curves, smoothing and modulation destinations. Trace Magnitude
and Flux behavior explicitly rather than treating them as simple gains.

Working module breakdown, to verify against this exact reference:

| Module | Questions to resolve |
| --- | --- |
| Noise | Generation versus playback assets, types, filtering, envelope/gating, stereo relationships |
| Wobble | Delay/interpolation, modulation sources, rates, depth, stereo and reset behavior |
| Distort | Mode transfer functions, filtering, drive compensation, oversampling |
| Digital | Quantization, sample-rate reduction, interpolation and filtering |
| Space | Reverb topology, delays, feedback, damping, stereo routing and tail |
| Magnetic | Level/dropout modulation, timing, envelopes and stereo linking |
| Global path | Filters/EQ, gain, width, mix, Magnitude, Flux and processing order |

For each module retain function addresses, REA Evidence IDs, pseudocode,
constants/tables, state layout, parameter mappings and unresolved questions.
Decompiler output is evidence to interpret, not compilable original source.

Exit: an evidence-linked specification sufficient to implement one module at a
time, including dependencies shared between modules.

## 3. Build a reference measurement harness

Render the original through a compatible offline plugin host. Save exact plugin
identity, parameter/state data, sample rate, block size and input/output audio.
Capture bypass, individual modules, parameter sweeps and selected combinations.

Use impulses, silence, tones at several levels, sweeps, stereo probes and short
musical excerpts. Repeat renders to discover randomization and reset behavior.
Measure latency, transfer curves, harmonic spectra, frequency response, tails,
modulation and stereo correlation as appropriate.

For deterministic paths, compare aligned samples and residuals. For stochastic
paths, recover seeds where feasible; otherwise compare distributions, spectra,
envelopes and modulation statistics across repeated captures. Define numerical
acceptance thresholds from reference repeatability before claiming a match.

Exit: a reproducible fixture set and comparison reports tied to exact settings.
If reference execution is unavailable, static reconstruction can proceed, but
audio equivalence remains explicitly unverified.

## 4. Implement the independent C DSP core

Use C11, CMake and an offline renderer. Proposed layout:

```text
src/dsp/       engine, parameters, shared DSP primitives, six modules
src/clap/      CLAP entry point and host adapter
tools/         offline renderer and reference comparison utilities
tests/         fixtures and DSP/host contract checks
docs/re/       module specifications and evidence index
third_party/  pinned CLAP headers and required notices
```

Define explicit create/prepare/reset/set-parameter/process/destroy operations.
Keep allocation, file access and locks out of audio processing. Make state
instance-local and random state reproducible for tests. Recover denormal and
precision behavior where it affects matching.

Implement shared primitives and a bypass/gain baseline first, then the simplest
well-understood deterministic module. Port the remaining modules in dependency
order, validating each before integrating the complete signal chain. Add
modulation and global interactions with their recovered routing and smoothing.

Exit: independently renderable DSP with per-module comparisons, whole-chain
fixtures and an explicit list of remaining differences.

## 5. Package as CLAP

Pin the official CLAP headers and implement the plugin factory, lifecycle and
audio adapter. Expose stable parameter IDs, names, units, ranges, formatting,
automation and versioned state persistence. Apply timed parameter events at
their correct sample offsets. Report audio ports, latency and tails according
to recovered behavior and the supported CLAP extensions.

Produce a Linux `.clap` first on this machine; add Windows/macOS builds when
their toolchains and host testing are available. Use a distinct plugin ID and
name. Keep the initial version usable through a host's generic parameter UI.

Exit: a loadable CLAP that produces the same output as the standalone C engine
for the same audio, parameter events and reset sequence.

## 6. Validate the deliverable

Run a CLAP validator and a real-host smoke test. Check state round trips,
automation, repeated activation, multiple instances, bypass, silence/tails,
varying block sizes, sample-rate changes and long runs for NaNs or instability.
Run memory/undefined-behavior sanitizers offline and measure CPU use under
representative full-chain settings. Compare final CLAP renders to the reference.

Deliver source, reproducible build instructions, the `.clap` artifact,
parameter/module documentation and a fidelity report with measured differences.

## First implementation milestone

Working REA engine + fixed reference version + DSP/parameter map + one isolated
module reproduced in C and checked against reference renders. This establishes
the evidence and validation workflow before expanding to all six modules.
