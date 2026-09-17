# Pass-local ancestor rejection — 2026-09-17

## Scope and correctness boundary

This stage follows matched-rule precedence sorting (`6aa01a5c`, evidence at
`afe98a0f`). It targets repeated full matching of selectors whose mandatory
ancestor tag, ID or class does not occur in the subject's ancestry. It does not
defer resize, stretch an old frame or change cascade precedence.

The design was checked against Chromium's negative-only ancestor filtering:
[SelectorFilter](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/core/css/selector_filter.h)
(observed blob `fdcfb63b6d08f854e9b61c69aa24d74aa25944e1`).
Our implementation is independent: a 256-bit summary and bounded pass-local
maps, rather than Chromium's ancestor stack and 8192-bit filter.

- Collect only mandatory tag/ID/class features on the contiguous ancestor/child
  suffix. Reset requirements at a sibling relation. Do not require attributes
  or features from functional-pseudo alternatives.
- Follow the existing `dom_parent` relation, including its shadow boundary.
  Include the existing synthetic `body`/`html` alias conservatively.
- ASCII-fold hashes, but retain the original case-sensitive full matcher.
  Collisions can only cause extra matching; filter acceptance is never a match.
- Cache inclusive ancestor summaries and selector-component requirements only
  for an immutable `selector_match_context`. Both are capped at 16,384 entries.
  On exhaustion, disable rejection rather than use incomplete information.
- Existing V8 batch scopes discard the context before authored callbacks and
  reset it between previous/current mutation state. Selector-list ownership is
  retained by the existing context. This introduces no mutation-lived cache.

The shared primitive is exercised directly in NativeWeb tests, but ordinary
NativeWeb query/cascade calls currently supply no matching context and therefore
do **not** gain this runtime optimization. V8's existing batched paths do.
No compiled stylesheet payload/emitter format changes are required.

## Controlled tests

The initial control failed the absent-ancestor regression with exit 174:
depth eight performed ten compound checks instead of zero. The implemented
filter rejects before compound matching at depths eight and 64.

The final repeated-match fixture separately counts full matching, existing
prefix-cache matching with this filter disabled, and ancestor-summary work.
Zero compound checks is **not** zero work: summaries must be constructed, masks
looked up and compared. This avoids attributing existing prefix-cache savings
to the new filter. For 128 repeated absent-ancestor queries:

| Depth | Uncached compound checks | Existing prefix-cache checks | Filtered checks | Ancestor summaries |
| --- | ---: | ---: | ---: | ---: |
| 8 | 1,280 | 137 | 0 | 9 |
| 64 | 8,448 | 193 | 0 | 65 |

Direct parity covers descendant/child selectors, sibling prefixes, functional
pseudos, scope, HTML tag folding, XML case, escaped identifiers, class whitespace,
ID/class mutations and reparenting with a fresh context. Capacities 0, 1, 2 and
16,384 preserve answers; a deliberately colliding class hash still fails full
matching. Required WPT additions exercise sibling/functional selector styles,
ancestor mutations, reparenting and shadow boundaries against Chrome and native.

## Validation and acceptance

- Shared CSS service and parsed/persistent-cache/precompiled stylesheet parity
  pass. The service test includes deliberate collision and capacity fallback.
- Expanded class-index/ancestor contract: Chrome **7/7**, native **7/7**.
- Native contract slice: **84/84 in ten documents**, including CSSOM, attribute
  and nested invalidation, shadow DOM, three media documents, containment and
  accessibility preferences. Two initially mistyped filters selected zero
  documents and are excluded; the corrected selections are included.
- Cumulative invalidation/scaling suite passes all 140 route cases plus existing
  subject-index, character-data, child-vector and batched fixtures. Six adjacent
  runtime groups pass: targeted media recascade, reentrant media, MediaQueryList,
  attribute scope, stylesheet CSSOM and dimension/custom-property compatibility.

Production runtime and SDK builds pass. The development overlay is
`/Volumes/SSD/sdks/spotify-ancestor-filter-dev-20260917`, not a newly
manifest-integrity-qualified release SDK. Engine archive SHA-256:
`eb42dcd6d52e0598bad05517f90528782c3c2b348bb1cb8c60983e2cc2816fe1`.
An installed-consumer Kestrel build also passes parsed/compiled parity: 376/376,
298/298 and 311/311 nodes with zero DOM/style/layout differences in the three
existing snapshots. Report:
`/Volumes/SSD/builds/kestrel-ancestor-filter-20260917/parity.json`.

The open manual Spotify app remains the qualified `86c83845` SDK build.
This experiment does not replace it. A production public-ABI Spotify comparison
uses the unchanged diagnostic source and the matched-sort development engine as
control. Live-content node/image counts alone cannot establish byte-identical
content; production certification-only phase fields remain unavailable.

No CI was monitored. #257 remains separately owned. Physical-frame/Chrome
resize acceptance and the broader CSS optimization plan remain incomplete.

### Production timing disposition

The serial A/B/B/A completed 48 measured transitions, all 2,726 elements and 18
images. Median dispatch times were A1 **190.766 ms**, B1 **132.511 ms**, B2
**205.426 ms**, A2 **312.162 ms**. Other host compilation began during the
comparison, and both variants vary substantially between runs. These timings
are **rejected for speedup/regression claims**, not pooled into a percentage.
Our builds/tests completed before the runs and resumed only after all four
processes exited. No profiler was attached. The optimization has controlled
work-count evidence, but not a new accepted Spotify timing result.

The first consumer configuration omitted the explicit arm64 architecture and
was correctly rejected by the SDK profile. Reconfiguration with arm64, the
pinned LLVM 22.1.8 toolchain and selected Xcode SDK succeeded. The existing Dawn
dependency targets macOS 26.5; this does not qualify macOS 26.0 execution.

[Raw measurements and disposition](evidence/css-ancestor-filter-abba-20260917.json).
