# Code OSS CSS caret-color implementation (#804)

## Product trigger

Unchanged Code OSS authors `caret-color:
var(--vscode-editorCursor-foreground)` on the EditContext text area. WebScene
previously painted the focused form-control caret with the text foreground.

## Implemented slice

- Adds generated native/managed identity and CSSOM for inherited
  `caret-color`, including `auto`, `currentColor`, CSS-wide values, mutation,
  removal, and cascade fallback.
- Retains the uncommon token in the existing sparse cold style map, so the
  feature adds no hot per-node field.
- Resolves the existing kind-14 caret scene command independently from text
  foreground while preserving element opacity.
- Treats mutation as paint-only and adds no layout invalidation, extra scene
  node, timer, platform widget, document scan, browser shell, or Code OSS
  patch.

## Authored gates

- `contracts/css-caret-color.html`: inheritance, currentColor, computed CSSOM,
  mutation, and removal.
- The existing native focus/blink/caret contract now requires distinct text
  `#123456` and caret `#abcdef` scene colors.
- `css-caret-color-performance`: 4,096 inputs, ten transitions, zero added
  layout, bounded scene publication, and a five-second guardrail.

## Evidence state

Implementation and gates are authored from source review. Per the active fast
merge direction, no build, test, WPT, visual, package, memory, lifecycle, or
performance command was executed. Evidence remains zero until full validation.
