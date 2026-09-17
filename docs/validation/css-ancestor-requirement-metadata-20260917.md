# Compiled CSS ancestor-requirement metadata — 2026-09-17

## Scope

The existing negative-only ancestor filter correctly avoids full compound
matching when a selector's required ancestor tag, ID, or class cannot occur in
the subject's ancestor chain. A current-main Spotify breakpoint profile showed
that every selector-matching pass still rebuilt those immutable requirements
and recovered them through a selector-pointer hash table.

This change moves the four-word requirement mask into
`compiled_css_selector`, aligned one-to-one with its compiled compounds. The
selector compiler prepares it once. Matching passes directly index the
immutable vector and retain only the bounded, pass-local DOM ancestor summaries.

This is deliberately not a computed-style cache. No result survives a DOM
mutation boundary, positive filter answers still require full selector
matching, hash collisions remain conservative, and capacity exhaustion still
disables filtering by returning a saturated ancestor summary. Sibling and
functional-pseudo features are not treated as mandatory subject ancestors.

## Regression coverage

The NativeWeb selector service now verifies that:

- every compiled selector has exactly one requirement mask per compiled
  compound;
- repeated matching passes and all ancestor-summary capacities leave the
  compiled masks byte-identical;
- sibling identities do not leak into the subject's ancestor requirement;
- descendant, child, sibling, functional-pseudo, scope, case-sensitive XML,
  mutation, reparenting, collision, and capacity behavior retains full-match
  parity; and
- an absent ancestor at depths 8 and 64 still performs zero compound checks
  after building only 9 and 65 pass-local ancestor summaries respectively.

The current native engine's focused media-query, CSS-invalidation, attribute,
CSSOM, and dimension-variable groups also pass. They preserve the existing
work-counter invariants under unrelated DOM/rule growth.

The existing browser-referenced
`contracts/css-nested-selector-invalidation.html` is the WPT-style behavior
gate for this matching boundary. It covers composed descendant/child/sibling
paths, functional selectors, reparenting, CSSOM changes, and reuse across
compiled-list eviction. The candidate passes 10/10 subtests in both native and
Chrome 153.0.8010.48. No browser-visible behavior was added merely to expose an
internal metadata lifetime.

## Populated Spotify A/B/B/A

Both consumers use the unchanged AppScene Spotify catalog at
`68a9dbd15e6b02e4bec0a76d5190713868b6517a`, LLVM 22.1.8, and the same host.
The baseline is merged WebScene `5c67a183637c59da1515beddc3d3800f8c0a32c6`.
The candidate differs only by this ancestor-requirement metadata change.

Each process loaded 2,733 nodes and 18 complete images. After the settling
resize, all 48 measured 800↔700 transitions retained 2,732 nodes and 18 images,
with four layout passes per transition. The serial order was A/B/B/A with 12
measured transitions per process:

| Variant | Run median dispatch | Run mean dispatch | Run p95 dispatch |
| --- | ---: | ---: | ---: |
| merged main A1 | 124.215 ms | 125.863 ms | 132.769 ms |
| candidate B1 | 107.882 ms | 109.995 ms | 116.259 ms |
| candidate B2 | 107.544 ms | 109.385 ms | 115.901 ms |
| merged main A2 | 122.489 ms | 124.332 ms | 131.187 ms |

Pooled medians are 123.082 ms for merged main and 107.544 ms for the
candidate, a 12.62% reduction. Pooled means are 125.097 ms and 109.690 ms.
Spotlight indexing was active during the run, but the reversed ordering and
the separation in both pairs support retaining the change. This is engine
dispatch evidence, not a claim of physical-window smoothness or a Chrome
comparison. At roughly 108 ms, the media-threshold transition remains above a
16.7 ms frame budget.

## Validation

- NativeWeb `native_web_css_service`: pass;
- NativeWeb `native_web_shared_styles`: pass;
- current native engine build: pass;
- focused current-engine groups `media-query-targeted-recascade`,
  `css-invalidation-scaling`, `attribute-invalidation-scope`,
  `stylesheet-cssom`, and `dimension-variable-compatibility`: pass;
- `css-nested-selector-invalidation.html`: native 10/10 and Chrome 10/10;
- development SDK producer: 16/16 AppScene tests, 2,870-file manifest, and
  compiler/runtime profile: pass;
- populated Spotify A/B/B/A: equivalent content/work and 12.62% lower pooled
  median dispatch;
- `git diff --check`: pass.

Development SDK:
`/Volumes/SSD/sdks/spotify-css-ancestor-precompute-dev-20260917`.
Archive SHA-256:
`f6849bb28d83a86c209cda9af659e585f54365b42c358ff09907c6525b38db93`.

## Boundary

This change does not alter cascade origins, layers, global keywords, custom
properties, initial values, or computed serialization owned by issue #238. It
also does not introduce resize throttling, stale-frame stretching, deferred
work, or Spotify-specific CSS. Physical Spotify resize and same-build Chrome
comparison remain parent-epic acceptance gates.
