# CSS variable dependency metadata and inherited replay — 2026-09-18

## Scope

This slice continues the dependency-directed viewport-media recascade from
`css-media-dependency-recascade-20260917.md`. It removes repeated parsing of
immutable stylesheet `var()` values and allows variable consumers that set a
modeled inherited property to use the existing exact-property replay path.

Each interned CSS rule payload now stores both per-declaration and aggregate
custom-property references. Runtime indexes, alias expansion, media planning,
consumer discovery, pseudo-element fallback checks, geometry invalidation, and
custom-property mutation invalidation read that metadata instead of rescanning
stylesheet declaration text. Runtime-created inline style values remain parsed
at transition time because those values are mutable.

The build-time UI compiler emits the same metadata. Parsed, cached, and
generated stylesheets compare the metadata as part of the existing shared-style
parity gate. A conservative runtime fallback parses declarations from an older
or hand-authored prepared payload whose per-declaration metadata is absent or
misaligned; this preserves compatibility without putting current parser- or
compiler-produced sheets back on the transition-time parsing path.

When a variable consumer affects a modeled inherited property, exact-property
replay now snapshots the consumer's previous computed style, reapplies only the
dependent declarations, and propagates only the inherited properties whose
computed values actually changed. Explicit overriding descendants remain
unchanged. Changed custom-property owners, pseudo-elements, unknown properties,
shadow-boundary cases, and other conservative fallbacks still take the complete
cascade path.

No resize throttling, deferred work, stretched stale frame, mutation-lived
computed-style cache, or Spotify-specific selector/property path is introduced.

## Correctness and bounded work

The certification diagnostic `css-variable-runtime-token-scans` proves that
viewport transitions do not reparse immutable stylesheet variable references.
The focused `media-query-targeted-recascade` filter passes with these results:

| Fixture | 32 unrelated | 1,024 unrelated | Activation/deactivation |
| --- | ---: | ---: | --- |
| immutable variable metadata | 0 runtime token scans | 0 runtime token scans | both transitions included |
| inherited variable consumer | 3 complete cascade applications | 3 complete cascade applications | `[3, 3]` at both sizes |
| prior custom-property fixture | 3 complete cascade applications | 3 complete cascade applications | `[3, 3]` at both sizes |
| prior inherited-value fixture | 10 complete cascade applications | 10 complete cascade applications | `[10, 10]` at both sizes |

The inherited-variable fixture contains a changed token owner, an inherited
`color: var(...)` consumer, one inheriting child, one explicitly overriding
child, and independently varied unrelated descendants. The token owner takes
one complete cascade; the consumer uses exact-property replay; the two children
are recomputed through normal inherited-value propagation. Correct styles and
complete-cascade work remain invariant as unrelated descendants grow.

The browser-referenced
`contracts/media-query-targeted-subtree-recascade.html` contract adds activation
and deactivation coverage for the same inherited-variable/override topology and
passes 1/1 documents and 5/5 subtests in the native runner and 5/5 in Chromium:

- native: `/tmp/webscene-media-metadata-native-final.ZFfSFu/results/results.json`
  (`a12e844490ed5a12ae73f43cce30bc5b694a4b9c32c2ed1741bf8f539879aafc`);
- Chromium: `/tmp/webscene-media-metadata-chrome-final.wfJaaX/results/results.json`
  (`5fa603ff9e904b1080474960c984e6a269fdc4064e328e532bd5d479ef210529`).

Final-source local gates also pass:

- V8-free shared CSS declaration service;
- runtime, cached-runtime, and build-time generated stylesheet integration;
- `attribute-invalidation-scope`, `stylesheet-cssom`, and
  `dimension-variable-compatibility` native filters; and
- the complete `css-invalidation-scaling` matrix.

## Populated Spotify evidence

The same populated AppScene Spotify catalog and diagnostic protocol used by the
previous slice measured 240 alternating 800/700 CSS-pixel transitions. Every
sample retained 2,732 measured DOM nodes and 18 complete images.

| Candidate | Samples | Median | Mean | p95 | Min–max |
| --- | ---: | ---: | ---: | ---: | ---: |
| merged #326 control | 240 | 92.1278 ms | 92.9922 ms | — | 86.947–104.625 ms |
| merged #335 property-targeted replay | 240 | 59.4642 ms | 59.6524 ms | — | 58.058–81.707 ms |
| metadata only | 240 | 60.0185 ms | 59.8428 ms | 61.5468 ms | 57.0133–77.0330 ms |
| metadata + inherited-property replay | 240 | **55.2183 ms** | **55.3375 ms** | **56.3241 ms** | **54.0232–70.7105 ms** |

The accepted candidate median is 40.06% lower than the merged #326 control
(1.668x dispatch throughput), 7.14% lower than #335, and 8.00% lower than the
metadata-only candidate. Its width-specific medians are 55.0614 ms at 800 CSS
pixels and 55.4110 ms at 700 CSS pixels.

Metadata alone is retained as an intermediate negative result: eliminating
stylesheet token scans did not improve the populated workload. The additional
speedup came from safely moving inherited-property consumers onto exact-property
replay.

Raw logs and SHA-256 identities:

- metadata-only profile:
  `/tmp/spotify-variable-metadata-profile-20260917.log`,
  `65b63a7af4a2e439ccf958a4a2700ed849a92b2b7333af5fc7c187a657e001bd`;
- accepted profile:
  `/tmp/spotify-inherited-variable-replay-profile-20260917.log`,
  `b531c1d11ff3309e19520f01bc052095ebe985a84a36686f0df4899d8e117042`;
- representative trace:
  `/tmp/spotify-inherited-variable-replay-trace-20260917.log`,
  `4cb71b1d20af37b868ad0641538e1d5c8f9bc8b0a69f3e4ccc79907a10b189ca`.

The representative 700-pixel transition plans for 13.6822 ms and applies for
22.6188 ms. It has 785 requests, 431 conditional inherited candidates, 137
actually changed inherited roots, and 690 targeted variable replays. Its single
59.0666 ms dispatch is diagnostic; the stable 240-transition profile above is
the accepted timing evidence.

## Completion boundary

This remains a structural milestone rather than completion of issue #235. The
populated threshold transition is now about 55 ms, still above a 16.7 ms frame
budget. Planning still visits 1,751 inheritance rules and produces 431
conditional inherited candidates, while application still handles 785
requests. The next product-neutral target is reusable inherited-candidate
metadata that reduces those planning candidates without weakening alias,
explicit-`inherit`, pseudo, unknown-property, shadow-boundary, or computed
inheritance correctness.

After the remaining structural candidate is evaluated, the combined SDK still
needs manifest qualification and continuous physical Spotify resizing across
the small-to-large threshold against the same Chromium build. This evidence is
synchronous engine dispatch, not physical-window cadence or a claim of beating
Chrome. The SDK used for profiling is a development overlay, not a qualified
release package.
