# Code OSS CSS text-wrap implementation (#802)

## Product trigger

Unchanged Code OSS has 10 `text-wrap` declarations across 9 CSS files. The
bounded values are `nowrap`, `wrap`, and one `initial` reset across diff and
placeholder text, hovers, Changes, Chat status/anchors/code pills, and notebook
content.

## Implemented slice

- Adds generated native/managed identity, validation, and CSSOM for the
  product-used `wrap|nowrap` mode plus CSS-wide reset/inheritance behavior.
- Retains the inherited mode in cold textual style independently from
  `white-space`; computed `white-space` therefore remains browser-shaped.
- Composes the mode with the existing retained inline/layout/scene wrap
  decision, including word-break and overflow-wrap behavior.
- Uses ordinary coalesced layout invalidation and adds no timer, frame
  participant, platform text widget, document scan, browser shell, or Code OSS
  patch.

`balance`, `pretty`, `stable`, and the complete Text Level 4 shorthand grammar
remain outside this bounded slice.

## Authored gates

- `contracts/css-text-wrap-mode.html`: inheritance, computed CSSOM, invalid
  writes, mutation/removal, overflow, and height change.
- `test_css_text_wrap_layout`: native inheritance and retained geometry.
- `css-text-wrap-performance`: 4,096 text nodes, ten forced layout checkpoints,
  bounded layout/publication, and a five-second guardrail.

## Evidence state

Implementation and gates are authored from source review. Per the active fast
merge direction, no build, test, WPT, visual, package, memory, lifecycle, or
performance command was executed. Evidence remains zero until full validation.
