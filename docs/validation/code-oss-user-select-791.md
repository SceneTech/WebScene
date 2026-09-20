# Code OSS user-select interaction implementation (#791)

## Product trigger

Unchanged Code OSS contains 276 `user-select` declarations across 94 files.
They control native selection in Monaco, lists, dialogs, hovers, terminal,
notebooks, Chat, media preview, Simple Browser, and webview content.

## Implemented slice

- Adds generated native/managed identities for `user-select` and the WebKit/MS
  aliases used by the product.
- Cascades and serializes `auto`, `text`, `none`, and `all` in cold style state.
- Resolves `auto` from the nearest ancestor selection policy during pointer
  default actions, avoiding a field on every hot node.
- Creates document Selection state from retained-text caret hit testing on
  pointer down/move/up.
- Suppresses `none`, permits explicit descendant `text`, atomically selects the
  nearest `all` subtree, and honors canceled `selectstart`.
- Reuses existing Selection/Range, selection paint, copy, and outbound-drag
  paths; CSSOM mutation is paint-only. Non-collapsed selections use the
  existing `Highlight` and `HighlightText` system colors by default, while
  matching author `::selection` declarations override those defaults.
- Clears active pointer-selection state during navigation and teardown.

Programmatic Selection and `Window.find` remain independent of `user-select`,
matching the property's user-interaction boundary. Text controls retain their
dedicated UTF-16 editing selection implementation.

## Authored gates

- `contracts/css-user-select-interaction.html`: canonical/prefixed CSSOM,
  invalid writes, mutation, removal, and cascade fallback.
- `test_user_select_pointer_default_action`: drag selection, `none`, `all`,
  and cancelable `selectstart`.
- `user-select-performance`: 4,096 nodes, ten policy transitions, zero added
  layout passes, and a five-second guardrail.

## Evidence state

Implementation and gates are authored from source review. Per the active fast
merge direction, no build, test, WPT, visual, package, memory, lifecycle, or
performance command was executed. Evidence remains zero until full validation.
