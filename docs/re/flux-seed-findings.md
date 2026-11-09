# Flux generator seeds and record ordering

Scope: static examination of the supplied, hash-pinned RC-20 1.0.5 x86_64
Mach-O and extracted Lua. No new REA session, original resource-script execution
or full-plugin construction was performed. Native primitive reset behavior was
previously runtime-verified by `tools/compare_native_noisegen.py`.

## Established from native assembly

The Mach-O symbol table identifies
`cRelationSpecHandler::relationSpecUpdated(char const*, shared_ptr<...>&)` at
**0xf1b30**. Its raw disassembly is retained in
`analysis/re/relation-spec-raw.asm`. This is direct local objdump evidence,
separate from REA's retained decompilation records.

Each noise generator's seed is the **zero-based index of its relation record in
the complete serialized relation specification**. It is not the ordinal among
noise generators and is not a parameter-ID hash. Relevant instructions:

| Address | Observation |
| --- | --- |
| 0xf1cab | reset total parsed-record count at handler+0x15028 to zero |
| 0xf6db5 | load signed current record count from handler+0x15028 into RSI |
| 0xf6dbd | compute byte displacement `record_index * 0x70` |
| 0xf6dc7 | store ESI at handler+displacement+0x40, i.e. record+0x20 |
| 0xf6dcc | pass record+0x28 as NoiseGen state pointer in RDI |
| 0xf6dd1 | call `NoiseGen_Reset(state, record_index)` at 0x1a5980 |
| 0xf8476–0xf8480 | increment total record count after each parsed relation |

Noise cases `N` and `O` share the parser dispatch at 0xf7ea2; the dispatch table
is at 0xfc2c0. Their mode distinction is retained in record type at record+0x60.
The handler's main parser writes the operation type at 0xf7830 and dispatches
it at 0xf7854. The total-record count increments for other relation types too,
so removing unrelated relations changes subsequent generator seeds.

Reset's runtime-verified formula is:

```c
history[0] = history[1] = history[2] = 0;
rng_x = 0x67452301;
rng_y = (uint32_t)record_index * 0x401 ^ 0x90325477;
```

The stored seed remains at record+0x20. `Drive` resets the same generator from
that stored seed when the pending-reset flag at record+0x1c is nonzero, then
clears the flag. See `analysis/re/relation-drive.c`, lines 29–31, backed by REA
Evidence `ev_43d783c22bb268386cf43ec47008a84343587b315ff459f6d5da36becdac41e7`.
The exact Drive math and cadence are a separate investigation.

## Established linked-list order

At specification replacement the noise-list head at handler+0x15020 is cleared
(0xf1b4e). Noise initialization then appends the current record to the list:

- 0xf6de2 loads the previous tail held in a local.
- 0xf6dec selects previous-tail+0x68, or head pointer for an empty list.
- 0xf6df0 computes the current record pointer.
- 0xf6e07 writes that pointer into the selected list link.
- 0xf6e0a clears the current record's next pointer at record+0x68.

Thus Drive traversal follows noise records in **serialized encounter order**,
with no sorting or reverse insertion at this point. `Drive` starts from the
head and follows record+0x68 (`relation-drive.c:14–15`). This does not establish
ModelStore callback propagation order or synchronous dependency evaluation.

## Digital seed values remain unresolved

The resource script appends modulation and generator relations inside
`for param,val in pairs(stateAndPresetParams)`
(`analysis/unpacked/guispec/FX_AudioBoot.lua:31–82`). Other per-parameter
relations are appended in the same loop (`:85–115`). Earlier Config code also
appends relations when iterating tables with `pairs()`
(`analysis/unpacked/guispec/Config.lua:525–546`, `:563–610`, `:652–660`).

Consequently file declaration order cannot establish the final serialized
record index of either Digital_BitDepth or Digital_DegradeSR generator. The
Digital bit and rate declaration positions alone must not be used as seeds.
Lua's actual table iteration order, prior inserted controls and the serializer's
output all matter. No emitted AudioThreadSyncSpec string or complete constructed
handler snapshot was found among the existing supplied evidence consulted here.

To establish exact Digital seed integers, capture either the original serialized
specification or the constructed handler records, preserving every relation
record before those generators. Match records to their destination model IDs,
then use their complete-list indices. This would also settle relative Digital
bit/rate generator order. Recreating only the two noise records necessarily uses
different seeds unless their original indices are explicitly preserved.

Confidence ledger: seed derivation and append order are statically established;
NoiseGen_Reset formula is independently runtime-verified; exact Digital seeds,
whole-spec ordering across launches and full-plugin reproducibility remain
unknown. No source changes were made for this investigation.
