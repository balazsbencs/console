# Noise controls, samples and routing

Read-only resource audit of RC-20 1.0.5, reference image SHA-256
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.
Sources below are relative to `analysis/unpacked/guispec/`. These are static
resource findings; no full plugin, original scripts or constructor was run.
The native playback and compressor numerical kernels require separate recovery.

## Controls and defaults

Config.lua:207–249 declares:

| Model | Default | Metadata / role |
| --- | --- | --- |
| Noise_Enable | 0 | follows MacroNoiseEnable |
| Noise_Follow | 0 | normalized, displayed 0..100 percent |
| Noise_NoiseLevel | 1 unscaled | macro Noise; physical metadata -78/-12/0 dB anchors |
| Noise_NoiseType | 1 | sample type value; menu below |
| Noise_Tone | .5 unscaled | normalized, physical metadata -12..12 dB |
| Noise_GateLevel | 0 | fixed resource value |
| Noise_RandomSeekEnable | 1 | fixed resource value |
| NoisePost | 0 | Pre=0, Post=1 |
| SComp_Enable | 0 | follows MacroNoiseEnable |
| SComp_Ratio | .5 | fixed resource value |
| SComp_Averaging | 0 | fixed resource value |
| SComp_GainComp | 0 | fixed resource value |
| SComp_Threshold | 0 | Duck knob, displayed 0..100 percent |
| SComp_Attack | 0 | fixed resource value |
| SComp_Release | .2 | fixed resource value |
| SComp_HiPassSidechain | 1 | fixed resource value |
| WaveVizOutputNoise_GainOutput | 4 | visualization control |
| WaveVizOutputNoise_LimitEnable | 0 | visualization control |

MacroNoise defaults are Enable=0, Amount=0, Flux=0 (Config.lua:177).
MasterMacroScaler/Magnitude defaults to1 (:521–522). Noise's macro declaration
sets MultiEnableParams={Noise,SComp}; generic wiring :651–665 copies the same
Enable into both blocks. It does not derive compressor Enable from Duck.
Automation exposes Amount, Enable, Type, Tone_Unscaled, Follow, Routing, Duck
and Flux (:102,110–117). GUI.lua:35–43 binds these smaller controls directly.

These are normalized resource values. In particular compressor Ratio=.5 is
not evidence for a physical ratio of .5:1, and Attack0/Release.2 do not establish
seconds. Noise_Follow is not a recovered envelope coefficient formula. Physical
metadata conversion and these DSP meanings must come from the native kernels.

## Amount, Magnitude and Flux graph

Let M=Magnitude, A=raw MacroNoiseAmount, F=Noise_Flux and T=the user Tone knob.
The generic resource wiring in Config.lua:538–584 and FX_AudioBoot.lua:22–84
produces:

```text
amount_scaled = M * A
G = (M * F) * C(A,.99)
C(x,k) = (x*k) / (((k+k-1)*x+1)-k)
level_baseline = 1 * amount_scaled
tone_baseline = T
level_generator_gain = G * .1
tone_generator_gain = G * .1
```

Opcode X's .5 curves take identity branches; curve .99 applies to raw A,
not M*A. Preserve `(M*F)*C(A,.99)` multiplication order. The level generator
has macroType Noise, so FX_AudioBoot.lua:43–50 creates its macro-scaled baseline.
Tone has no macroType, so :54–58 copies Noise_Tone_Unscaled unchanged into
ModulateIn. Tone is not pulled toward zero or toward its midpoint by Magnitude.

Both generators declare macroGroup Noise, Chaos0, Decay0, Flux/localgain.1,
ModStyle.75, and bipolarNoise=true. They are **not** members of FreeRunning;
the group gain multiplier applies to both. Their smoothing is:

```text
level_smooth = FromToKnob(G, .82929, .82929, .21, .5, .5)
tone_smooth = .79840725806491
```

Noise_NoiseLevel_F_Smooth's separate declaration initially says .82, while
its fluxParam Smooth initially says .8292943460654. The explicit relation
at Config.lua:210 overwrites the settled value; at G=0 its target is .82929.
The two lower anchors are equal, so the lower half of G yields the same target;
the upper half interpolates to .21. Use the recovered FromToKnob routine to
preserve native float arithmetic rather than reducing the graph prematurely.

The retained original Drive scales generator output by16. ModStyle.75 adds
that value and clamps to [0,1], even though the stochastic source is bipolar:

```text
Noise_NoiseLevel = clamp(level_baseline + 16*level_noise, 0,1)
Noise_Tone = clamp(tone_baseline + 16*tone_noise, 0,1)
```

These normalized outputs subsequently undergo their model metadata conversions.
The level's -78/-12/0 dB anchors do not mean normalized Amount itself is a
linear amplitude gain, nor do they justify an invented linear dB mapping.
The exact metadata curve and final gain operation need native evidence.

At Amount0, Magnitude0 or Flux0, G is zero. At Flux0 alone the ordinary level
baseline can remain nonzero. Zero generator gain need not freeze internal
random history. No Enable-dependent stochastic gate appears in this resource
wiring. Original production seed record indices remain unidentified here.

