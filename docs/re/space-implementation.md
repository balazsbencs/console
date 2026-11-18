# Space C DSP and CLAP study

Retro Study Space 0.1.0 introduced the recovered `Block::cResoXBlock` processor as a
fifth standalone CLAP module. This is the Space engine selected by the supplied
resource graph, rather than the unrelated generic SimpleReverb block.

The C processor includes twelve resonator delays per channel, four all-pass
stages per channel, two predelay rings, cubic predelay interpolation, low/high
frequency filters, stereo detuning, native mix curves and control smoothing.
Its explicit 64-frame control cadence survives arbitrary host partitions
without added buffering latency. The sample processing allocates no memory.

## Controls and lifecycle

The CLAP exposes fourteen direct engine controls plus Bypass, IDs 5000–5014.
Filter controls accept physical Hz and predelay accepts milliseconds; other
continuous controls use the documented study ranges. Native construction
values supply defaults except Enable, which defaults to one for the isolated
study. The decoded full-product defaults differ: see
[Space controls](space-control-findings.md). State format RSSP version 1 stores
all fifteen controls, rejects malformed records atomically and supports partial
stream reads/writes.

Space 0.2.0 adds the Amount/Magnitude/Flux model and free-running generator
in Macro mode. See [Space macro integration](space-macro-integration.md).
Pre Wobble Noise and Pre Wobble Amount can be supplied directly. Allpass and
Reverb Mode expose additional native study switches absent from the product's
user controls. Meter Feedback is retained as a study value; the native
flag governs meter publication, which this audio-only implementation omits.
It does not control the resonator feedback gain.

Controls latch at the next 64-frame engine boundary. Bypass freezes the engine
clock and histories. Enable follows the recovered kernel's clear/skip behavior;
setting it differs from study Bypass. The adapter rejects nonfinite/out-of-range
controls before publication. Study sample rates are 1000–1536000 Hz.

`retro_space_reset_smoothing` models the separate native predelay-smoothing
reset. `retro_space_reset` is an explicit study hard reset: it restores clean
numeric preparation for current controls, clears all rings and rewinds their
positions. It reuses existing allocations. This is stronger than the native
`clearDelayLines`, which preserves ring positions and unrelated filter history,
or native `resetSmoothing`, which only updates smoothing state. CLAP reset and
activation supply repeatable clean state; they do not reconstruct product boot
notification timing.

Preparation allocates fixed capacities derived from the native pitch/time
constants. Failure releases partial allocations and leaves processing disabled.
No binary data, original code pages or native C++ objects are shipped with the
C library or CLAP. The private native-shaped numerical layout makes comparison
with the reference explicit; C performs all arithmetic and owns every buffer.

## Evidence and validation boundary

Reference: supplied RC-20 1.0.5 x86_64 Mach-O, SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
REA/Ghidra records for constructor, prepare, process, clear, smoothing and delay
helpers were retained in `analysis/re/space-evidence.json` and the provider-neutral
query snapshot `space.rea`. The project-local REA session was exported and closed.
The decompiled text is analysis evidence, not original source.

The isolated native fixture executes original numerical preparation and audio
processing on synthetic state. It binds necessary numerical/allocator imports
to Linux libraries, supplies deterministic zero-filled allocation, and installs
small caller-owned virtual tables pointing only to the original delay numerical
helpers. It executes no original constructor, resource script, plugin entry
point or host. Model wrappers receive explicit cached physical values. Original
visualization/model publication is not part of the C audio engine.

`compare_native_space_prepare.py` checks numeric preparation, capacities and ring
positions against original prepare. `compare_native_space.py` compares audio,
processing state and all thirty-four delay buffers across changes to controls
and C buffer partitions. These use an active original input marker rather than
claiming its silence optimization. Linux libm is used on both sides; bit identity
with Apple's math library, original arm64 code or a complete hosted plugin is
not established. See [validation results](space-validation.json) and
[core findings](space-dsp-findings.md).

CI now builds all five Linux effects and all five universal arm64/x86_64 macOS
CLAP bundles. It runs the host contracts and verifies bundle versions and both
Mach-O slices before archiving them.
