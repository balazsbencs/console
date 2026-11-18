# Space controls and resource graph

Reference: RC-20 1.0.5 x86_64 image SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
This inspection uses decoded resources and original instructions, without
executing the complete constructor or plugin. Resource paths below are relative
to `analysis/unpacked/guispec/`.

## Physical kernel bindings

`Block::cResoXBlock` constructs fourteen model wrappers at 0x35aa10. Bindings
at 0x35b41e–0x35b64e establish the following object-relative offsets and
**native construction defaults**, which differ from the settled resource graph.
Raw instructions: `analysis/re/space-ctor-raw.asm`.

| Name | Wrapper | Value/cache | Construction default | Native metadata |
| --- | --- | --- | --- | --- |
| Enable | +0x108 | +0x138 | 0 | plain float |
| Decay | +0x140 | +0x170 | .8 | plain float |
| Dampen | +0x178 | physical +0x214 | 0 Hz | type11, endpoints 0→22050, middle2000 |
| Soften | +0x218 | physical +0x2b4 | 22050 Hz | type11, endpoints 22050→200, middle2000 |
| Level | +0x2b8 | +0x2e8 | .15 | plain float |
| StereoDetune | +0x2f0 | +0x320 | 0 | plain float |
| StereoMode | +0x328 | +0x358 | 0 | plain float |
| DualMono | +0x360 | +0x390 | 0 | plain float |
| EnableFeedBacks | +0x398 | +0x3c8 | 1 | plain float |
| DoAllPass | +0x3d0 | +0x400 | 1 | plain float |
| ReverbMode | +0x408 | +0x438 | 1 | plain float |
| PreDelay | +0x440 | physical +0x4dc | .01 ms | type11, endpoints 0→100, middle50 |
| PreWobbleNoise | +0x4e0 | +0x510 | 0 | plain float |
| PreWobbleNoiseAmount | +0x518 | +0x548 | 0 | plain float |

Plain wrappers are 0x38 bytes, with flags +0x2c/+0x2d and value +0x30.
Metadata wrappers are 0xa0 bytes, with conversion function +0x28,
min/max/middle +0x38/+0x3c/+0x40, validity flags +0x94/+0x95, raw value
+0x98 and physical cache +0x9c. Metadata builder 0x95e80 serializes its four
float arguments as default, minimum, maximum and middle (format string at
0x6350c6). Soften's descending endpoints are intentional in these instructions;
they must not be silently transcribed as ascending metadata.

Plain wrappers themselves declare no numerical minimum/maximum. Normalized
0..1 resource controls and Boolean interpretation do not establish native
validation for arbitrary out-of-range model values. In particular no native
range beyond resource values is claimed here for ReverbMode or DoAllPass.

## Settled resources and front panel

FX_GUIBoot.lua:785 instantiates `Space={class="cResoX",addDSPMetaData=false}`.
Config.lua:358–380 supplies its model metadata and values. These overrides
explain why native constructor frequency metadata is not the GUI frequency
range. DoAllPass and ReverbMode have no corresponding Config declaration and
retain native construction defaults in this resource graph.

| Resource | Normalized/default value | Meaning / physical mapping |
| --- | --- | --- |
| Space_Enable | 0 | generic MacroSpaceEnable copy |
| Space_Level | 1 unscaled | multiplied by Magnitude×Space Amount |
| Space_StereoMode | 0 | Mono/Stereo Boolean options |
| Space_Decay | .8 | displayed 0..100 percent |
| Space_DualMono | 1 | fixed resource value |
| Space_PreDelay | 0 | displayed 0..100 ms |
| Space_PreWobbleNoiseAmount | 0 initially | copied from group Flux |
| Space_PreWobbleNoise | 0 baseline | free-running modulation below |
| Space_StereoDetune | .3918 | fixed resource value |
| Space_Dampen | 0 | log100..16000 Hz, physical default100 Hz |
| Space_Soften | 1 | same log range, physical default16000 Hz |
| Space_EnableFeedBacks | 1 | fixed resource value |

