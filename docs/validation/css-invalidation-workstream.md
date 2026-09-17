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
- Route ID property/attribute APIs and `dataset` assignment/deletion through the
  same transition checkpoints. Reconstructed batch states synchronize both the
  attribute map and the ID used by matching. Value conversion runs before capturing
  the previous state, and dataset custom-element reactions follow invalidation.
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
- Compile child-list routes and a per-cascade structural-rule index. Positional
  selectors reach children; `:empty` reaches the changed parent; `:has()` reaches
  parent/ancestor anchors; nested/sibling routes reach affected final subjects.
  Candidate projection ignores pseudos while retaining tag/class/ID/attribute
  filters, so subjects that stopped matching after removal still get recascaded.
- Use completed child lists for insertion/removal/replacement/reparenting APIs,
  including fragments, HTML and text setters. Refresh source and destination
  structural subjects, then inserted subtrees for ancestry/inheritance. Connected
  custom-element callbacks observe final sibling styles, including ancestor
  `:has()` and inherited variables. This replaces the former parent-subtree fallback.
- Deliver structural custom-element reactions after the native mutation and style
  invalidation finish. Nested boundaries use one FIFO per element, preserving
  pending connected/disconnected ordering during reentrant operations. Failed
  operations unwind their boundary without losing the original exception.
  Documents with no valid custom-element definition make no reaction-bridge calls;
  first definition during argument conversion lazily activates enclosing boundaries.
- Fix `:empty` matching to ignore comments and empty text, but not whitespace text.

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

`contracts/css-id-dataset-checkpoints.html` passes seven checks in WebScene and
Chrome: ID reflection and attribute APIs, dataset updates/deletion, batched
conjunction removal, relational ancestors, reentrant custom-element callbacks,
and value-conversion side effects. All five original correctness checks failed
against the preceding native implementation, showing stale styles after removal.

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

The same native filter now runs 48 cases: twelve component shapes, 8/128 affected
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
| ID following-sibling transition | 256 | 1,028 | 512 | 258 |
| Dataset following-sibling transition | 256 | 1,028 | 512 | 258 |

The stronger matching counters exposed repeated scans of earlier siblings. In the
local route-only development build, 8/128 sibling targets took 328/51,328 compound
checks. Pass-local prefix reuse reduces these to 160/2,560 (20 checks per affected
target), with unchanged cascade/rule counts and styles. This is a deterministic
operation-count comparison between development variants, not a timing A/B, an
end-to-end Spotify result, or a Chrome-speed claim. Timing remains informational.

## Remaining stages

### Structural follow-up after #148 merged

`css-child-list-invalidation.html` is a required-profile WPT-style contract,
not an upstream WPT submission. Its initial 19 tests passed Chrome but exposed
16 native failures on the preceding build. The positional-index stage passes
**50/50 in WebScene and Chrome**: removal/replacement/reorder APIs, both sides of
reparenting, `:empty` character data, fragment/HTML connection checkpoints,
nested negated positional selectors, inherited custom properties, and first-legend
disabled-state changes, and all ten modeled positional pseudo classes across
mixed tags/non-element siblings and repeated reorders/removals. Callback observations are captured and asserted outside
the callback, so swallowed custom-element exceptions cannot masquerade as passes.

Five additional native scaling shapes exercise positional insertion/removal,
general-sibling insertion/removal, relational-ancestor sibling targets, and empty
state, including a wide positional sibling list. The matrix additionally records
`css-positional-sibling-visits`, so bounded compound-match counts cannot hide
quadratic internal scans. Its linear scan gate reproduced the old path's failure:
8/128 affected siblings required 136/32,896 visits, despite unchanged CSS results.
Pass-local positional indexing reduces this to **17/257 visits**, with identical
other matching/cascade counts and widths. The index stores element and same-tag
positions/counts per parent only for the current immutable matching pass, and is
discarded/reset with the existing relationship memo before changed states.
Calls outside such a pass keep the uncached matcher. This preserves the existing
matching semantics rather than claiming new namespace or `nth-child(... of S)`
support. Unrelated *structural* rule scaling remains to be measured. Operation
counts are not a timing or end-to-end browser-speed claim.

