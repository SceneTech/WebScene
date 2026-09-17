# Generated effective-property metadata validation (2026-09-17)

This stage of #235 moves the effective-property knowledge introduced by #305
out of cascade-specific conditionals and into a generated registry. The source
catalog is `experiments/WebScene.NativeEngine.Probe/native/css_property_metadata.json`;
`tools/generate_css_property_metadata.py` validates it and emits the sorted,
constexpr native table.

## Structural result

The registry currently contains 61 physical, logical, and shorthand names for
margin, padding, inset, border width/color, gap, and overflow. Each row owns its
modeled style mask and zero, one, two, or four effective longhands. Cascade
rollback and property-mask lookup now consume that shared data instead of
maintaining separate property-name lists.

Six logical shorthands whose prior ordinary application path lost their second
component are marked for application-time expansion: `inset-block`,
`inset-inline`, `border-block-width`, `border-inline-width`,
`border-block-color`, and `border-inline-color`. Other common shorthands retain
their existing direct application path; high-frequency direct properties such
as width, display, background, color, font, and transform still return before a
metadata lookup. Expansion remains allocation-free until one of those six
logical shorthands or the rare rollback path is actually used.

The generator rejects duplicate names, unknown effective targets, invalid mask
symbols, invalid expansion arity, and stale checked-in output. CI runs both its
unit tests and `--check` mode.

## Browser and native behavior

The browser-referenced effective-property contract was extended from 12 to 15
subtests. The three additions cover logical inset rollback, logical border
width/color rollback, and two-value inline CSSOM application followed by a
recascade.

- Chrome 153.0.8010.48: 15/15
- native current CSSParser: 15/15
- native legacy parser: 15/15
- unchanged cascade winner matrix: 17/17
- adjacent all/margin/CSSOM contracts: 21/21
- NativeWeb CSS service/shared styles/compiler: 3/3 CTest

The cascade-layer mutation gate retains exact work invariance when unrelated
nodes grow from 32 to 1,024:

| affected | unrelated | selector candidates | fallback visits | rule checks | applications | cascade candidates |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 32 | 16 | 0 | 32 | 18 | 32 |
| 8 | 1024 | 16 | 0 | 32 | 18 | 32 |
| 128 | 32 | 256 | 0 | 512 | 258 | 512 |
| 128 | 1024 | 256 | 0 | 512 | 258 | 512 |

## Lookup A/B

`webscene_css_property_metadata_benchmark` performs 24,000,000 lookups per run
over a fixed mix of 15 direct hot properties and nine effective-property
entries. For the retained A/B, the same benchmark source and AppleClang 21
`-O3` command were compiled once against merged #305 and once against this
branch. Nine alternating runs produced:

- merged #305 median: 502.446 ms (495.039–521.802 ms)
- generated registry median: 424.271 ms (418.068–440.586 ms)
- median change: -15.56%

This microbenchmark establishes that centralization did not trade maintainable
metadata for slower property dispatch. It is not an end-to-end Spotify resize
claim; physical resize and media-threshold acceptance remains later work in
#235.

Machine-readable evidence is in
`docs/validation/evidence/css-property-metadata-20260917.json`.
