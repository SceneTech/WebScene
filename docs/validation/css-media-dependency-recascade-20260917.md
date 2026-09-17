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
- replays only the affected property cascade for consumer-only custom-property
  requests, while retaining the complete cascade for changed-rule subjects,
  custom-property owners, pseudo-elements, inherited values, and conservative
  fallbacks;
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
| 8 direct + 2 aliased custom-property consumers | 3 applications | 3 applications |
| inherited color with an overriding branch | 10 applications | 10 applications |

Each count is identical on media activation and deactivation. The custom
property regression also caught and fixed an alias-discovery iterator lifetime
bug: adding a stylesheet alias could grow the affected-variable vector while a
reference into that vector was still used to inspect inline aliases.

The browser-referenced contract still passes 4/4 in both engines after the
property-targeted replay:

- native result: `/tmp/webscene-media-final-native.Jwgzym/results/results.json`;
- Chromium result: `/tmp/webscene-media-final-chrome.D3l9Z8/results/results.json`.

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
| property-targeted variable replay | 240 | 59.4642 ms | 59.6524 ms | 58.058–81.707 ms |

The final candidate median is 35.45% lower than the merged control (1.55×
throughput) and 8.35% lower than the first dependency-directed candidate. Raw
logs are `/tmp/spotify-media-control-profile-20260917.log`,
`/tmp/spotify-media-final-profile-20260917.log`, and
`/tmp/spotify-variable-targeted-profile-20260917.log`.

A traced 700-pixel transition reports 116 changed media rules, 923
variable-reference rules, 785 exact/conditional requests, zero unconditional
subtree requests, 431 conditional inherited candidates, and 137 roots whose
modeled inherited values actually changed. Planning took 13.5308 ms and
application 26.8904 ms in that diagnostic run. The raw trace is
`/tmp/spotify-media-selective-mask-once-20260917.log`.

With property-targeted replay, the same 785-request transition performs 287
targeted consumer replays. A representative transition used 13.8067 ms for
planning and 25.0519 ms for application; the stable 240-transition run rather
than this single sample is the accepted timing evidence. Set
`WEBSCENE_TRACE_RESIZE_PHASES=1` to split outer listeners, media refresh, frame
listeners, batched recascade, layout, and observers. The representative raw
trace is `/tmp/spotify-variable-targeted-once-20260917.log`.

## Rejected selector-index reuse experiment

The older root-only media-refresh experiment was repeated on this exact code
because phase attribution again made the full selector-index rebuild look
suspicious. It removed all 26,300 `index_css_rule` calls from the 100-transition
256-rule fixture and reduced that fixture's mean from 20.466 ms to 13.597 ms.
It did not improve the populated Spotify median (64.8782 ms control versus
64.8062 ms experiment), and its mean regressed from 65.9190 ms to 69.8560 ms
with a substantially worse tail. This agrees with the earlier cumulative gate
that rejected the experiment for CPU/FPS regression. Production therefore
continues rebuilding indexes on media truth changes; the accepted optimization
targets the measured variable-consumer cascade instead.

## Completion boundary

This is a mergeable optimization slice, not completion of #235. Roughly 59 ms
of synchronous dispatch remains above a 16.7 ms frame budget. The remaining
work is to precompute/reuse product-neutral custom-property dependency metadata,
reduce the remaining 431 conditional inherited candidates and 498 complete
cascades, consume #238 without duplicating its cascade semantics, assemble a
qualified combined SDK, and run continuous physical resize plus the
small-to-large threshold against the same Chrome build.

The SDK used here is a development overlay and is not release-qualified.
