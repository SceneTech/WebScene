# Spotify resize invalidation

The Spotify catalog probe exposed a resize bottleneck in style invalidation rather
than image rasterization or scene serialization. On the September 16, 2026 macOS
profile, one host resize input took 477.423 ms. The first stylesheet recascade used
438.100 ms because viewport-media and attribute changes conservatively recascaded the
whole document. Layout used 22.532 ms and scene construction used about 1.9 ms.

The native runtime now keeps selector-feature indexes with the active cascade.
Attribute and class transitions use those indexes to invalidate matching subjects,
descendants, direct children, adjacent/general siblings, and inherited custom-property
consumers. Viewport changes first identify media rules whose active state changed and
recascade only roots that those rules can affect. Resize listeners batch repeated
attribute transitions by node and attribute, preserving the first state and the final
state until the rendering checkpoint instead of planning every intermediate change.
Relational `:has()` invalidation walks the changed node's ancestor chain to find only
anchors whose match state changed; attributes outside `:has()` continue to invalidate
their ordinary selector subject. Attribute removal and `toggleAttribute()` use the
same transition path.

The same Spotify interaction after this change completed its resize input in 20.703 ms
(about 23 times faster). Attribute invalidation was 1.218 ms and the dominant layout
pass was 12.232 ms. Subsequent retained-scene publications in the captured resize
sequence were typically 2-4 ms. These timings are host observations, not a portable
performance guarantee or an exact Chrome comparison. Every resized scene was laid out
at the current viewport; the optimization does not stretch or interpolate a cached
frame.

A follow-up live profile found two additional costs. Connection checks performed a
provisional-frame scan once per ancestor queried by geometry-heavy JavaScript; they now
find the parent-chain root once and compare provisional roots once. Spotify image-state
selectors containing `:has()` also scanned the complete document twice per mutation.
Across comparable startup-and-resize traces, 276 `data-image-status`/`aria-hidden`
invalidations averaged 5.957 ms before the relational plan. The rebuilt probe recorded
303 corresponding invalidations averaging 0.036 ms (maximum 0.124 ms), reducing their
aggregate planning time from 1.64 seconds to 10.8 ms. The remaining sampled resize work
is primarily real layout and intrinsic sizing.

This follows Blink's broad invalidation architecture: compile selector features into
indexed invalidation data, accumulate invalidations after DOM changes, and recalculate
only marked nodes at a rendering checkpoint. See Chromium's
[`style-invalidation.md`](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/third_party/blink/renderer/core/css/style-invalidation.md)
and [`rule_feature_set.cc`](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/third_party/blink/renderer/core/css/rule_feature_set.cc).

## Regression coverage

- `contracts/css-attribute-invalidation-scope.html` covers attribute addition and
  removal for selector subjects, descendants, direct children, adjacent/general
  siblings, functional `:not()` arguments, inherited custom properties, relational
  `:has()` ancestors, and selector subjects following a `:has()` anchor.
- `contracts/disconnected-element-geometry.html` covers connected, detached, and
  reattached geometry through a 32-level ancestor chain.
- `contracts/media-query-targeted-subtree-recascade.html` covers media-query activation
  and deactivation while verifying that an unrelated sibling stays unchanged.
- `test_attribute_invalidation_scopes_subject_and_descendant_rules` and
  `test_media_query_resize_recascades_only_affected_subtrees` assert the native
  recascade counts as well as rendered behavior. The resize batching regression also
  exercises a descendant attribute transition that changes an ancestor `:has()` match.

Set `WEBSCENE_TRACE_RECASCADE_ROOTS=1` to inspect invalidation planning and
`WEBSCENE_TRACE_LAYOUT_PHASES=1` to split a layout pass into tree, sticky-position,
paint-order, and retained-canvas phases. Use the existing `native-resize-cadence`
benchmark and `scripts/profile-chrome-resize-cadence.mjs` for matched-cadence product
comparisons; do not infer browser parity from a single manual resize.
