# Magnetic macro and modulation integration

Magnetic 0.2.0 connects the recovered Amount, Wear/Flutter Balance, Magnitude,
Flux and Dropouts graph to the C kernel. Four separate `rs_noisegen` instances
supply depth steering, dropout steering, wear gain and wear pan. The new
`rs_magnetic_macro` API in `src/dsp/magnetic_macro.h` has explicit reset seeds,
a tick operation and an apply operation. Tick runs once per 64 audio frames at
sample_rate/64; apply preserves direct Rate, Stereo Phase, Wear Stereo and
Enable controls.

The mappings and their packaged-resource sources are recorded in
[magnetic-control-findings.md](magnetic-control-findings.md). The previously
unused FromToKnob opcode is now separately native-verified over 361,146 cases
with GCC and Clang: [arithmetic findings](fromto-knob-findings.md). Its lower
and upper halves have independent curvature, with native float operation order.
Magnetic depth gain uses anchors .14/.17/.5 with both curvatures .5.

All four generators belong to the resource's FreeRunning group. Their gains
are not multiplied by group Flux. Flux zero therefore still produces random
modulation: it selects depth gain .14, depth smoothing .83 and wear smoothing
.91. Amount zero or Magnitude zero removes effect depths and dropout amount,
while the four generators continue ticking in macro mode. Dropouts follows
its separate curved Amount and Magnitude path, then controls dropout thresholds
and generator smoothing. It is independent of Wear/Flutter Balance.

## Host controls and lifecycle

The original fourteen IDs 3000–3013 retain direct-mode behavior. New controls:

| ID | Control | Range / default |
| --- | --- | --- |
| 3014 | Control Mode | 0 direct, 1 macro; default 0 |
| 3015 | Amount | [0,1], default 0 |
| 3016 | Wear / Flutter Balance | [0,1], default .5 |
| 3017 | Magnitude | [0,1], default 1 |
| 3018 | Flux | [0,1], default 0 |
| 3019 | Dropouts | [0,1], default 0 |
| 3020 | Depth Flux Seed | integer [0,65535], default 0 |
| 3021 | Dropout Flux Seed | integer [0,65535], default 1 |
| 3022 | Wear Flux Seed | integer [0,65535], default 2 |
| 3023 | Pan Flux Seed | integer [0,65535], default 3 |

Balance 0 selects wear, 1 selects flutter, and .5 gives both full weight.
Wear Stereo and Rate remain active in both modes. Stereo Phase is a direct
study control retained from the kernel interface; no new front-panel mapping
for it is inferred here. Direct modulation/threshold controls apply in direct
mode. In macro mode those effective inputs come from the recovered graph.

Enable=0 freezes the kernel but keeps the four generators advancing. Bypass
copies dry audio and pauses both kernel and generators. Direct mode pauses the
macro generator clock. Activation and full reset restart the four generators
and kernel histories. Changing any seed resets all four generator histories
and restarts their 64-frame clock. These are explicit study adapter choices.
State format 2 stores all 24 controls; old format 1 states load their fourteen
controls and default the ten added controls. Controls and seeds are saved;
audio history is restarted on reset rather than serialized.

The seeds are repeatable study choices. Original generator reset seeds derive
from complete serialized relation-record indices. The actual production
Magnetic indices and boot ordering remain unresolved; 0/1/2/3 are not claimed
to be RC-20 factory seeds.

## Native comparison and boundaries

`tools/compare_native_magnetic_macro.py` builds four synthetic native relation
records and a fifth scratch record for arithmetic. Original handler
`objectChanged` at 0xf0d70 computes macro relations; original Drive at 0xd6fd0
updates all four original noise states; original cFlutter at 0x353c30 renders
the stereo audio. Upstream relation inputs are explicitly settled before each
tick. The exact reference hash and retained static REA Evidence remain in
[magnetic-validation.json](magnetic-validation.json).

Both GCC and Clang builds pass 60 scenarios / 79,872 ticks / 10,223,616 audio
samples with zero error. All 319,488 noise outputs, 718,848 mapped targets and
12,779,520 compared noise-state bytes match exactly. Cases cover three sample
rates (44.1/48/96 kHz), two sets of seeds including signed extremes, zero Flux,
zero Amount/Magnitude, wear-only, flutter-only, full and partial dropouts,
changing controls, seed restarts, stereo changes, enable intervals and partial
smoothing reset. The direct 76-case kernel suite also still passes.

The host test independently composes 64-frame generator ticks and kernel calls,
then compares fixed versus irregular host buffers with nonaligned automation.
It checks in-place output, simultaneous seeds and macro events in reversed
order, bypass/direct-mode pauses, advancement during disabled intervals,
state migration and deterministic reset after a warmup render. GCC, Clang and
ASan/UBSan checks pass.

The native probe now retains prepare's dirty depth/phase caches if an unchanged
value is republished before the first render. This fixes fixture initialization
for composed tests; it does not change the C kernel or reference audio code.

These comparisons establish the settled synthetic graph. They do not run the
complete original ModelStore scheduler, Lua initialization, plugin constructor
or host. Notification ordering, original startup/reset timing, production seeds,
macOS math rounding and full-chain reference output remain unverified.
