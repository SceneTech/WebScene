# Match before cascade-precedence sorting — 2026-09-17

Implementation: `6aa01a5c`, based on `170e0cff` / engine `86c83845`.

## Proven cost and change

The [merged production profile](css-compiled-payload-integration-20260917.md)
showed substantial candidate-sort partition/introsort activity in media
recascade, in addition to selector matching. The full candidate vector was
sorted by specificity before rejecting selector, media and scope misses.
Each comparison chased both rule payloads even though most candidates did not
contribute declarations.

The shared V8/NativeWeb cascade now deduplicates candidate **indices** using
numeric order. Matching then filters the candidates, and only the successful
ordinary/pseudo rule vectors are sorted by specificity and source position.
Equal-specificity precedence uses position in the same contiguous rule span,
not bucket/discovery order. Declaration importance and application remain
unchanged. This adds no persistent computed-style cache and defers no resize.
Other specialized variable-replay paths retain their existing ordering logic.

Chromium's `ElementRuleCollector` similarly collects matched rules and calls
`SortMatchedRules` before transferring their declarations. See the
[primary source](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/core/css/element_rule_collector.cc)
(observed blob `ec82c6bc7828d0ecff2d6fb073ea696c491977a0`,
`DidMatchRule`, `SortAndTransferMatchedRules`, `SortMatchedRules`). This is the
ordering-stage comparison, not a claim to reproduce all Blink cascade features.

## Semantic and bounded-work evidence

- The previous helper fails the new unsorted-discovery contract (exit 133);
  the changed helper returns correct ascending precedence.
- A product-neutral fixture grows candidates **39 → 1,031** while only **six**
  matched entries require payload-based precedence sorting. Ordinary rules,
  before/after rules, equal-specificity source order, reverse discovery order,
  duplicate indices and inactive media are checked. Numeric deduplication still
  processes the full candidate list; this is not an O(matches) claim for the
  entire cascade. Dense-match ordering is also compared with the previous full
  precedence-sort oracle; no universal all-workload speedup is inferred.
- Shared CSS service and parsed/persistent-cache/precompiled shared-stylesheet
  scene parity pass.
- Expanded required CSSOM contract: **7/7 Chrome and native**, including higher
  specificity, source-order reversal on stylesheet replacement, `!important`,
  repeated class tokens, dynamic state removal, pseudo rules and inactive media.
- Native contract slice: **81/81 in ten documents** (CSSOM, class index,
  attribute/nested invalidation, shadow roots, media, containment and accessibility).
- Cumulative certification invalidation/scaling suite and six adjacent native
  groups pass. The cumulative suite includes all 140 route cases.
- Fresh production SDK build passes. Installed-consumer Kestrel parity against
  the development overlay has matching 376, 298 and 311 nodes and zero
  DOM/style/layout differences in the existing three snapshots.

Two default-profile test filters selected zero documents for containment and
accessibility and are excluded. An attempted `--profile` option was rejected;
the successful checks use each explicit `--manifest` and report 10/10 each.

## Timing and packaging boundary

Candidate SDK: `/Volumes/SSD/sdks/spotify-match-sort-dev-20260917`.
It is a development overlay, not a manifest-integrity-qualified release SDK.
Engine archive SHA-256:
`6dda13f209405dbe655564b06ea3b0bde4cffe0ee9feed85249382dc70cac2e6`.
Control is the qualified merged engine, SHA-256
`72bd32198a9595f23b2f9876f7b6238725a0df68d0b6768582a01a28c315b2e3`.
The public-ABI diagnostic source is unchanged (SHA-256
`17c7a48812807f541728dc083a018d3bbc1360a3aaddd0f4996ac169354aaab7`).

The serial Spotify A/B/B/A runs use the same 700/800 breakpoint sequence. Our
builds and tests finish before the runs. Other host compilation was observed,
so timing results require explicit content and variability checks; this report
does not turn provisional timing into physical-frame/Chrome acceptance.

All four processes completed, with 12 measured transitions each after the first
settling resize. Every measured state has 2,726 elements and 18 images, but these
counts alone do not prove identical live stylesheet/content bytes. Median
dispatch times were A1 **331.242 ms**, B1 **195.169 ms**, B2 **288.624 ms**, A2
**204.940 ms**. The large within-variant variation and observed host compilation
make this timing comparison **rejected for speedup/regression claims**. Do not
pool those numbers into an apparently reliable percentage. No compiler/test
work launched by this task overlapped the runs, and no profiler was attached.
See [raw measurements and disposition](evidence/css-match-sort-abba-20260917.json).
Certification-only phase telemetry is unavailable in this production binary;
the raw zero phase fields are not evidence of zero work.

The open manual native demo remains the exact qualified `86c83845` package.
The candidate has not replaced it. Header clipping, the remaining breakpoint
pause and the broader CSS plan remain open. CI was not monitored.
