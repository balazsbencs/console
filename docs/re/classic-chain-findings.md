# Classic Distortion chain mappings

These findings settle the audio mappings needed around Crunch, Heavy and Zap.
Sources are decoded `analysis/unpacked/guispec/FX_GUIBoot.lua:381–574`,
Config.lua:290–317, retained REA records and read-only native raw disassembly.
No complete original constructor, resource script or plugin was run.
Reference SHA-256:
`f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a`.

## Parenthesized Amount

The parser recognizes a value beginning `(` and ending `)` at 0xa0987 and
0xa09a2, removes those two characters, and resolves the resulting model name
through the same lookup as an unparenthesized name. It sets a flag at 0xa09eb,
then stores it at **link-node+0x49** at 0xa0cf5. Thus `(Amount)` names the same
Amount source, rather than a second model, negative Amount, an inverted knob,
or a request to feed physical engine Amount into another mapping.

The runtime audio update at **0x9f500** does not inspect that flag. It passes
node+0x24 to ScaleToPlugParam at **0x9f090**, then publishes the returned value
to the engine's SetParam. The scaler likewise never reads the flag (relative
link+0x25). Consequently `(Amount)` and `Amount` have identical numerical
mappings for a given source value and declared range/scale.

An isolated original-scaler fixture swept 15 actual mapping families over
1,001 inputs each and toggled that stored flag: **15,015 comparisons were bit
identical**. This supports the numerical conclusion, not an invented name for
the flag's historical metadata/UI purpose. A complete historical purpose for
that otherwise numerically unused marker is unnecessary for audio equivalence
and remains outside this finding.

Raw evidence: `analysis/re/chain-create-raw.asm` at 0xa0953–0xa0ad8 and
0xa0c8a–0xa0cf5, plus `chain-update-raw.asm` and
`chain-scale-param-raw.asm`. The incomplete REA CreateFromString pseudocode
stops at an allocation import; the relevant parsing observations come from
actual decoded instructions, not that truncated output.

## lin, omitted scale and sym

Each linked row stores a **source range of 1** at node+0x2c (0xa0cb7). Values
therefore consume normalized Amount. The parser stores destination endpoints
at +0x38/+0x3c, scale enum at +0x40, curvature at +0x44, and legal destination
parameter limits at +0x30/+0x34. ScaleToPlugParam receives the link portion at
node+0x24, so its corresponding offsets are +8, +0x14/+0x18, +0x1c, +0x20
and +0x0c/+0x10.

Scale enum **0** is linear, including these rows with omitted scale:

```text
mapped = ((high-low)*source)/float32(source_range) + low
```

Enum **5**, selected by the literal `sym`, is the single rational curve:

```text
x = source / float32(source_range)
numerator = x*k
denominator = (k+k+(-1))*x
denominator = denominator+1
denominator = denominator-k
mapped = (high-low)*(numerator/denominator)+low
```

Native output is then limited to the intersection of the endpoint interval and
the legal parameter interval. This is output clamping, not an implicit input
clamp. For normal Amount in [0,1], all classic resource endpoint intervals are
valid. In particular `sym .15` is **not** a two-half symmetric knob mapping:
it bends the whole 0..1 interval. It is also distinct from opcode K's two-half
dB gain conversion and from FromToKnob.

Existing recovered rational/from-to helpers can supply this arithmetic when
controls stay in [0,1], provided float operation order and endpoint limits are
preserved. Avoid replacing Heavy targets with precomputed linear multiples of
.15; native division rounding is visible at the midpoint.

## Effective source and ordered chain

Let `A = Magnitude * Amount`, with normalized user Amount. Config.lua gives
Dist_Amount's unscaled default one, so settled Dist_Amount equals A. All rows
below use that same A, including parenthesized rows. Character IDs in this
table are **internal resource IDs**, not the study adapter's local 0..2 enum:

```text
Type 0 Crunch / 1 Heavy / 2 Zap:
  gain -12 dB
  gain Map(A,-19,0,lin)
  pre peak EQ
  classic distortion engine
  post peak EQ
  gain Map(A,19,0,lin)
  gain +12 dB
```

Each gain row is a separate persistent gain processor. Each pre/post EQ row is
also a distinct stateful processor. Pre/post opposite dB numbers must not be
collapsed, and bracketing gain rows must not be algebraically canceled around
the nonlinear engine.

