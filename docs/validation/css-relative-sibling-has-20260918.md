# Relative sibling `:has()` validation — 2026-09-18

## Scope and provenance

- Initial WebScene base: `2f0ae98816a3450b048efb994e283ef35a55ed17`.
- Final integration base: `e523daa11ae3ac4d06a9cbba64561ec06bd271f3`.
- Unchanged Code OSS: `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`.
- Browser oracle: Google Chrome `153.0.8010.50` on macOS arm64.
- Standards authority: Selectors Level 4 relative selector anchoring and the
  relational pseudo-class.

Unchanged Code OSS uses both
`.monaco-list-row:has(+ .monaco-list-row.selected)` in the notebook editor and
`p:has(+ [data-code] > .chat-codeblock-pill-widget)` in chat rendering. Native
matching rejected sibling-leading `:has()` arms even though Servo already parsed
them and the immutable invalidation plan already represented reverse sibling steps.

The shared matcher now anchors each relative arm at `:scope` and searches only the
reachable descendant region or following sibling subtrees. The ordinary compiled
selector matcher verifies the complete chain. This covers adjacent and general
sibling arms without creating a separate selector interpreter. Attribute and class
changes use the existing `previous_sibling` and `preceding_siblings` routes. Child-list
plans additionally visit surviving children so removing the final matching sibling
still recascades the preceding subject. Stable outer-compound indexes bound that work
to the changed sibling list.

## Browser and native oracle

The dedicated product-neutral contract covers the two exact Code OSS shapes,
nested functional selectors in an adjacent arm, ignored text/comment nodes,
adjacent class invalidation, general-sibling descendant attribute invalidation,
final-sibling removal/reinsertion, and forward-only matching.

- Pre-fix native control: 1/7 assertions pass; all six positive matching,
  computed-style, and restoration checks fail.
- Candidate native: 7/7 assertions pass in 96 ms.
- Chrome: 7/7 assertions pass in 49 ms.
- The Servo selector/parser and immutable dependency-plan executable passes,
  including exact adjacent and general-sibling reverse routes.
- Adjacent native nested-functional, attribute-invalidation, and structural-scaling
  filters pass.

Evidence SHA-256:

- pre-fix native result: `f50f4df86a89874fe3203af4c9eb94cfa77850732c1dfb482121ebc9319f1ca4`;
- candidate native result: `7e89b70e5bc0fa625912a1c1b1d0e5993a6bc3fae1bb6c94404f03699b9aeaa4`;
- Chrome result: `e77b819ce0a3aa525142ab8bca0c44bf48625fd9acabcadc9ec3f3351c049711`;
- candidate native library: `d34a543b2777aa46aad934aee18e4f74c386a8f5b8c66462261226f48b5e0d7f`.

## Bounded invalidation, timing, memory, and lifecycle

The certification gate keeps 128 affected sibling groups constant while growing
unrelated rules and nodes from 32 to 1,024. It performs 1,000 paired adjacent-class
and general-sibling descendant-attribute transitions and reads all 256 affected
computed styles after every transition.

Both unrelated sizes produce the same counters: 512,000 plan lookups, 9,664,000
candidate visits, zero fallback visits, 21,312,000 compound checks, 768,000 rule
checks, 512,000 cascade applications, and 768,000 cascade-candidate checks. The
work follows the affected sibling groups and is unchanged by 992 additional rules
and nodes.

The 32/1,024 unrelated cases complete in 3,097.37/3,355.65 ms, below the five-second
gate. Settled V8 heap returns byte-exactly to 1,103,344/1,585,208 bytes after the
fixture and style are detached and low-memory notification completes. Native node
counts do not increase, and peak RSS remains inside the 64 MiB growth bound before
engine destruction.

## Remaining selector boundary

This slice closes relative adjacent/general-sibling `:has()` matching and reverse
class, attribute, and child-list invalidation. The broader structural/state selector
matrix, shadow-specific selectors, and cumulative packaged product acceptance remain
open under #237 and #235.
