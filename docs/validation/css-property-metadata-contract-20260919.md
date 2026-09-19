# CSS property metadata contract repair (2026-09-19)

Issue #617 tracks a current-main CI failure in the CSS effective-property
metadata contract. The catalog contained 106 managed properties while the test
still expected the 105-property baseline. The drift came from two intentional
feature additions that did not update the versioned metadata contract:

- `aspect-ratio` added one managed storage property and one typed native
  property; and
- single-layer mask support added eight storage-only names and expanded the
  generated CSSOM surface.

The first addition inserted `aspect-ratio` inside both ordered ID lists. Those
lists assign numeric IDs by array position, so an insertion would renumber every
later property. This repair moves the new entries to the append-only boundary:

- the original 105 managed names retain their exact sequence fingerprint
  `fc4d77c515998b6df1f119ff7e09b0b82cc812744c34f1c44c6fd142067b1fb2`;
- managed ID 105 is `aspect-ratio`;
- the original 146 native property IDs retain their exact sequence fingerprint
  `b17b1cd0520ae5e36fd875ed8226b1818508bbe41be9a32540ef58a1b31da036`;
  and
- native ID 146 is `aspect_ratio`.

The regression contract now checks the preserved prefixes and their explicit
append-only extensions. It also versions the current catalog boundaries:

| Contract | Current value |
| --- | ---: |
| Managed storage properties | 106 |
| CSSOM-supported names | 239 |
| Native property IDs | 147 |
| Native name-to-ID pairs | 202 |
| Native storage-only names | 62 |
| Native maskless IDs | 25 |
| Generated V8 CSSStyleDeclaration accessors | 435 |

The generated managed storage table, native enum, and native identity table are
regenerated from `css_property_metadata.json`. No runtime behavior or supported
property is removed.

## Validation state

Per the active throughput directive, this change has only received
`git diff --check`. The focused Python contract, generator `--check`, managed
CSS tests, native CSS tests, and Linux/macOS CI lanes remain unexecuted and must
run before release evidence is promoted.
