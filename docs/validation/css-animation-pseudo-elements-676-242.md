# CSS generated-pseudo animation source validation

Issue #676 extends WebScene's retained, eight-track CSS animation runtime under
#242 to generated `::before` and `::after` boxes. The originating element owns
each bounded pseudo timeline and receives its animation event with the standard
`pseudoElement` identity. No pseudo node or public ABI surface is introduced.

## Cascade and retained state

Animation shorthand and longhands cascade into the existing generated-pseudo
style record. Each eligible generated box compiles its maximum eight tracks
once after cascade, using the same bounded opacity, two-dimensional translate,
scale, rotation, and filter keyframes as an ordinary element. The retained
signature includes name, list-coordinated timing, delay, iteration, direction,
fill, play state, and compiled stops. A changed signature restarts only that
pseudo track; removing its rule, `content`, generated box, or originating
element clears its contribution and queued completion.

Sampling uses the host animation clock and the existing direction, fill,
pause, finite, fractional, infinite, and zero-time phase rules. Retained scene
commands bracket only that pseudo box for scale, rotation, and filters;
translation adjusts its retained box coordinates and animated opacity applies
to its generated paint. The principal element and the sibling pseudo box are
not included in those effect brackets.

## Ownership, lifecycle, and bounds

`animationend` dispatches exactly once on the originating element with
`pseudoElement` set to `::before` or `::after`. Identity mutation removes stale
queued pseudo events. Disconnected owners do not advance or request frames;
navigation and document teardown discard nodes, runtime tracks, and events.
Paused, completed, and fill-only pseudo tracks remain static without host-frame
demand.

Cascade and keyframe compilation occur only when style changes. A frame scans
at most eight retained tracks for each of the two generated boxes. Sampling
does no selector matching, cascade, keyframe parsing, DOM construction, or
visual-tree mutation. Allocation metrics include pseudo track and filter
storage.

## Evidence and deferred scope

`test_generated_pseudo_animations_share_owner_lifecycle` covers a running
`::before`, paused `::after`, retained midpoint transform paint, exact owner and
`pseudoElement` completion data, static fill, rule/content removal, detach, and
settled frame demand. The browser contract checks the same before/after
midpoint and owner-event shape against standard browser semantics.

Transitions on generated pseudo boxes, animationstart/animationiteration/
animationcancel expansion, CSS Animations on other pseudo types, Web Animations
API pseudo targets, additive composition, and properties outside the existing
bounded retained keyframe set remain deferred. Under the fast implementation
policy, only `git diff --check` is executed. Native, browser, package, and
unchanged Code OSS acceptance remain unexecuted.
