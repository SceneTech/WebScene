# CSS animation fill-mode source validation

Issue #661 completes the bounded `animation-fill-mode` behavior for WebScene's
existing single-animation opacity, `rotate()`, and foreground-filter keyframes
under #242. It accepts the first longhand list item and the first shorthand
animation for `none`, `forwards`, `backwards`, and `both`. The CSSOM
`animationFillMode` and `getPropertyValue("animation-fill-mode")` surfaces expose
the configured value without an ABI or AppScene change.

## Timing and fill contract

Fill is configuration around an animation timeline, not part of timeline
identity. A fill-only cascade or CSSOM change retains the original start time.
An already-completed supported animation requests at most one host sample to
apply or remove its static boundary value, then returns to zero frame demand.
The existing `animationend` latch is preserved, so this update cannot dispatch a
second terminal event.

For a positive delay, `backwards` and `both` sample the boundary that starts
iteration zero. `normal` and `alternate` use progress zero; `reverse` and
`alternate-reverse` use progress one. The positive-delay boundary is sampled
directly, without applying the between-keyframe timing function, so
`steps(..., start)` does not jump before the active interval. `none` and
`forwards` expose the underlying property before that interval. A negative delay
has no before phase and enters the active or after phase directly.

After a finite active interval, `forwards` and `both` use the existing
direction-aware integer or fractional terminal progress for opacity, rotation,
and compatible bounded filters. `none` and `backwards` restore the underlying
value. Rotation now retains an explicit filled override just like filters, while
filled overrides remain excluded from the active-animation frame-demand test.
Removing the animation, changing its identity, navigation, and node teardown
clear the override through the established runtime lifecycle.

## Bounds and deferred behavior

- The runtime still configures one animation and the first comma-separated
  longhand value.
- Existing keyframe-stop and compatible filter-list limits are unchanged.
- No timer, queue, visual-tree node, selector pass, or per-frame allocation is
  added. Host timestamps and visibility/clip frame-demand gates are preserved.
- Translate/scale keyframes, pseudo-element animation, animation
  start/iteration/cancel events, and the Web Animations API remain deferred.
  Play state, multiple lists, and zero-time fill are covered by #665, #672,
  and #674.
- Unchanged Code OSS uses `both` for delayed Quick Input, action-widget, and Chat
  entry animations. This slice prevents their supported opacity before-phase
  flash. Their translate/scale keyframes remain outside the rotate-only bound.

## Evidence

- `test_animation_fill_maps_before_after_and_preserves_timeline` covers all four
  values, longhand and shorthand metadata, direction and step timing in the
  before phase, opacity/rotation/filter terminal paint, fill-only mutation,
  exactly-once `animationend`, removal, detach, and settled frame demand.
- `tests/WebPlatformSubset/contracts/css-animation-fill-mode-boundaries.html`
  is a bounded WPT-aligned browser contract with the same before, after, live
  update, and cleanup boundaries. A dedicated profile selects only that test.
- Under the fast implementation policy, only `git diff --check` is executed in
  this slice. Native, browser, package, and unchanged Code OSS acceptance remain
  unexecuted.
