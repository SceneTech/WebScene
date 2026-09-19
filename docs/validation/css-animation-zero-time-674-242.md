# Zero-time CSS animation source validation

Issue #674 completes the zero-duration and zero-iteration-count boundary for
WebScene's retained multi-track CSS animation runtime under #242. Both values are
valid CSS. Either makes active duration zero, including `0s × infinite`, while
the animation still has before and after phases and can dispatch completion.
The eight-track cap and coordinated-list rules from #672 remain unchanged.

## Phase and sampling contract

A positive delay retains the before phase. Backwards or both fill samples the
directed start of the first iteration; none or forwards contributes nothing.
Once the delay boundary is reached, the track settles immediately in its after
phase. A zero or negative delay reaches that phase in the same bounded host
update.

For zero iteration count, forwards or both fill also samples the directed start
of the first iteration because no iteration ran. For zero duration with a
positive finite or fractional count, forwards fill uses the terminal progress
for that count and direction. A zero-duration infinite count has zero active
duration; its terminal simple progress follows the Web Animations infinity rule,
with reverse mapped backwards and infinite alternate directions treated as
forwards. Timing is applied only when the resolved progress falls between
retained keyframe stops.

A paused positive delay remains held in the before phase and requests no frame.
Resuming preserves the unconsumed delay, then performs one instantaneous
settlement. A paused zero-time track whose delay boundary is already reached has
no active interval to hold and settles once. Filled results remain retained
without frame demand.

## Events, lifecycle, and bounds

- Every resolved supported track appends at most one `animationend`, with
  `elapsedTime` equal to the zero active duration.
- Tracks completing in the same host update append records in animation-list
  order. Fill or play-state mutations do not duplicate an already emitted end.
- Identity mutation rearms only that track. Removal, detach, navigation, and
  teardown discard retained paint and queued stale records.
- Initial configuration and a zero-time CSSOM phase change invalidate the
  duplicate-timestamp guard once, allowing settlement in the current host
  update. Completed, filled, or paused tracks do not sustain frame demand.
- Sampling remains an eight-track retained scan. No selector matching, cascade,
  layout, timer spin, or visual-tree reconstruction is added to the frame path.

The phase and progress rules align with CSS Animations Level 1 and the Web
Animations active-duration, overall-progress, simple-progress, and direction
algorithms.

## Evidence and deferred scope

`test_zero_time_animation_tracks_settle_without_frame_demand` covers zero
values in shorthand and longhand CSSOM, positive and fractional counts, zero
count, normal/reverse/alternate mapping, all relevant fill phases, a positive
delay, pause/resume, same-update mutation, ordered exactly-once completion,
removal, detach, and demand settlement. The dedicated browser contract provides
the corresponding WPT-aligned observable reduction.

Animation start/iteration/cancel event expansion, additive composition, Web
Animations API objects, more than eight live tracks, additional animated
properties, pseudo-elements, and ABI or AppScene changes remain deferred. Under
the fast implementation policy, only `git diff --check` is executed. Native,
browser, package, and unchanged Code OSS acceptance remain unexecuted.
