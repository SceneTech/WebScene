# CSS animation direction source validation

Issue #659 adds the four CSS `animation-direction` values needed by unchanged
Code OSS to WebScene's existing single-animation runtime under #242. Both the
longhand and the first animation in the shorthand accept `normal`, `reverse`,
`alternate`, and `alternate-reverse`. This is an internal runtime extension and
does not change the WebScene ABI or require an AppScene change.

## Runtime contract

The cascade stores the authored direction list and the bounded runtime selects
only its first value, matching the existing first-animation scope. Direction is
part of every opacity, rotation, and filter keyframe signature, so a real
direction change restarts the animation while an unrelated recascade with the
same signature does not.

Sampling derives the zero-based iteration from the host frame timestamp after
applying the animation delay. `reverse` flips every iteration, `alternate`
flips odd iterations, and `alternate-reverse` flips even iterations. The mapped
progress is passed to the existing timing and keyframe interpolation path for
opacity, `rotate()`, and bounded filters. Negative delays therefore enter the
correct cycle and parity without another timer or task.

Finite integer counts terminate at progress one in the preceding iteration.
Finite fractional counts terminate at their fractional progress in the current
iteration. The direction mapping is applied at that boundary before the
already-supported `forwards` or `both` fill sample is retained. Infinite counts
continue to wrap on the host clock. Completed, detached, hidden, reduced-motion,
and offscreen animations retain the existing lifecycle and frame-demand rules.

## Bounds and deferred behavior

- One configured animation and the first comma-separated longhand value remain
  the runtime bound.
- Keyframe property, stop, filter-list, and frame-demand limits are unchanged.
- `animation-play-state`, backwards fill, multiple-animation composition, the
  Web Animations API, and additional animated properties remain deferred.
- Invalid or unsupported direction text maps to the initial `normal` behavior
  inside configuration and cannot create new runtime state.

## Evidence

- `test_animation_direction_maps_cycles_and_terminal_boundaries` is a
  deterministic native regression for all four directions, shorthand and
  longhand metadata, opacity/rotation/filter sampling, a negative delay, a
  finite integer and fractional terminal fill, and detach cleanup.
- `tests/WebPlatformSubset/contracts/css-animation-direction.html` is a bounded,
  WPT-aligned browser contract for first- and second-cycle parity plus the
  fractional terminal boundary. Its dedicated profile caps the run to one
  required contract.
- Per the fast implementation policy, only `git diff --check` is executed in
  this slice. Native, browser, and package acceptance remain unexecuted.
