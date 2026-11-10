# Distortion resource graph and numerical boundaries

Read-only inspection of decoded RC-20 1.0.5 resources and retained native
symbols/raw disassembly. No new REA session, original resource-script
execution, plugin constructor or complete plugin was run. Lua paths below
are relative to `analysis/unpacked/guispec/`. Reference SHA-256:
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.

## User types and internal selectors

Config.lua:71–79 exposes six options in this order:

| Option index | Internal Dist_Type | Display / resource name | Numerical engine / local shape |
| --- | --- | --- | --- |
| 0 | 5 | TUBEPAIR / TubePair | magicDistort / 0 |
| 1 | 7 | IRON / IronTransformer | magicDistort / 2 |
| 2 | 0 | CRUNCH / Crunch | distort / 0 |
| 3 | 1 | HEAVY / Heavy | distort / 1 |
| 4 | 2 | ZAP / Zap | distort / 2 |
| 5 | 6 | AIR / AirPressure | magicDistort / 1 |

Config.lua:291 selects **internal type 5**, so the resource default selects
TubePair local shape 0. The normalized host enum is backed by this option
spec; internal IDs must not be treated as six consecutive values. The image
strip map at FX_GUIBoot.lua:289–301 uses yet another order and even contains an
entry for internal type 3; image indexing is not the type option list.

Internal type 3 has active 850 Hz pre/post EQ and the shared 0..3 gain rows,
but **no active distort or magicDistort row** in this resource. Type 4 has only
the global -12/+12 dB pair. Neither 3 nor 4 appears in DistTypeSpec. Existing
native ShapeMono/ShapeStereo<3> functions do not establish that either missing
user type routes to them.

There is another default at the numerical factory boundary: native
cAEMagicDistortion's constructor (0xae170) stores local shape **3** at +0x604
(0xaeaf3). SetParam index 8 updates that field at 0xaf503. This is overridden
by the resource-selected local shape and must not be used as the RC-20
TubePair default.

## Exact plug chain declaration

`plugChainDist` is the text at FX_GUIBoot.lua:381–574. Every active row is
conditional on Enable and its stated inclusive Type range. Semicolon-prefixed
rows are comments and do not execute. The declarations below preserve the
literal mapping labels `lin`, `log`, `sym`, and parenthesized `(Amount)`;
these labels require the native chain parser's exact conversion behavior.

The chain starts with -12 dB for all Type 0..7, and ends with +12 dB for 0..7.
Between those endpoints:

| Type | Ordered active operations |
| --- | --- |
| 0 Crunch | gain Amount -19→0 dB; bpPeak 900 Hz, Q .4, (Amount) +10→+12 dB; distort local shape 0; bpPeak 900 Hz Q .4, (Amount) -10→-12 dB; gain Amount +19→0 dB |
| 1 Heavy | gain Amount -19→0 dB; bpPeak 900 Hz Q .4, (Amount) +10→+12 dB sym .15; distort local shape 1; bpPeak 900 Hz Q .4, (Amount) -10→-12 dB sym .15; gain Amount +19→0 dB |
| 2 Zap | gain Amount -19→0 dB; bpPeak 850 Hz Q .2, (Amount) +10→+16 dB lin; distort local shape 2; bpPeak 850 Hz Q .2, (Amount) -10→-16 dB lin; gain Amount +19→0 dB |
| 3 unexposed | gain Amount -19→0 dB; bpPeak 850 Hz Q .2, (Amount) +10→+16 dB lin; bpPeak 850 Hz Q .2, (Amount) -10→-16 dB lin; gain Amount +19→0 dB |
| 4 unexposed | no operations between the global -12/+12 dB rows |
| 5 TubePair | gain Amount 0→+22 dB; magicDistort local shape 0; gain Amount 0→-22 dB |
| 6 Air | magicDistort local shape 1 |
| 7 Iron | gain Amount 0→+18 dB sym .4; magicDistort local shape 2; gain Amount 0→-18 dB sym .4 |

Engine-specific declarations are:

| Type | amount | refLevel | distDB | distLP |
| --- | --- | --- | --- | --- |
| 0 | Amount (implicit engine range) | -10 dB | (Amount) 0→-3 dB | (Amount) 20000→5000 Hz |
| 1 | Amount 0→100 sym .15 | -15 dB | (Amount) 0→-3 dB sym .15 | (Amount) 20000→7000 Hz sym .15 |
| 2 | Amount (implicit engine range) | Amount 0→-6 dB | (Amount) 0→-1 dB | (Amount) 10000→3000 Hz |
| 5 | Amount (implicit engine range) | engine default | engine default | engine default |
| 6 | Amount 0→75 lin | engine default | engine default | engine default |
| 7 | Amount 0→100 sym .4 | engine default | engine default | engine default |

Each of the six actual distortion rows maps lowFreq from FreqLo and highFreq
from FreqHi with **20→20000 Hz log** scaling. Config.lua:316–317 supplies
normalized defaults zero/one for those controls. All per-engine `mix Mix`
rows are commented; outer graph mixing supplies the user's Mix. Likewise the
`passDB -100` rows are comments; recreating them as active band-pass cuts would
change the declared graph. Native engine defaults and filter behavior must
establish how the out-of-band signal is handled.

The global -12/+12 dB and the other bracketing gain pairs do not cancel through
a nonlinear engine. They affect both drive level and gain compensation.
Pre/post EQ cancellation is also not equivalent to omitting those filters.

