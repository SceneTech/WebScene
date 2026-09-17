# Generated native CSS grammar metadata — 2026-09-17

## Scope

This stage extends `native/css_property_metadata.json` so the two native
specified-value implementations share the property-family classification used
for their identical simple parsing paths. It follows the generated native
property identity merged in PR #316 at `486e7931`.

The catalog now classifies every one of the 144 typed property IDs exactly once:

| Family | Properties | Handling |
| --- | ---: | --- |
| keyword | 29 | generated dispatch |
| component list | 15 | generated dispatch |
| length | 34 | generated dispatch |
| length list, up to four values | 5 | generated dispatch |
| length list, up to two values | 10 | generated dispatch |
| color | 9 | generated dispatch |
| complex or path-specific | 42 | explicit implementation switch |

The 102 simple properties return through an O(1) generated grammar table in
both `webscene_css_specified_ir.h` and the retained
`webscene_css_specified_value.h`. The 42 complex properties deliberately remain
explicit. In particular, `contain`, `cursor`, `font-family`, `fill`, and
`stroke` differ between the live and retained representations and are not
incorrectly generalized.

This change centralizes classification, not standards grammar. It does not
claim to make a permissive existing parser stricter, generalize complex
shorthands, or alter cascade, inheritance, initial values, or computed-value
serialization.

## Generator invariants

The generator rejects:

- a missing or extra grammar family;
- a family value that is not an array of property IDs;
- an unknown property ID;
- classification of the special `unknown` or `custom` IDs;
- duplicate classification; and
- any typed property ID without a classification.

The ordered classification fingerprint is
`e1b6e60b2a079e9b6fb6856cb4ad10b3a51ee8d13610a63f8a6cfcb913a98d06`.
The generated table is indexed by the stable `css_property_id` ABI and bounds
checks out-of-range input to the special family.

## Regression coverage

The current and retained-header identity tests independently compile their
specified-value implementations and exercise all 102 generated simple-family
entries with a family-appropriate value. Each must produce the expected typed
kind and a valid result.

The live-path test additionally verifies:

- exactly 144 unique samples cover every typed property ID;
- every sample compiles to a fully typed specified value;
- every specified-value kind encodes and decodes successfully;
- the decoded kind matches the compiled kind;
- re-encoding is byte-for-byte stable; and
- each simple-family sample agrees with its generated grammar classification.

Five previously missing samples were added for `container`, `container-type`,
`container-name`, `content-visibility`, and `contain-intrinsic-size`.

## Performance evidence

An AppleClang Release microbenchmark compiles 24 representative simple-property
values 2.4 million times per sample. Baseline and candidate use the same source,
toolchain, SDK and checksum and were run in ten interleaved A/B pairs.

| Build | Median | Samples (ms) |
| --- | ---: | --- |
| merged #316 (`486e7931`) | 493.674 ms | 484.782, 605.985, 565.706, 477.110, 466.196, 502.566, 459.562, 458.701, 521.890, 503.045 |
| generated grammar dispatch | 491.505 ms | 544.126, 621.622, 494.087, 481.503, 475.781, 465.705, 472.348, 488.923, 536.271, 506.966 |

The candidate median is 0.44% lower (1.004x throughput) with identical checksum
`1085102592571149948`. The result is effectively neutral and is recorded to
show that structural centralization did not add a measurable regression. It is
a specified-value microbenchmark, not a Spotify frame-time claim.

## Validation

- generator unit tests: 11/11; generated-output `--check`: pass;
- current-parser native engine build: pass;
- legacy-parser native engine build: pass;
- CSS parser CTest: pass;
- current and retained-header identity CTests: 2/2 in both native build trees;
- `cssom-inline-declaration-validity`: 5/5 current parser and 5/5 legacy parser;
- selected NativeWeb CSS service, shared-style, and compiler CTests: 3/3.

An accidental unfiltered NativeWeb `ctest` invocation selected targets that had
not been built and is not treated as product evidence. The corrected target
filter above passed all three intended tests.

## Remaining epic boundary

This completes the generated grammar-family slice, not issue #235. The remaining
structural work is a cross-runtime completeness audit across exposure, typed
recognition, effective masks, inheritance/initial metadata and computed
serialization, plus the cold V8 `CSSStyleDeclaration` template guard. The final
product gate remains a freshly assembled SDK running the unchanged Spotify
catalog through continuous physical resize and the small-to-large media-query
transition against the same Chrome build.
