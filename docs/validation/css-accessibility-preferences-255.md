# CSS accessibility preference qualification

This gate covers the product-neutral preference slice implemented for issue #255:

- host publication of forced-colors, reduced-motion, and increased-contrast state;
- live `forced-colors`, `prefers-reduced-motion`, and `prefers-contrast` media-query updates;
- computed system colors plus inherited `color-scheme` and `accent-color` values;
- a public managed host property and versioned native C ABI with unknown-flag rejection;
- bounded refresh and cleanup across 4,096 controls and 100 enable/restore cycles.

## Direct gates

| Gate | Denominator | Result |
| --- | ---: | --- |
| Native WPT profile | 1 document / 10 subtests | 1/1 and 10/10 passed |
| Chrome 153 oracle | 1 document / 10 subtests | 1/1 and 10/10 passed |
| Native preference lifecycle/performance | 4,096 controls / 100 cycles / 200 states | passed in 8,394.38 ms; 8,194..8,198 fixture-node bound; at most 1 retained node |
| Portable CSS tests | 364 tests / 2 target frameworks | 364/364 on net8.0 and 364/364 on net10.0 |
| Managed host build | 1 target | 1/1 passed with 0 warnings and 0 errors |
| Portable native build | 1 target | 1/1 passed |
| V8 dedicated native target | 1 target / 1 CTest | 1/1 built and 1/1 passed |

The native gate has a 15,000 ms ceiling, requires zero host animation-frame demand
after the final settled state, waits for a completed low-memory notification, limits
post-cleanup V8 heap growth to 32 MiB, and rejects unknown ABI flag bits. The two
retained runner reports are bounded to 64 KiB each; their combined observed size is
5,187 bytes.

The monolithic native test executable is excluded from this change-related gate
because it reaches the pre-existing four-engine resource-deduplication assertion.
The dedicated `webscene_css_accessibility_preferences_tests` target isolates the
#255 semantic, performance, memory, and lifecycle checks from that baseline failure.

Machine-readable results and report digests are recorded in
`docs/validation/evidence/css-accessibility-preferences-255-20260917.json`.
