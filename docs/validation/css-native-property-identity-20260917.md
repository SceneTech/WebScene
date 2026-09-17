# Generated native CSS property identity — 2026-09-17

## Scope

This stage extends `native/css_property_metadata.json` from CSSOM exposure and
effective box-property metadata to the native typed-property identity used by
specified-value compilation. It does not change cascade origins, value grammar,
serialization, or complex shorthand semantics.

Before this change, `webscene_css_specified_ir.h` and the retained
`webscene_css_specified_value.h` each declared the same 146-value enum and
maintained independent name/alias chains. The live IR path recognized 201 names;
the retained header recognized 192 and had already lost nine compatibility
aliases. Both paths allocated a lower-case `std::string` for every lookup,
including already-normalized parser input.

## Structural result

The checked-in catalog now owns:

- the exact ordered 146-value `css_property_id` ABI, including `unknown` and
  `custom`;
- 201 canonical names and aliases mapped to those IDs; and
- 54 explicitly storage-only names among the 214 CSSOM-exposed names.

The generator emits the enum body, typed/storage-only classification tables,
and the lower-case direct lookup. Both specified-value headers consume those
outputs. Generator validation rejects duplicate IDs or aliases, invalid special
IDs, unknown storage-only names, typed/storage overlap, and any exposed CSSOM
name without exactly one native classification. SHA-256 tests pin the ordered
ID, name-to-ID, and storage-only sequences.

The property entry point now scans for ASCII uppercase and only allocates for
that compatibility path. Already-normalized lower-case parser input goes
directly to the generated lookup without allocation. Custom properties retain
their case-sensitive early return.

## Regression coverage

`webscene_css_property_identity_tests` and
`webscene_css_legacy_property_identity_tests` compile the two headers
independently and verify:

- stable numeric sentinels (`unknown=0`, `custom=1`, final ID `145`);
- all 201 names and aliases, including ASCII-uppercase lookup;
- all 54 storage-only names remain untyped;
- all 214 CSSOM names have exactly one typed/storage-only classification;
- custom and unknown behavior; and
- zero allocations across the complete lower-case typed-name catalog.

Compiling the retained header independently exposed an existing invalid
`const` mutable lambda; the local `const` qualifier was removed without changing
the splitting algorithm.

## Performance evidence

An AppleClang Release benchmark performs 24 million representative lower-case
property-ID lookups. The same benchmark source, compiler, SDK, architecture and
checksum were used against merged `main` (`b69f931d`) and this change. Eight
interleaved A/B samples produced:

| Build | Median | Samples (ms) |
| --- | ---: | --- |
| merged `main` | 6,223.195 ms | 6164.480, 6146.020, 6105.050, 6212.720, 6422.480, 6473.780, 6233.670, 6237.290 |
| generated allocation-free lookup | 346.289 ms | 340.123, 338.446, 345.610, 342.323, 382.502, 361.756, 351.289, 346.967 |

The median is 94.44% lower (17.97× throughput) with the identical checksum
`8878625900673316631`. This is a property-lookup microbenchmark, not a claim
about end-to-end Spotify frame time.

## Validation

- generator unit tests: 9/9; generated-output `--check`: pass;
- current and retained-header identity CTests: 2/2 in both current-parser and
  legacy-parser build trees;
- current native engine build: pass;
- legacy-parser native engine build: pass;
- current CSS parser CTest: pass;
- `cssom-inline-declaration-validity`: 5/5 current parser and 5/5 legacy parser.

The full CSS epic remains open. Grammar/serialization metadata, the broader
cross-runtime audit, cold V8-template setup measurement, coordinated cascade and
stylesheet-finalization work, and matched physical Spotify/Chrome qualification
remain separate stages.
