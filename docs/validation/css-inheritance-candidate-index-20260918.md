# CSS inheritance-candidate index — 2026-09-18

## Scope

This slice is based on the immutable variable-dependency metadata in PR #338
(`317d2e2fb9b7b7233b59e0cf370a3df0c40aa755`). It moves another repeated
viewport-media planning step to stylesheet preparation and indexing.

Each immutable rule payload now carries a modeled property mask for
declarations that can resolve to `inherit`, either directly or through
`var()`. The runtime records only those rules in a lifecycle-owned inheritance
candidate index when a stylesheet is added or rebuilt. A media transition
filters the compact index by its changed-property mask and current media truth;
it no longer walks every stylesheet rule, every declaration, and every
property/value token to reconstruct the same candidate set.

The index follows the existing cascade lifecycle: stylesheet replacement and
removal rebuild it; navigation and frame initialization clear it; document and
shadow cascade activation move it with the owning cascade; low-memory
compaction shrinks it; and memory diagnostics include active and inactive index
capacity. The UI compiler emits the immutable property mask, and parsed,
cached, and generated stylesheet parity compares it. Older or hand-authored
prepared payloads derive the mask once while they are indexed.

No computed style survives a mutation, no resize work is deferred, and there
is no site-specific property or selector path.

## Deterministic validation

The focused `media-query-targeted-recascade` native filter adds a width
inheritance fixture that independently varies 32 and 1,024 unrelated rules.
Activation and deactivation both produce the correct inherited 17/41-pixel
width. Across both fixture sizes, the two transitions perform:

| Unrelated rules | inheritance-index visits | transition declaration scans |
| ---: | ---: | ---: |
| 32 | 2 | 0 |
| 1,024 | 2 | 0 |

The existing custom-property, inherited-variable, inherited-value, exact-root,
and media subject-index bounded-work matrices remain unchanged. The final
source also passes:

- V8-free shared CSS declaration service;
- runtime, cached-runtime, and build-time generated stylesheet integration;
- `attribute-invalidation-scope`, `stylesheet-cssom`, and
  `dimension-variable-compatibility` native filters;
- the complete `css-invalidation-scaling` matrix; and
- the browser-referenced media contract, 1/1 native document and 5/5 subtests,
  plus 5/5 in Chromium.

WPT-style evidence:

- native: `/tmp/webscene-inheritance-index-native.OBHpDU/results/results.json`,
  SHA-256
  `47f17bb5f57603f9469c83c77c4b7139c394899f9968ae71afd25e2b44dcdfd3`;
- Chromium:
  `/tmp/webscene-inheritance-index-chrome.sW6tGB/results/results.json`,
  SHA-256
  `6c8ab03cd3ab492dd3024e4ccac5c6af1e0bcd235c821d0e895a6280cffc9ef0`.

## Populated Spotify evidence

The unchanged diagnostic uses the same 2,732-node/18-image catalog, pinned
toolchain, 1,800-frame settle, and 240 alternating 800/700 CSS-pixel threshold
transitions as the prior accepted candidate.

| Candidate | Median | Mean | p95 | Min–max |
| --- | ---: | ---: | ---: | ---: |
| merged #326 control | 92.1278 ms | 92.9922 ms | — | 86.947–104.625 ms |
| PR #338 variable metadata + inherited replay | 55.2183 ms | 55.3375 ms | 56.3241 ms | 54.0232–70.7105 ms |
| inheritance-candidate index | **53.0003 ms** | **53.2729 ms** | **54.1071 ms** | **51.6053–92.4725 ms** |

The new median is 4.02% lower than PR #338 and 42.47% lower than the merged
#326 control, equivalent to 1.738x dispatch throughput. Width-specific medians
are 52.7684 ms at 800 CSS pixels and 53.1736 ms at 700 CSS pixels. The one
92.4725 ms maximum is retained in the aggregate rather than trimmed.

A representative narrow transition preserves 1,751 selected inheritance
rules but obtains them from the precomputed index. Planning falls from the
prior representative 13.6822 ms to 11.3870 ms; application is 23.2315 ms in
that single diagnostic sample. Stable aggregate dispatch, rather than either
single trace, is the accepted comparison.

Evidence identities:

- profile: `/tmp/spotify-inheritance-candidate-index-profile-20260918.log`,
  SHA-256
  `f4fb350f19409dad0759bd2607cf42b8e60d2ee2550e1e23eb2110aca2f231d7`;
- trace: `/tmp/spotify-inheritance-candidate-index-trace-20260918.log`,
  SHA-256
  `9f2838c53072df38421fb1be7337dce3784cc76d01fc9db3bd8e515d2bcacec0`.

## Rejected duplicate-match shortcut

An earlier experiment carried variable-dependent property-name vectors from
planning into each request to skip one dependent-rule match during application.
It compiled and passed the focused media matrix, but copying and deduplicating
the per-node strings regressed the populated workload: median 56.0747 ms, mean
57.0339 ms, and p95 61.6147 ms. That is a 1.55% median and 9.39% p95 regression
from the accepted PR #338 baseline. The experiment was removed before this
slice. Its raw log is
`/tmp/spotify-planned-variable-properties-profile-20260918.log`, SHA-256
`211071cd9d2eff6b065f0db527f09ea949c107854bea2b78830fe0cc886bd8af`.

Do not reintroduce per-request property-name vectors without an allocation-free
representation and new stable product evidence.

## Completion boundary

The approximately 53 ms threshold dispatch remains above a 16.7 ms frame
budget. The representative transition still creates 785 requests, including
431 conditional inherited roots, and performs 690 targeted variable replays.
Further structural work should target winning-declaration dependency provenance
or another allocation-free way to exclude losing variable declarations before
application, while consuming #238 rather than duplicating cascade semantics.

The combined SDK also still needs manifest qualification and physical Spotify
resizing across repeated small-to-large threshold crossings against the same
Chromium build. These timings are synchronous engine dispatch, not physical
window cadence or a claim of beating Chrome. The profiling SDK is a development
overlay, not a qualified release package.
