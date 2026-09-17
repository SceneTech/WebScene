# General CSS optimization: compiled invalidation

This records the implemented stages of the general CSS performance workstream,
not a claim that all CSS or Spotify resize work has been optimized. It replaces
mutation-time dependency discovery with immutable plans prepared alongside shared
stylesheet rule payloads. No site-specific selectors or resize-frame stretching are
used.

## Implemented

- Compile class and attribute dependencies from parsed compounds, decoding escaped
  identifiers and recursively visiting `:is()`, `:where()`, `:not()` and `:has()`.
- Compile subject/ancestor dependencies and forward/reverse relationship routes.
  Nested `:is()`/`:where()`/`:not()` follow forward combinators to the subject;
  `:has()` reverses relative combinators to its anchor. Inherited disabled/enabled
  state includes descendant controls, preserving first-legend exemptions. This
  narrows invalidation without expanding selector matching conformance.
- Share the plans with immutable rule payloads and account for their storage in
  process CSS memory diagnostics. Mutation handling looks up feature indexes; it
  no longer rescans selector text to discover class/attribute dependencies.
- Route `className`, `classList.add/remove/toggle/value`, and class attribute APIs
  through the same targeted path. Only changed class tokens select class-dependent
  rules; raw `[class]` selectors also participate.
- Follow custom-property consumers below affected sibling/descendant subjects, not
  just consumers below the original mutation. The native controlled-switch test
  and a product-neutral sibling-variable contract guard this path.
- Apply queued recascade roots in ancestor-before-descendant order, including
  roots discovered by different transition plans, so a consumer cannot read an
  ancestor's previous computed-variable state.
- Coalesce repeated changes using a node/attribute hash index, preserving the first
  and final values. Existing event/rendering checkpoints still flush synchronous
  style and geometry reads. Plans are evaluated through a coherent sequence from
  the complete pre-batch attribute state to the final state, preventing stale styles
  when multiple conjunctive selector attributes are removed together.
- Queue `toggleAttribute()` invalidation before custom-element callbacks so their
  synchronous reads see the change and reentrant writes remain the final state.
- Reuse ancestor/sibling-prefix match results only within an unchanged native
  matching pass. Old/new transition states have separate caches; none survives
  the pass or author callbacks. Keys include node, compiled selector, component,
  scope root, and relation kind. Recursive compiled selector lists remain pinned
  even when the separate 512-entry parser cache evicts them. Relation entries have
  a 16,384-entry soft cap; sibling indexes and pinned selectors are transient,
  proportional to the pass's visited work.
- Compile child-list sensitivity and refresh source/destination sibling subtrees
  for single-node `appendChild`/`insertBefore` moves when required. This fixes stale
  nested sibling matches. It is a conservative parent-local correctness path,
  not completion of structural invalidation optimization across all DOM APIs.

## Correctness and scaling gates

`contracts/css-attribute-invalidation-scope.html` is a local WPT-style testharness
contract in the required component profile. It covers subject/child/descendant/
sibling changes, inherited custom properties, nested functional selectors,
relational ancestors, escaped class names, class mutation APIs, fallback semantics,
and synchronous reads inside animation/custom-element callbacks, including
multi-attribute removals and reentrant writes.
Run the same contract against WebScene and Chrome. It is not an upstream WPT
submission.

`contracts/css-nested-selector-invalidation.html` adds ten WebScene/Chrome checks
for composed forward/reverse paths, sibling boundaries, inherited disabled state,
combined mutation checkpoints, sibling-chain reordering/reparenting, CSSOM changes,
and matching reuse across recursive selector-cache eviction. Both engines passed
all ten on September 17.

`webscene_selector_parser_tests` additionally tests the compiled plans, including
relative selector-list arms and escaped attribute identifiers. Native filter
`css-invalidation-scaling` grows
unrelated DOM nodes **and unrelated stylesheet rules** from 32 to 1,024 while a
fixed component receives a batched resize callback with 200 repeated class/attribute
mutations. With `WEBSCENE_NATIVE_ENGINE_CERTIFICATION=ON`, it asserts bounded and
identical dependency work at both sizes, using these diagnostics:

