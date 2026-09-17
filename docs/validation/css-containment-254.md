# CSS containment qualification

This gate covers the product-neutral slice implemented for issue #254:

- named and unnamed inline-size containers, nested lookup, threshold operators, and self-query rejection;
- `cqi`, `cqw`, `cqb`, `cqh`, `cqmin`, and `cqmax` length resolution with viewport fallback;
- synchronous query updates after size, container-type, stylesheet, detach, and reattach mutations;
- `content-visibility: hidden` intrinsic fallback, scene/hit/focus exclusion, and reveal/hide lifecycle;
- bounded native work across 4,096 descendants and 100 hide/reveal cycles.

## Direct gates

| Gate | Denominator | Result |
| --- | ---: | --- |
| Native WPT profile | 1 document / 10 subtests | 1/1 and 10/10 passed |
| Chrome 153 oracle | 1 document / 10 subtests | 1/1 and 10/10 passed |
| Native lifecycle/performance | 4,096 descendants / 100 cycles | passed in 1,340.03 ms; 4,101 peak nodes; 0 retained nodes |
| Portable native build | 1 target | passed |
| V8 native build | 2 targets | passed |

The lifecycle gate has a 10,000 ms ceiling, permits at most 4,102 fixture nodes,
requires all fixture nodes to be reclaimed after an explicit low-memory cycle, and
limits post-cleanup V8 heap growth to 32 MiB. The two retained runner reports are
bounded to 64 KiB each; their combined observed size is 5,325 bytes.

The monolithic native test executable still stops before this gate on the existing
four-engine resource-deduplication assertion. The dedicated
`webscene_css_containment_tests` target isolates the #254 semantic, performance,
and lifecycle checks from that baseline failure.

Machine-readable results and report digests are recorded in
`docs/validation/evidence/css-containment-254-20260917.json`.
