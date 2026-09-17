# General CSS optimization: compiled invalidation

This is the first implementation stage of the general CSS performance workstream,
not a claim that all CSS or Spotify resize work has been optimized. It replaces
mutation-time dependency discovery with immutable plans prepared alongside shared
stylesheet rule payloads. No site-specific selectors or resize-frame stretching are
used.

## Implemented

- Compile class and attribute dependencies from parsed compounds, decoding escaped
  identifiers and recursively visiting `:is()`, `:where()`, `:not()` and `:has()`.
- Classify dependencies as subject, ancestor (`:has()`), or conservative fallback.
  The existing parsed combinator chain routes changes to children/descendants and
  following siblings. Complex nested selector routes and inherited disabled-control
  state deliberately retain fallback rather than risking missed invalidation.
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

## Correctness and scaling gates

`contracts/css-attribute-invalidation-scope.html` is a local WPT-style testharness
contract in the required component profile. It covers subject/child/descendant/
sibling changes, inherited custom properties, nested functional selectors,
relational ancestors, escaped class names, class mutation APIs, fallback semantics,
and synchronous reads inside animation/custom-element callbacks, including
multi-attribute removals and reentrant writes.
Run the same contract against WebScene and Chrome. It is not an upstream WPT
submission.

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

The September 17 macOS run recorded **15 lookups, 6 candidate visits, and 0 fallback
visits at both sizes**. Timings are printed for investigation, not used as flaky CI
thresholds. These counters describe invalidation planning, not all selector matching,
layout or painting. The test explicitly waits for the throttled diagnostic snapshot
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

## Remaining stages

1. Expand the scaling matrix to descendant-heavy and sibling-heavy trees, custom
   property chains, structural mutations, media/container queries and dynamic state.
   Measure candidate matching and cascade application as well as plan traversal.
2. Compile more precise reverse/forward routes for currently conservative nested
   selectors and inherited control state. Keep correctness tests for each fallback
   before replacing it. This patch does not expand selector matching conformance.
3. Broaden checkpoint consistency to remaining DOM/CSSOM mutation paths. ID,
   inline-style and several dynamic-state paths retain their existing handling.
4. Profile cascade reuse and dependency-aware layout caching, with invalidation
   tests for inheritance, custom properties, fonts and available size. Do not cache
   results solely by viewport size or trade correctness for elastic resizing.
5. Repeat matched-cadence end-to-end Spotify/Chrome profiling. The operation-count
   result above does not establish a browser-speed advantage or predict the total
   live-resize frame time.
