# Multiple CSS animation list source validation

Issue #672 adds bounded comma-separated CSS animation coordination to WebScene's
retained opacity, `rotate()`, and compatible foreground-filter runtime under
#242. `animation-name` determines track count and order. Duration, timing,
delay, iteration, direction, fill, and play-state lists repeat modulo their
length; entries beyond the name count do not create tracks. The shorthand
parses every comma-separated item and CSSOM serializes one coordinated item per
name.

## Runtime contract

Each configured track retains an independent identity, host-clock origin, pause
hold, fill state, compatible filter data, and exactly-once end latch. A same-index
track preserves time while only fill or play state changes. Changes to its name,
keyframes, duration, delay, iterations, direction, or timing restart that track.
Removing a track destroys its retained state, so it cannot paint or dispatch a
stale completion after mutation, detach, navigation, or teardown.

Tracks sample in animation-list order. For each supported property, the last
track that contributes in its active or applicable fill phase supplies the
replace-composited value. A later track in an unfilled delay or terminal phase
does not hide an earlier contributing track. Simultaneous finite completions
append one `animationend` per track in list order. A `none` or unresolved name
keeps its coordinated slot but creates no effect or event.

## Bounds and scheduling

- At most eight animation-name entries configure runtime tracks per element.
  Authored CSSOM text remains observable beyond the execution cap.
- Existing stop-count, filter-function, duration, and iteration bounds apply to
  every track. Invalid coordinated values fall back to each longhand's initial
  runtime value.
- List parsing, keyframe lookup, filter compatibility, and track reconciliation
  happen during style configuration. Frame sampling performs one bounded scan
  over retained tracks and does not rerun selector matching, cascade, layout,
  or visual-tree construction.
- Paused, completed, and fill-only tracks do not keep host-frame demand alive.
  Infinite or incomplete running tracks use the existing visibility and paint
  area gates.

## Evidence and deferred scope

`test_multiple_animation_lists_coordinate_bounded_tracks` covers repeated
longhands, multi-item shorthand serialization, independent pause/delay/direction,
last-contributing paint, the eight-track cap, same-frame end ordering, removal,
detach, and frame demand. The dedicated browser contract provides a WPT-aligned
observable reduction for coordination, ordering, serialization, and cleanup.

Additive/accumulate composition, `animation-composition`, Web Animations API
objects, more than eight live tracks, animation start/iteration/cancel event
expansion, additional animated properties, transition/animation compositing
changes, and ABI or AppScene changes remain deferred. Zero-duration/count end
semantics are covered by #674. Under the fast implementation policy, only
`git diff --check` is executed. Native, browser, package, and unchanged Code OSS
acceptance remain unexecuted.