The certification build passes all 48 cases with identical eight-counter vectors
across unrelated growth, including the stricter wide-list scan budget. Seven
adjacent native groups and eight WPT-style contracts (98 checks) also pass after
the positional-index change. The structural contract passes Chrome 50/50.
The production build also passes the 48-case semantic matrix and 50-check
structural contract without certification telemetry.

### Disconnected reaction checkpoints

The subsequent contract first reproduced eight failures: seven removal/replacement
APIs delivered disconnected callbacks before detaching the element, and reparenting
delivered them before destination attachment and both sides' style invalidation.
All eight passed Chrome. Native reaction boundaries now defer callback delivery
until the final mutation checkpoint, while keeping per-element FIFO ordering across
nested operations. The expanded contract passes **63/63 in WebScene and Chrome**,
including reentrant reinsertion, pending connection followed by nested removal,
first registry activation during conversion, invalid hierarchy, and throwing
argument conversion with exact exception identity. Six adjacent custom-element
contracts (including upstream lifecycle tests) pass **118/118** checks.

The native matrix adds a `tree-reactions` shape: 8/128 custom elements are removed
and inserted beside `:empty`-dependent targets. Each callback reads and records its
target's computed width; observations are asserted outside callbacks. The expanded
**52-case production and certification matrix** passes. All eight CSS work counters
remain identical with unrelated growth and stay within the linear component budget.
For 128 reaction targets at both unrelated sizes: 512 plan lookups, 384 candidate
visits, zero fallback visits, 1,408 compound checks, 512 rule checks, 896 cascades,
512 cascade candidates, and zero positional sibling scans. Parser tests also pass.
A native cold-document regression
also checks zero begin/end reaction-bridge calls until the first valid definition.
Seven adjacent native groups and five adjacent structural/attribute WPT contracts
(35 checks) pass. These are local checks, not a remote CI or full custom-element
conformance claim.

Informational production timing for the new shape still grows with unrelated
content (one 128-target sample: approximately 26 ms with 32 unrelated nodes/rules,
77 ms with 1,024). This workload forces synchronous style reads after each mutation;
the residual cost needs attribution, not a claim of end-to-end improvement. Broader
cascade/layout profiling remains coordinated with #238/#240/#243.

This stage does not complete every DOM checkpoint: variadic/fragment forms of
`prepend`/`before`/`after`, and further
character-data/CSSOM mutation paths still need the browser-referenced audit.
The rest of the optimization plan remains open; no Chrome-speed advantage follows
from the structural operation counts.

### Remaining acceptance work

1. Extend the implemented descendant/sibling/custom-property/disabled/relational
   matrix to structural mutations, media/container queries, and additional dynamic
   state. Continue measuring matching and cascade work, not just plan traversal.
2. Finish the structural checkpoint audit above and profile
   unrelated structural-rule growth. Compiled child-list, nested-selector and
   inherited-control routes are implemented; this does not claim broader selector
   matching conformance.
3. Broaden checkpoint consistency to remaining DOM/CSSOM mutation paths. ID and
   dataset are covered; inline-style and several dynamic-state paths retain their
   existing handling. Distinguish fragment navigation's designated `:target` from
   a live ID/hash comparison: Chrome rejected a test that assumed inserting an ID
   after navigating to a missing fragment automatically made that element a target.
4. Profile cascade reuse and dependency-aware layout caching, with invalidation
   tests for inheritance, custom properties, fonts and available size. Do not cache
   results solely by viewport size or trade correctness for elastic resizing.
5. Repeat matched-cadence end-to-end Spotify/Chrome profiling. The operation-count
   result above does not establish a browser-speed advantage or predict the total
   live-resize frame time.
