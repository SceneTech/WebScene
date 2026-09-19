# Generated CSS property identity validation (2026-09-17)

Follow-up additions must preserve the 105-property managed ID prefix and the
146-entry native ID prefix recorded here. The 2026-09-19 contract repair appends
`aspect-ratio` after those prefixes and is documented in
`css-property-metadata-contract-20260919.md`.

This stage of #235 removes the remaining independent managed and native
CSSStyleDeclaration property lists. It builds on the effective-property registry
merged in #309; it does not alter the selector timing, containment, or stylesheet
finalization work owned by #238 and #297.

## Structural result

`experiments/WebScene.NativeEngine.Probe/native/css_property_metadata.json` is now
the checked-in source for three related property views:

- 105 stable managed storage properties, in ID order;
- 95 additional CSSOM-supported names; and
- 61 effective box-property entries used by native cascade masks and expansion.

The generator validates and emits:

- the existing 61-entry native effective-property table;
- a 214-entry native CSSOM table containing canonical CSS names and precomputed IDL
  names; and
- managed arrays for stable storage IDs and the same 214 supported CSSOM names,
  plus the direct name-to-ID switch.

The native V8 template previously maintained 152 handwritten IDL names separately
from the managed catalog. It now consumes the generated CSS/IDL pairs. The pairs
are compile-time string views, so template setup does not allocate strings to
convert 214 property names at runtime. Six compatibility names that existed only
in the native list (`animation`, `box-shadow`, `contain`, `grid-row-gap`,
`grid-column-gap`, and `webkit-font-smoothing`) were retained in the shared catalog.

Managed direct-storage IDs remain byte-for-byte ordered as before. The generator
test pins all 105 names with a SHA-256 sequence fingerprint in addition to checking
the generated outputs. Case-insensitive access still uses the existing frozen-map
fallback, while exact lower-case access retains the direct switch.

The shared union also closes an observed CSSOM drift: 14 logical effective names,
including `inset-block-end`, `border-block-width`, `border-inline-color`,
`padding-block-start`, and `margin-block-end`, were understood by native style
application but absent from one or both named-property exposure lists.

The generator itself is now explicitly tracked. The repository's broad `tools/**`
ignore rule had excluded the script introduced with #309 even though CI invoked it;
the new `.gitignore` exception prevents clean checkouts from losing that build input.

## Browser-shaped regression

`tests/WebPlatformSubset/contracts/cssom-inline-declaration-validity.html` now has a
fifth subtest covering named exposure and assignment for logical inset, border,
padding, and margin properties. Results:

- Google Chrome 153.0.8010.48: 5/5;
- native current CSSParser: 5/5; and
- native legacy parser: 5/5.

The native engine initially failed the new subtest because `inset-block-end` was
missing from its handwritten V8 template list. Replacing that list with generated
metadata is the regression fix; adding only managed tests would not have caught it.

An adjacent native `cssom-` sweep passes all nine CSS-specific documents. The filter
also selects `dom-document-cssom-hosted-page-primitives.html`; its unrelated
`document.cookie` assertion failed both with default and fresh storage, leaving the
combined sweep at 9/10 documents and 60/61 subtests. No property-exposure assertion
failed.

## Build and test evidence

- metadata generator tests: 6/6, followed by a clean `--check`;
- WebScene.Css tests: 383/383 on .NET 8 and 383/383 on .NET 10;
- full `WebScene.sln` Release build: success with 16 pre-existing nullable warnings
  and zero errors;
- full managed solution tests: 1,774 passed, 30 platform-dependent tests skipped;
- current-parser native CSS parser CTest: 1/1;
- NativeWeb CSS service/shared-style/compiler CTests: 3/3; and
- both current- and legacy-parser native engine libraries rebuilt successfully.

This stage centralizes identity and exposure, not all CSS semantics. Property initial
values, inheritance, serialization, grammar selection, complex shorthand expansion,
origins, and CSS-wide keyword behavior remain later metadata slices in #235.

Machine-readable evidence is in
`docs/validation/evidence/css-property-identity-20260917.json`.
