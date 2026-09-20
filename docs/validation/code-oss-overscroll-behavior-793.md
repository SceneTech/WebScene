# Code OSS overscroll behavior implementation (#793)

## Product trigger

Unchanged Code OSS contains 12 `overscroll-behavior` declarations. The
workbench root uses `none`; Markdown, Chat, question carousels, voice input,
sessions/mobile shells, and overlay views use `contain`.

## Implemented slice

- Adds generated native/managed identities and CSSOM accessors for the
  shorthand and both physical-axis longhands.
- Parses and serializes `auto`, `contain`, and `none`, including one/two-value
  shorthand, global keywords, inline mutation, removal, and cascade fallback.
- Stores two compact policies in the existing cold textual style allocation.
- Stops exhausted wheel-chain axes at qualifying retained scroll containers
  for `contain` and `none`, while preserving `auto`, the other axis, root
  viewport ownership, wheel cancellation, scrolling, and scroll events.
- Adds no hot-node field, timer, frame participant, platform widget, document
  scan, browser shell, or Code OSS patch. Policy-only CSSOM mutation causes no
  layout or scene publication.

WebScene does not provide a rubber-band affordance, so this bounded slice does
not invent a visual distinction between `contain` and `none`. Both values stop
scroll chaining as required by the product paths.

## Authored gates

- `contracts/css-overscroll-behavior.html`: shorthand/longhand CSSOM,
  mutation, invalid writes, removal, and cascade fallback.
- `test_overscroll_behavior_wheel_chaining`: nested boundary containment for
  `contain`, `none`, and restored `auto` chaining.
- `overscroll-performance`: 4,096 policies, ten transitions, zero added layout
  and scene publications, and a five-second guardrail.

## Evidence state

Implementation and gates are authored from source review. Per the active fast
merge direction, no build, test, WPT, visual, package, memory, lifecycle, or
performance command was executed. Evidence remains zero until full validation.