## Sample selection

Config.lua:51–69 gives the following display order and native values:

| Menu position (zero based) | Native value | Display / description |
| --- | --- | --- |
| 0 | 4 | VINYL 1 / Vinyl1 |
| 1 | 12 | VINYL 2 / Vinyl2 |
| 2 | 5 | 7.5 inch / Tape7.5 ips |
| 3 | 6 | 15 inch / Tape15 ips |
| 4 | 0 | TUBE / Tube U47 & Tape |
| 5 | 2 | DC / DC Noise & Hum |
| 6 | 3 | MUFF / Muff Stomp box |
| 7 | 13 | RADIO / Static Radio |
| 8 | 8 | VHS / VHS Machine |
| 9 | 9 | CASS / C90 Cassette Tape |
| 10 | 10 | 8-BIT / 8-bit Home Computer |
| 11 | 1 | WHITE / JP-8 White Noise |
| 12 | 11 | PINK / Pink-ish Noise |
| 13 | 7 | STUDIO 1 / Studio Ambience1 |
| 14 | 14 | STUDIO 2 / Studio Ambience2 |
| 15 | 15 | APOLLO / Apollo Lunar Lander |

The declared model default is value1, corresponding to WHITE in that table;
it is distinct from menu position1. GUI.lua:39 uses a zero-indexed radio group
and the same serialized options as the drop-down. Host automation uses a
normalized options mirror (Config.lua:606–612), not a reordered physical
sample ID parameter. Exact host-normalization round trips require native option
metadata behavior; do not infer them solely by dividing a sample ID by15.

## Playback assets and boundary

The retained `analysis/unpacked/sound/` tree contains sixteen stack descriptors
and sixteen compressed sample members, IDs000..015. Nine descriptors end in
`_Noise.stackv3`; the remaining seven end simply `.stackv3`. All sixteen are
ASCII text beginning `Version 4`, `Product 1001`, `Pitch 1.0`, with one layer,
VDR1, recorded-volume offsets0, and one sample row. That row has Slot0,
JumpTo0, At0 and a filename such as
`990101_000_00_v700_Noise [ABC].nxac`. `HeadLth` varies by asset; no claim is
made here that it directly denotes decoded sample frames. The `.kpinfo` file
is empty in this retained extraction.

Every sample member inspected begins literal `NXAC v1.1`, followed by a
Compress options line and an Original file line ending `.wav`, then binary
payload. This establishes NXAC container identity and recorded source names;
it does not make the packaged bytes WAV, determine their codec, channel count,
sample rate, PCM bit depth, or decode them. Some original source names match
the type IDs (for example0-TubeU47,04-Vinyl,13-Radio scan), corroborating the
resource association without identifying numerical playback behavior.

No WAV/AIFF/FLAC/OGG samples were found within this extracted sound tree.
FX_GUIBoot.lua:835 calls `cppProc.scanAndManageSamples({'sound'})`; native
sample-management and NXAC decoding remain a separate layer. No proprietary
sample bytes are added to reconstruction sources or distributable CLAP modules.
A study module can accept user-supplied decoded samples or use independently
created fixtures; exact playback of a synthetic fixture establishes DSP
behavior, not equivalence to the original sixteen recordings.

## Pre/Post and compressor topology

FX_GUIBoot.lua:736–756 creates two `cContainer` instances with the same graph:

```text
container input -> cNoiseFX.in
cNoiseFX.out -> cSimpleComp.in
container input -> cSimpleComp.insidechain
cSimpleComp.out -> WaveVizOutputNoise.in [gain Noise_Enable]
WaveVizOutputNoise.out -> container output
```

cNoiseFX config is `types,16,driverprio,5` for the pre instance, and
`types,16,driverprio,0` for the post instance. Compressor config is `sidechain`.
The source entering the noise container therefore supplies the compressor's
sidechain independently from its main input, which is the Noise block output.
Whether Noise.out itself mixes dry input is a kernel question, not established
by this graph. Display gain4 also must not be assumed to multiply audio passed
through the visualization block without inspecting that block's implementation.

The outer connections (:798–829) feed input audio times InGain into NoiseFX,
and add NoiseFX.out into Wobble.in when !NoisePost. A separate dry input-times-
InGain connection always feeds Wobble.in. After Magnetic, the outer graph feeds
NoisePostFX and adds its output into Width.in when NoisePost; Magnetic.out also
feeds Width.in directly. Thus the outer graph is an added branch at either
location, not a simple replacement of the dry path. Pre is before Wobble and
all later color modules; Post is after Magnetic and before Width/output gain.

Config.lua:597–601 mirrors every Noise_, SComp_ and WaveVizOutputNoise_ control
into **both** NoiseFX/ and NoisePostFX/ namespaces. Changing Pre/Post does not
itself create new controls, load another preset, or specify that histories
must reset. CurrentNoiseFX at FX_GUIBoot.lua:849–850 selects the visualization
namespace only. The two native containers' actual processing/sleeping and
playback histories when their outer output is unused require native scheduling
evidence. Driver priority differences alone do not establish a reset rule.