- `selector-invalidation-plan-lookups`
- `selector-invalidation-candidate-visits`
- `selector-invalidation-fallback-visits`
- `css-compound-match-checks` (includes nested matching and DOM selector queries)
- `css-rule-match-checks`
- `css-cascade-applications` (element full-cascade applications)
- `css-cascade-candidate-checks` (deduplicated candidates in those applications)

The September 17 macOS run recorded **15 lookups, 6 candidate visits, and 0 fallback
visits at both sizes**. Timings are printed for investigation, not used as flaky CI
thresholds. The extended run also recorded **49 compound checks, 12 rule checks,
7 cascades, and 12 cascade candidates at both sizes**. Counters include matching
and full-cascade work, but not total layout/painting. The test explicitly waits for the throttled diagnostic snapshot
outside its timing interval. In non-certification builds it still checks computed
styles and checkpoint behavior, without diagnostic counter assertions.

Example (use an existing configured native build directory):

```sh
cmake -S experiments/WebScene.NativeEngine.Probe -B "$CSS_TEST_BUILD" \
  -DWEBSCENE_NATIVE_ENGINE_CERTIFICATION=ON
cmake --build "$CSS_TEST_BUILD" --target webscene_native_engine_tests
WEBSCENE_NATIVE_ENGINE_TEST_FILTER=css-invalidation-scaling \
  "$CSS_TEST_BUILD/webscene_native_engine_tests"
```

## Component-size matrix and measured matching bottleneck

The same native filter now runs 20 cases: five component shapes, 8/128 affected
targets, and 32/1,024 unrelated nodes plus unrelated rules. Each case adds then
removes selector state and checks every target's computed width. All seven counters
must be identical at both unrelated sizes; work must stay within a linear
component-size ceiling and avoid document fallback. Inputs are block-level so this
is a style/invalidation fixture rather than an inline wrapping benchmark.

September 17 certification results at 128 affected targets (identical at both
unrelated sizes):

| Shape | Candidate visits | Compound checks | Rule checks | Full cascades |
| --- | ---: | ---: | ---: | ---: |
| Nested descendant selector | 512 | 3,084 | 512 | 258 |
| Nested general sibling selector | 256 | 2,560 | 512 | 258 |
| Inherited disabled controls | 258 | 1,028 | 512 | 258 |
| Sibling custom-property provider and aliases | 2,058 | 1,806 | 770 | 516 |
| Relational parent/ancestor routes | 1,024 | 3,968 | 512 | 512 |

The stronger matching counters exposed repeated scans of earlier siblings. In the
local route-only development build, 8/128 sibling targets took 328/51,328 compound
checks. Pass-local prefix reuse reduces these to 160/2,560 (20 checks per affected
target), with unchanged cascade/rule counts and styles. This is a deterministic
operation-count comparison between development variants, not a timing A/B, an
end-to-end Spotify result, or a Chrome-speed claim. Timing remains informational.

## Remaining stages

1. Extend the implemented descendant/sibling/custom-property/disabled/relational
   matrix to structural mutations, media/container queries, and additional dynamic
   state. Continue measuring matching and cascade work, not just plan traversal.
2. Narrow the remaining conservative structural paths and cover all mutation APIs.
   The compiled nested-selector and inherited-control routes are implemented;
   this does not claim broader selector matching conformance.
3. Broaden checkpoint consistency to remaining DOM/CSSOM mutation paths. ID,
   inline-style and several dynamic-state paths retain their existing handling.
4. Profile cascade reuse and dependency-aware layout caching, with invalidation
   tests for inheritance, custom properties, fonts and available size. Do not cache
   results solely by viewport size or trade correctness for elastic resizing.
5. Repeat matched-cadence end-to-end Spotify/Chrome profiling. The operation-count
   result above does not establish a browser-speed advantage or predict the total
   live-resize frame time.