The frequency metadata includes `fitMiddle=1000` but explicitly selects
`type="log"`; a nearby source comment questioning the curve is not operative
code. Automation exposes Amount, Enable, Decay, FilterLo=Dampen,
FilterHi=Soften, PreDelay, StereoMode_Normalized and Flux
(Config.lua:106,141–147). Macro defaults are Enable0/Amount0/Flux0 (:176).
PreDelay is specified in milliseconds, not seconds or normalized samples.
Native resetSmoothing at 0x35c880 multiplies physical PreDelay by .001 and
sample rate, storing sample counts at +0x1134/+0x1138 and setting +0x610=1.
It does not clear all delay lines.

## Amount and Flux

With M=Magnitude, A=MacroSpaceAmount and F=Space_Flux, the settled graph is:

```text
MacroSpaceAmountScaled = M*A
Space_Level = 1*MacroSpaceAmountScaled
Space_Flux_Scaled = (M*F)*C(A,.99)
Space_PreWobbleNoiseAmount = Space_Flux_Scaled
C(x,k) = (x*k)/(((k+k-1)*x+1)-k)
```

Sources: Config.lua:538–547,568–584; FX_AudioBoot.lua:22–28,106–109.
Level has no Flux generator; Flux does not randomly modulate Level.

PreWobbleNoise declares macroGroup FreeRunning, Chaos.5, Decay.59,
Smooth.42, local Flux/gain.5, ModStyle.75, and omits bipolarNoise (false).
FreeRunning has disableFluxScaling=true (Config.lua:181), so the unscaled
FX_AudioBoot.lua:51–63 branch keeps generator gain .5 independently of M,F,A.
Its ModulateIn is the declared baseline0. The retained noise/modulation
primitives therefore produce:

```text
Space_PreWobbleNoise = clamp(16*generator_output,0,1)
```

The separately copied PreWobbleNoiseAmount supplies the macro Flux scaling.
Flux0 thus makes the noise amount zero while the free-running generator can
continue advancing. FX_AudioBoot.lua:7 requests sync every batch; this resource
statement alone does not establish an arbitrary host callback as the native
control quantum. Exact process cadence belongs to the kernel recovery.
Production generator seeds follow the previously recovered serialized-record
rule; actual Space record indices have not been identified here.

## Other state and limits of this finding

The objectChanged callback at 0x361360 responds only to the model ID stored at
+0x1150. It writes oscillator shape-related values +0x628=.75,
+0x630=.1, +0x62c=+0x634 times a constant and clears 0x180 bytes at +0x760.
It is not a generic notification that clears all reverb history. Observer thunk
0x3613b0 adjusts the secondary observer pointer by -0x100 first.

Prepare0x35be80, resetSmoothing0x35c880, processAudio0x35c900 and
clearDelayLines0x361840 are distinct routines. Constructor state includes
oscillator callback0x135330 at +0x618 and twelve additional model IDs at
+0x550..+0x57c. Full delay allocation/feedback topology and exact arbitrary
parameter validation require the separate core recovery. No full front-panel
macro or Flux reconstruction, seed identity, oversampling or complete original
plugin behavior is implied by exposing direct kernel controls.

## Direct study adapter contract

The first Space adapter exposes all fourteen kernel controls in the wrapper
order above plus Bypass, IDs5000..5014, with state magic RSSP/version1.
Enable defaults to1 to make the isolated study audible; the other defaults
follow construction, including Level.15. Both filter controls accept0..22050
physical Hz, PreDelay0..100ms, PreWobbleNoise-1..1, all other continuous
controls0..1. Enable, StereoMode, DualMono, Feedbacks, Allpass, ReverbMode and
Bypass are stepped0/1 controls. The original GUI does not expose Allpass or
ReverbMode; their adapter labels identify them as study controls. EnableFeedBacks
is labeled Meter Feedback (Study): it controls native meter publication, which
the C audio engine omits, and does not disable resonator feedback. Supported
study sample rates are1000..1536000Hz. These are explicit C API bounds,
not a claim that the native unbounded plain model wrappers enforce them.
