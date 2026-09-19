# Bounded Web Animations API

Issue #677 adds a product-neutral Web Animations layer over WebScene's retained
host-clock animation tracks. `Element.animate()`, `Animation`,
`KeyframeEffect`, element/document `getAnimations()`, `currentTime`,
`play()`, `pause()`, `cancel()`, `finish()`, `ready` and `finished` are exposed
without an Electron or application shim.

The first slice accepts two to sixteen ordered keyframes and compiles opacity
and the existing bounded two-dimensional translate, scale and rotation grammar
once. Duration and absolute delay are capped at one hour, finite iteration
count at one million, and the combined CSS/WAAPI track count at eight per
element, with a 1,024-animation document registry ceiling. Easing is limited
to the five supported keyword curves. Property
indexed keyframes, partial-property interpolation, filters, additive
composition, playback-rate mutation and arbitrary timelines remain explicit
later scope.

WAAPI tracks sample the same monotonic host timestamp as CSS animations and
replace-compose after CSS tracks in creation order. Paused, canceled, finished
and statically filled tracks do not request frames. Cancellation rejects
`finished` with `AbortError`; natural or explicit completion resolves it and
dispatches one finish event. Track removal, subtree detach and document reset
clear native ownership and stale paint. CSS mutations that consume the shared
eight-track budget cancel newest excess WAAPI tracks.

`new Animation(effect)` remains idle with a null timeline. Passing the owning
document timeline admits the bounded effect, while `Element.animate()` selects
that timeline and starts it. Negative finite `currentTime` values round-trip;
seek or replay discards a stale queued finish notification and renews a settled
`finished` promise for the new playback generation. The runtime `EventTarget`
owns `onfinish` and `oncancel` delivery so handlers and listener callbacks fire
once.

The native regression covers identity, midpoint paint, seek, pause/resume,
finish, fill, detach cancellation and frame demand. The browser contract uses
the same public API shape. Only `git diff --check` was run under the directed
fast policy; compilation and runtime execution remain for CI.
