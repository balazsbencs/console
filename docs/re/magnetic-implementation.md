# Magnetic kernel and CLAP 0.2.0

The C11 implementation in `src/dsp/magnetic.c` reconstructs the stereo
`Block::cFlutterBlock::processAudio` routine at `0x353c30` in the supplied
RC-20 1.0.5 x86_64 Mach-O slice. It uses the recovered sine polynomial,
64-frame control interpolation, three fixed-point control smoothers, periodic
gain modulation, wear attenuation, stereo wear panning and dropout shaping.
Rate uses the original Fit3 metadata curve: normalized 0/.5/1 → 6/20/30 Hz.

`retro_magnetic_create`, `set`, `prepare`, `process`, `reset_smoothing`, `reset`
and `destroy` form a host-independent API. Processing operates in place on two
float32 channels, allocates no memory and accepts arbitrary frame counts.
Set/prepare/reset/process must be serialized per instance. Parameter changes
start a new control interval. With unchanged controls, partitioning a render
into arbitrary buffers preserves its samples exactly.

Depth Steer, Wear Gain and Wear Pan accept [-1,1]. Other continuous controls
accept [0,1]. Enable and Wear Stereo accept 0 or 1. The direct API requires
Dropout Max > Dropout Min, keeping the reciprocal span defined. Dropout Amount
acts as a zero/nonzero kernel gate: its original continuous scaling happens
upstream in the model. The reference's wear floor is .031.

Native prepare sets both oscillators to phase .25, then invalidates the phase
control. First processing resets the left phase to 0 and right to half Stereo
Phase. Subsequent phase changes also restart these phases. Native
`resetSmoothing` (`0x353c00`) invalidates the three smoothers only when started;
it retains oscillator phase and interpolation history. The study's full reset
instead restores deterministic constructor/prepare state. Enable=0 and the
Depth=WearDepth=0 early exit copy dry audio and freeze kernel history.

The standalone `RetroStudyMagnetic.clap` retains its 14 direct controls, IDs 3000–3013:
Depth, Depth Steer, Rate, Stereo Phase, Wear Gain, Wear Pan, Wear Stereo, Wear
Depth, Dropout Steer, Dropout Amount, Dropout Min, Dropout Max, Enable, Bypass.
State format 2 (`RSMG`) stores all 24 controls using explicit little-endian records;
format 1 states load with new controls defaulted.
Host automation is applied at sample offsets. Bypass copies dry and freezes
history; reset and activation restart the study state. Independent host
threshold automation can cross the original linked thresholds: the adapter
limits effective Min to .9999 and raises effective Max to at least Min+.0001.
The saved/displayed values remain the host values. This adapter safeguard is
separate from native fidelity comparisons, which use ordered thresholds.

Direct mode supplies external modulation signals. Macro mode now connects the
four free-running generators and recovered Amount/Balance/Magnitude/Flux/Dropouts
mappings. See [macro integration](magnetic-macro-integration.md) and the resource
wiring in [magnetic-control-findings.md](magnetic-control-findings.md). Defaults are study
settings, not an RC-20 preset. The complete effect graph, original model
notification timing and factory preset behavior remain pending.

## Evidence and comparison

REA 6.8.0/Ghidra recovered process, constructor (`0x352d40`), prepare
(`0x353a60`), smoothing reset and object-change (`0x354bd0`) routines. Local
retained results are `analysis/re/magnetic-*.json`, exported Evidence is
`magnetic-evidence.json`, and the closed retained-query snapshot is
`magnetic.rea`. The exact supplied slice hash is recorded in
[reference-manifest.json](reference-manifest.json).

`tools/native_magnetic_probe.py` executes only that hash-pinned isolated audio
routine with synthetic wrappers, reconstructed constructor/prepare fields,
Linux libm imports, scratch meter buffers and inactive visualization helpers.
It does not invoke the plugin constructor, full prepare, editor or installer.
`tools/compare_native_magnetic.py` compares the C kernel to that native fixture,
including control changes and both partial and full study resets. Results are
summarized in [magnetic-validation.json](magnetic-validation.json).

These checks establish the tested stereo kernel behavior with supplied
parameter/cache values. They do not establish the original observer scheduler,
macOS libm rounding, cross-platform reference equivalence or full plugin output.
The macOS CI build checks universal packaging and runs host tests on its runner
architecture; a real DAW test remains pending.