## Amount, Mix, Flux and external routing

Config.lua:174, :290–317 and :546–582 establish the ordinary macro path:

```text
amount_scaled = MasterMacroScaler * MacroDistAmount
Dist_Amount = Dist_Amount_Unscaled * amount_scaled
Dist_Amount_Unscaled default = 1
Dist_MixFromAmount = FromTo(Dist_Amount, 0,1, 0,1, .99)
Dist_MixInternal = Dist_Mix * Dist_MixFromAmount
```

Dist_Mix defaults one. Dist_Enable and MacroDistEnable default zero; generic
wiring copies the macro enable into the block enable. Frequency and Mix
controls do not carry macroType and are not directly scaled by Magnitude.

The group Flux path is shared with the earlier effects:

```text
group_flux = (MasterMacroScaler * Dist_Flux) * C(MacroDistAmount,.99)
```

Dist_InGain_Feed defaults .5 and declares one bipolar noise generator with
Chaos 0, Decay .405, Smooth .913, local Flux .16, and additive ModStyle .75.
It sets disableFGainScaling=true, so FX_AudioBoot.lua does not multiply the
generator gain by group_flux. Instead, explicit `FromToKnob` relations set:

| Generator coefficient | Low / middle / high output anchors | Half curves |
| --- | --- | --- |
| gain | 0 / .16 / .36 | .5 / .5 |
| smooth | .913 / .913 / .1 | .5 / .5 |

Original Drive multiplies generator output by 16. The model feed is then
`clamp(.5 + noise, 0,1)`. The pre/post connections use Dist_InGain and
Dist_OutGain, derived by `syncKnob2AnyGain` from that feed, respectively with
arguments `(-3,20,.5,.5)` and `(3,-20,.5,.5)` (Config.lua:295–296). The embedded
helper emits opcode `'K'`, with low/high gain parameters and two curvature
parameters. Recover native K's dB/piecewise conversion and exact float rounding
before implementing these controls; treating these as an ordinary linear
-3→20 dB ramp would be an unverified substitution. The intended reciprocal
pair must also preserve actual native arithmetic.

FX_GUIBoot.lua:806–814 gives the container-level routes:

```text
Wobble.out -> DistMix.in : !Dist_MixInternal * Dist_Enable
Wobble.out -> Digital.in : !Dist_Enable
Wobble.out -> Dist.in    : Dist_Enable * Dist_InGain * Dist_MixInternal
Dist.out   -> DistMix.in : Dist_Enable * Dist_OutGain
DistMix.out -> Digital.in
```

The graph expression `!` denotes its complement convention, not C boolean
negation. The wet Mix multiplier is applied **before** the nonlinear chain.
Therefore the full effect cannot automatically be replaced by a conventional
post-distortion dry/wet interpolation. The routing/gain engine should establish
sample smoothing and summation order before claiming full-chain equivalence.

## Isolated numerical boundaries

The symbol table identifies two engines and a chain adapter:

| Boundary | Address |
| --- | --- |
| scAEPlugChain::CreateFromString | 0x9f6a0 |
| scAEPlugChain::UpdateParamFromXDParam | 0x9f500 |
| scAEPlugChain::ResetAudio / SoftResetAudio / Process | 0xa3f30 / 0xa3fb0 / 0xa4000 |
| cAESingleBandDistortion::SetParam / CalcInternals / Process | 0xa5d80 / 0xa6080 / 0xa5e30 |
| SingleBand ResetAudio / SoftResetAudio | 0xa5d10 / 0xa5d50 |
| cAEMagicDistortion::SetParam / CalcInternals / Process | 0xaf4a0 / 0xafc70 / 0xaf570 |
| Magic ResetAudio / SoftResetAudio | 0xaef90 / 0xaf360 |
| TubePair setup / per-sample process | 0xb1ab0 / 0xb2550 |
| AirPressure setup / reset / per-sample process | 0xb1cf0 / 0xafa80 / 0xb22f0 |
| IronTransformer setup | 0xb1f60 |

Magic mono shape functions 0/1/2/3 are at
0xb02b0/0xb0440/0xb05d0/0xaff50; stereo equivalents are at
0xb0f30/0xb1200/0xb14d0/0xb0890. Classic mono shapes 0/1/2/3 are at
0xa63a0/0xa6760/0xa6b30/0xa6f20; stereo equivalents at
0xa7290/0xa7960/0xa8020/0xa8770. SingleBand also has SplitMono4/SplitStereo4
at 0xa8de0/0xa8fc0, useful boundaries for focus-filter reconstruction.

TubePair setup/per-sample process is a feasible first independent numeric
milestone because it isolates its small filter histories, nonlinear function
and scalar gain conversion. It does not include global attenuation/boost,
Amount's extra ±22 dB gain pair, focus filters, outer Mix routing or Flux.
Air and Iron require their own stateful boundaries; a single static waveshaper
cannot stand in for all three. Classical Crunch/Heavy/Zap additionally need
their pre/post peak EQ, range mappings and split-filter implementations.

## Remaining questions

Native chain mapping must establish parenthesized `(Amount)`, implicit ranges,
`lin` versus `sym` curve meanings and enable-transition processing. The K
relation requires direct native validation. Global routing smoothing/order,
focus-band pass-through behavior and engine latency/reset semantics remain
separate from numerical primitive recovery. Production Flux seed indices and
complete original notification ordering are not supplied by these resources.
