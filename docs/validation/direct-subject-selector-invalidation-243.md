# Direct-subject selector invalidation — 21 September 2026

WebScene issue #243 identified repeated selector-transition planning as a
material owner of unchanged Code OSS workspace startup. Three traced launches
spent 560–587 ms planning and applying 4,205–4,403 selector transitions, while
layout consumed 219–247 ms. Common list-row class mutations repeatedly planned
roughly 800–1,300 rules even though the changed selector feature was on the
selector's final compound and only the mutated element could be affected.

The runtime now sends final-compound, subject-only dependencies directly to the
existing subject cascade. Dependencies that route through an ancestor,
descendant, child, or sibling keep the compiled transition planner. Inherited
properties and custom-property consumers keep their existing propagation.
`WEBSCENE_PROBE_DISABLE_DIRECT_SUBJECT_INVALIDATION=1` restores the old path in
certification builds for an identical-input A/B comparison.

## Product-neutral gate

`WEBSCENE_NATIVE_ENGINE_TEST_FILTER=direct-subject-invalidation` mutates 64
elements across 512 relevant class rules, first with 32 and then with 1,024
unrelated rules. Addition and removal preserve computed width and perform the
exact 128 required subject cascades.

| Path | 32 unrelated | 1,024 unrelated | Plan lookups | Candidate/fallback visits |
| --- | ---: | ---: | ---: | ---: |
| old planner | 71.41 ms | 72.42 ms | 65,664 | 0 / 0 |
| direct subject | 52.83 ms | 40.72 ms | 0 | 0 / 0 |

The descendant/child/adjacent/general-sibling attribute invalidation contract
passes. The broader CSS invalidation matrix preserved identical bounded counters
at 32 and 1,024 unrelated rules through the existing descendant, sibling,
disabled-state, variable, relational, identity, dataset, and structural cases.
Its pre-existing `tree-reactions` case still exceeds its compound-check ceiling;
the untouched baseline also has pre-existing failures before that point. The
unrelated live-form filter fails identically on the untouched baseline because
`:invalid` now includes both the form and its invalid control.

## Unchanged Code OSS gate

The candidate was produced through the AppScene SDK pipeline at clean AppScene
`cd0a02ecd15f0a635bccc242cdd657888db39e25`, WebScene base
`a99eaf23aef5e5525b0a58c5635fa8a431b604f1` plus this patch, and unchanged Code
OSS `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`. The SDK inventory contained 2,906
files, AppScene passed 31/31 tests, and the macOS compiler/runtime profile passed.

Twenty clean native launches all reconstructed the exact remote root, registered
the stock provider, resolved the exact five entries, populated the five-entry
Explorer model, painted all five rows, and stayed within 64 Explorer DOM nodes.

| Boundary | Clean base | Candidate | Change |
| --- | ---: | ---: | ---: |
| provider resolve p50 | 914.43 ms | 751.61 ms | -17.8% |
| provider resolve p95 | 1,158.98 ms | 762.64 ms | -34.2% |
| first-child paint p50 | 2,164.26 ms | 1,828.19 ms | -15.5% |
| first-child paint p95 | 2,262.03 ms | 1,860.12 ms | -17.8% |
| launches within 2 seconds | 9/20 | 20/20 | +11 passes |

The candidate report is
`/private/tmp/workspace-direct-subject-243-cycles/report.json`, SHA-256
`5387023d909650235e83d1776302bf86beef65d8bd55585ae938972fd7cd5f2f`.
A traced candidate launch reduced selector-transition time from 559.59 ms to
448.29 ms and maximum WebSocket queue delay from 883.27 ms to 544.19 ms while
remaining inside the product paint bound. Its compact report SHA-256 is
`41fd9d5be3a41b99309b52d806890564ef5865f830892a020639b528aa31962a`.
