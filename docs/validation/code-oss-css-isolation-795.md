# Code OSS CSS isolation implementation (#795)

## Product trigger

Unchanged Code OSS uses `isolation:isolate` on five retained surfaces: the
pixel spinner, sessions workbench/part, Chat session container, and voice glow.

## Implemented slice

- Adds generated native/managed identity, validation, and CSSOM exposure for
  `isolation:auto|isolate`, including global keywords, mutation, removal, and
  cascade reset.
- Retains one packed stacking-context bit beside the existing transform and
  containment flags; no per-node token string or map is allocated.
- Includes isolation in the existing atomic stacking-context predicate shared
  by retained paint and hit ordering.
- Treats mutation as paint-only and adds no layout invalidation, timer, frame
  participant, platform widget, visual-tree node, browser shell, or Code OSS
  patch.

## Authored gates

- `contracts/css-isolation-stacking-context.html`: initial/computed CSSOM,
  invalid writes, mutation/removal, and overlapping descendant/sibling order.
- `test_css_isolation_stacking_context`: native hit-order mutation contract.
- `css-isolation-performance`: 4,096 nodes, ten transitions, zero added layout,
  bounded scene publication, and a five-second guardrail.

## Evidence state

Implementation and gates are authored from source review. Per the active fast
merge direction, no build, test, WPT, visual, package, memory, lifecycle, or
performance command was executed. Evidence remains zero until full validation.
