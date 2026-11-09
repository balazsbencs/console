# Settled Digital Flux native comparison

`tools/compare_native_flux.py` compares the C `rs_digital_flux` adapter against
original isolated native relation arithmetic and `cRelationSpecHandler::Drive`
(0xd6fd0), followed by original normalized stereo cBitColor processing. It
reuses the hash-pinned Mach-O mapping and math bindings, without plugin entry
points, constructors or resource scripts.

The synthetic ModelStore supplies get/set callbacks. Two linked records of
type 0x4f hold independent noise states, explicit stored seeds, reset flags and
cached update rates. The harness uses the original relation handler to compute
curved triple Flux scaling, its rate smoothing conversion, local generator
gains, triangle balance, macro multiplication and style-0.75 modulation. Native
objectChanged notifications explicitly invalidate generator coefficient caches
before Drive. This is **settled input verification**, not a reconstruction of
the production notification scheduler.

The C side calls `rs_digital_flux_tick` once per 64-frame batch, compares both
scaled noise outputs, both complete 40-byte noise states and the normalized
targets, then drives the C audio engine and original cBitColor with the matched
targets. Two resets per scenario exercise the original Drive pending-reset
path alongside the C reset API; audio states also reset together.

Verified Release results in `analysis/re/native-flux-comparison.json`:

- 12 scenarios: 44.1/48/96 kHz and four explicit seed pairs.
- 3,072 changing-control ticks, including zero Amount/Magnitude/Flux and balance
  endpoints; 6,144 noise outputs and 6,144 normalized targets.
- All 80 generator-state bytes matched at every tick.
- 393,216 composite stereo samples matched exactly.
- Maximum control error 0; maximum audio error 0.

Reproduce from the repository root:

```sh
python3 tools/compare_native_flux.py --library build-release/libretro_dsp.so
```

`--report` accepts an alternate result JSON path. The default report path is
`analysis/re/native-flux-comparison.json`.

Limits: explicit seed pairs are synthetic and do not establish the original
Digital generators' production relation indices. Parameter propagation is
settled manually. No full original ModelStore scheduling, host automation,
resource boot order or complete-plugin audio execution is represented. Native
math imports use Linux system libm. Composite audio uses the current normalized
Focus=1 static defaults for controls outside the changing macro/Flux targets.
