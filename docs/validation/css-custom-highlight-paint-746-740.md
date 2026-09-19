# CSS Custom Highlight retained paint for Code OSS

Issues: WebScene #746, parent #740; depends on #741/#743 and PRs #742/#744

Unchanged Code OSS: `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`

## Product behavior

Code OSS chat find and Markdown inner-diff register named Highlights whose
styles use global `::highlight(name)` rules. This top stack layer resolves
`color` and `background-color`, including root custom properties, intersects
live UTF-16 Range boundaries with retained text fragments, and publishes
background rectangles plus foreground text commands. Overlap order follows
Highlight priority and then registry insertion order.

The implementation adds no DOM nodes and no visual-tree nodes. Registered
changes rebuild one bounded paint ledger and dirty one retained scene;
unregistered Range and Highlight changes remain cold. Text fragments retain
source byte spans during layout so paint does not scan or mutate the DOM and
adds no idle frame work.

## Gates

- `test_custom_highlight_retained_paint_contract` checks variable-backed
  backgrounds, foreground text, and priority command order in the native scene.
- `css-custom-highlight-paint-code-oss.html` is the neutral browser candidate
  for cross-text-node ranges and the two names used by unchanged Code OSS.
- The complete native GitHub stack #745 is validated from this top layer.

## Explicit remainder

The bounded claim covers the global named rules and paint properties used by
Code OSS. Selector-qualified highlight rules, the full Highlight pseudo
cascade, arbitrary DOM mutation adjustment of live Range endpoints, text
decorations, shadows, and every CSS Custom Highlight property remain outside
this issue.
