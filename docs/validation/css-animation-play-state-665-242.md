# CSS animation play-state source validation

Issue #665 adds bounded `animation-play-state` behavior to WebScene's existing
single-animation opacity, `rotate()`, and compatible foreground-filter keyframe
runtime under #242. The first longhand list item and first shorthand animation
accept `running` or `paused`. `animationPlayState`, the kebab-case CSSOM name,
`getPropertyValue("animation-play-state")`, shorthand serialization, mutation,
and clearing expose the retained value without an ABI or AppScene change.

## Held timeline contract

Play state remains outside keyframe identity. A running-to-paused mutation stores
one bounded host timestamp on the node's existing animation runtime. Opacity,
rotation, and filter tracks sample that held timestamp, so positive delay remains
in its fill-aware before phase and negative delay immediately exposes its active
or terminal sample. No timer or background clock advances the hold.

On resume, unchanged active track origins move forward once by the elapsed held
host duration. Their effective progress therefore continues from the held sample
without rebuilding keyframes or restarting the animation. If supported keyframe
identity changes while paused, changed tracks retain the established restart
behavior while unchanged active tracks preserve their held progress.

A terminal sample reached while paused remains held and cannot dispatch
`animationend`; resume performs the existing finite completion path. The shared
end latch still dispatches at most once. Completed fill remains settled. Removal,
node detach, document replacement, navigation, and engine teardown discard the
node-owned hold with the established animation runtime lifecycle.

## Scheduling and bounds

- Paused keyframes are excluded from `has_active_animations()`, including delay
  and active phases. A play-state mutation can publish its held sample once, but
  does not sustain host-frame demand.
- Resume uses the existing visibility and paint-area gates. Opacity, rotation,
  and filters remain paint-only and do not add selector, cascade, layout, scene
  node, queue, or per-frame allocation work.
- The runtime now configures up to eight coordinated tracks through #672.
  Existing duration, iteration, keyframe-stop, and filter-list bounds remain.
- Translate/scale, pseudo-elements, animation start/iteration/cancel events, and
  Web Animations playback APIs remain deferred. Zero-duration/count end
  semantics are covered by #674.

## Evidence

- `test_animation_play_state_holds_and_resumes_host_time` covers shorthand and
  longhand metadata, CSSOM mutation/clear, positive and negative delay, reverse
  opacity/rotation/filter samples, hold stability, resumed continuity, terminal
  fill, exactly-once `animationend`, detach, and settled frame demand.
- `tests/WebPlatformSubset/contracts/css-animation-play-state.html` is a bounded
  WPT-aligned browser contract for the same observable boundaries. Its dedicated
  profile selects only that contract.
- Under the fast implementation policy, only `git diff --check` is executed.
  Native, browser, package, idle, and unchanged Code OSS acceptance remain
  unexecuted.
