# Code OSS CSS will-change implementation (#800)

## Product trigger

Unchanged Code OSS has 16 `will-change` declarations across 12 CSS files.
Transform/opacity hints cover Quick Input, Action Widget, sessions/mobile,
Chat, counters, voice glow, and onboarding. Other hints include
background-position, box-shadow, left, and top.

## Implemented slice

- Adds generated native/managed identity, bounded validation, and CSSOM for
  `auto` or up to eight comma-separated ASCII custom identifiers.
- Retains the normalized authored list in cold textual style and derives one
  packed hot bit when a hinted non-initial property establishes a stacking
  context.
- Reuses the retained atomic stacking-context predicate for paint and hit
  ordering. It does not preallocate a compositor layer.
- Treats mutation as paint-only and adds no layout invalidation, visual-tree
  node, timer, frame participant, platform widget, browser shell, or Code OSS
  patch.

## Authored gates

- `contracts/css-will-change-stacking-context.html`: CSSOM, invalid writes,
  mutation/removal, and overlapping descendant/sibling order.
- `test_css_will_change_stacking_context`: native hit-order mutation contract.
- `css-will-change-performance`: 4,096 nodes, ten transitions, zero added
  layout, bounded scene publication, and a five-second guardrail.

## Evidence state

Implementation and gates are authored from source review. Per the active fast
merge direction, no build, test, WPT, visual, package, memory, lifecycle, or
performance command was executed. Evidence remains zero until full validation.