| Resource type | Pre EQ | Post EQ |
| --- | --- | --- |
| 0 Crunch | peak 900 Hz, Q .4, Map(A,10,12,lin) dB | same frequency/Q, Map(A,-10,-12,lin) dB |
| 1 Heavy | peak 900 Hz, Q .4, Map(A,10,12,sym .15) dB | same frequency/Q, Map(A,-10,-12,sym .15) dB |
| 2 Zap | peak 850 Hz, Q .2, Map(A,10,16,lin) dB | same frequency/Q, Map(A,-10,-16,lin) dB |

The 850 Hz pair also has an inclusive internal Type 2..3 selector. Internal
Type 3 is unexposed and lacks an active distortion engine row in this resource;
its presence does not add a fourth classic character to the front panel.

## Engine parameter targets

| Engine input | Crunch shape 0 | Heavy shape 1 | Zap shape 2 |
| --- | --- | --- | --- |
| amount (physical percent) | Map(A,0,100,lin), from native implicit range | Map(A,0,100,sym .15) | Map(A,0,100,lin), from native implicit range |
| refLevel dB | -10 | -15 | Map(A,0,-6,lin) |
| distDB dB | Map(A,0,-3,lin) | Map(A,0,-3,sym .15) | Map(A,0,-1,lin) |
| distLP Hz | Map(A,20000,5000,lin) | Map(A,20000,7000,sym .15) | Map(A,10000,3000,lin) |
| passDB dB | factory default 0 | factory default 0 | factory default 0 |
| mix percent | factory default 100 | factory default 100 | factory default 100 |

The apparent `mix Mix` and `passDB -100` resource lines are comments. Activating
them would implement a different effect. lowFreq and highFreq each receive
their own FreqLo/FreqHi model controls through 20→20000 Hz logarithmic mapping.
A study API that already accepts physical Hz can pass those values directly,
but should not claim the original GUI knob interpolation is thereby hosted.
The classic engine handles crossed frequency values using its recovered max
rule, separately from any GUI constraints.

Native midpoint observations (A=.5):

| Target | Crunch | Heavy | Zap |
| --- | --- | --- | --- |
| Engine amount | 50 | 15.000001907348633 | 50 |
| Pre EQ dB | 11 | 10.300000190734863 | 13 |
| Post EQ dB | -11 | -10.300000190734863 | -13 |
| refLevel dB | -10 | -15 | -3 |
| distDB dB | -1.5 | -0.4500000476837158 | -.5 |
| distLP Hz | 12500 | 18050 | 6500 |

All three common pre/post gain targets at A=.5 are -9.5/+9.5 dB, in addition
to the global -12/+12 dB endpoints. More retained native samples are in
`analysis/re/classic-chain-mappings.json`.

## External Mix and Flux

The same recovered Dist graph surrounds classic paths:

```text
wet_mix = Mix * FromTo(A,0,1,0,1,.99)
wet_input = input * (flux_input_gain * wet_mix)
wet_output = selected classic chain(wet_input)
output = input*(1-wet_mix) + wet_output*flux_output_gain
```

The single Dist Flux generator, FromToKnob gain/smoothing maps, additive feed
and opcode K reciprocal gain conversion are shared with the three-character
Magic chain. They are documented in dist-chain-integration.md and
knob-gain-findings.md. Wet Mix multiplies input **before** EQ and distortion.
A standard post-distortion mix would alter the signal driving the nonlinearity.

## Evidence and remaining boundaries

| Retained record | Evidence ID |
| --- | --- |
| chain-create.json | ev_9621496c418856c274f22f6343f7a7ea4e73fae2c021eebe06107af1e4c17909 |
| chain-scale-param.json | ev_818012f9609c63486d575ddcf888314b8abcd7c3d77d20b8e974cc8c252d016d |
| chain-update.json | ev_396f3a042adae64cd45135f95d5d6f838f20258d6a96fa3a010a1822f399c82b |

The parent REA session can query 0x9f6a0, 0x9f090 or 0x9f500 for further
facets, but no additional query is required for these scalar audio mappings.
Peak-EQ numerical filtering and coefficient/history behavior require their own
native evidence; the separate peak investigation owns that work. Original
connection gain transitions, update notification order, complete graph
summation and production Flux seed index remain unresolved by these settled
mapping observations. No full-chain original-host equivalence is claimed.
