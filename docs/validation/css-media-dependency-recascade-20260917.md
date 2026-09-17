# Media-query dependency recascade — 2026-09-17

## Scope

This change narrows viewport-media recascade without deferring resize work or
retaining computed style across mutation boundaries. It is limited to
invalidation planning and does not claim the cascade/global-keyword completion
owned by #238.

The previous path selected a matched media-rule subject and replayed the whole
subtree. The new plan:

- applies known non-inherited declarations only to their matched subjects;
- finds explicit `inherit` consumers for modeled non-inherited properties;
- follows media-switched custom properties through stylesheet and inline alias
  chains to the declarations that consume them;
- treats supported maskless non-inherited properties as exact-subject work;
- conditionally propagates modeled inherited properties only while the
  relevant computed inherited values continue to change; and
- retains conservative subtree handling for unknown/unmodeled inherited
  properties and shadow-boundary cases.

All indexes and dependency scopes are pass-local. No resize throttling,
stale-frame stretching, deferred resize, or Spotify-specific selector/property
special case is introduced.

## Correctness and bounded work

`media-query-targeted-recascade` passes with activation and deactivation
coverage for direct media matches, stylesheet/inline `inherit`, a `var()`
fallback to `inherit`, transitive stylesheet custom-property aliases, transitive
inline aliases, and modeled inherited color propagation.

The certification counters are invariant under unrelated descendant growth:

| Fixture | 32 unrelated descendants | 1,024 unrelated descendants |
| --- | ---: | ---: |
| exact non-inherited root | 1 application | 1 application |
| 8 direct + 2 aliased custom-property consumers | 13 applications | 13 applications |
| inherited color with an overriding branch | 10 applications | 10 applications |

Each count is identical on media activation and deactivation. The custom
property regression also caught and fixed an alias-discovery iterator lifetime
bug: adding a stylesheet alias could grow the affected-variable vector while a
reference into that vector was still used to inspect inline aliases.

The browser-referenced contract passes 4/4 in both engines:

- native result: `/tmp/webscene-media-final-native.HKjBXZ/results/results.json`;
- Chromium result: `/tmp/webscene-media-final-chrome.T7KN8M/results/results.json`.

Adjacent `attribute-invalidation-scope`, `stylesheet-cssom`,
`dimension-variable-compatibility`, and the complete
`css-invalidation-scaling` filter pass. The focused native target and current
engine build also pass.

## Populated Spotify evidence

The unchanged AppScene Spotify catalog was built against the same pinned LLVM
22.1.8 toolchain and compared in separate serial processes. Both processes had
2,732 measured DOM nodes and 18 complete images on every sample. The diagnostic
alternated 800 and 700 CSS pixels for 240 transitions after the same 1,800-frame
startup settle. It pumps one frame after each synchronous resize barrier and is
an engine-dispatch benchmark, not a physical-window cadence measurement.

| Engine | samples | median dispatch | mean dispatch | min–max |
| --- | ---: | ---: | ---: | ---: |
| merged #326 control | 240 | 92.1278 ms | 92.9922 ms | 86.947–104.625 ms |
| dependency-directed candidate | 240 | 64.8797 ms | 65.9190 ms | 60.2178–102.993 ms |

The candidate median is 29.58% lower (1.42× throughput). Raw logs are
`/tmp/spotify-media-control-profile-20260917.log` and
`/tmp/spotify-media-final-profile-20260917.log`.

A traced 700-pixel transition reports 116 changed media rules, 923
variable-reference rules, 785 exact/conditional requests, zero unconditional
subtree requests, 431 conditional inherited candidates, and 137 roots whose
modeled inherited values actually changed. Planning took 13.5308 ms and
application 26.8904 ms in that diagnostic run. The raw trace is
`/tmp/spotify-media-selective-mask-once-20260917.log`.

## Completion boundary

This is a mergeable optimization slice, not completion of #235. Roughly 65 ms
of synchronous dispatch remains above a 16.7 ms frame budget. The remaining
work is to precompute/reuse product-neutral custom-property dependency metadata,
profile the remaining exact/conditional application cost, consume #238 without
duplicating its cascade semantics, assemble a qualified combined SDK, and run
continuous physical resize plus the small-to-large threshold against the same
Chrome build.

The SDK used here is a development overlay and is not release-qualified.

