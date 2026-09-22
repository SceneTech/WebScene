# CSSOM-created anchor overlay regression — 22 September 2026

## Product reproduction

The exact installed-SDK/CLI Code OSS Release using WebScene `406bc88a` and
AppScene `36d7a82e` still opens a blank Markdown Preview. The unchanged
workbench creates its overlay at runtime. Its two named targets have nonzero
rectangles: editor part `[348, 61, 792, 857]` and preview editor
`[744, 96, 396, 822]`; the clipping root, overlay content, and iframe remain
0×0. A diagnostic fixed-position probe whose `top`, `left`, `width`, and
`height` are assigned through JavaScript named style setters also measures
0×0 for either target. A direct pixel-sized probe measures
`[17, 19, 53, 37]`, while a stylesheet-defined named-anchor control measures
`[27, 29, 43, 47]`. Thus the problem is the CSSOM-created anchor-function
path, not target geometry, generic positioned layout, or absence of the
anchor implementation in the package. The native screenshot confirms the
blank preview pane.

## Cause and fix

The V8 `CSSStyleDeclaration` named-property setter updates numeric inset and
size fields but did not retain `anchor()` / `anchor-size()` tokens. When a
detached element is appended, CSS recascade also cleared cold effect tokens;
ordinary inline dimensions are intentionally not replayed, so their anchor
functions disappeared even if the setter had retained them. Retain tokens in
the six named setters, then restore only authored inline anchor functions
after recascade clears cold values. The restoration path is guarded by an
existing anchor token, avoiding six authored-style lookups for ordinary nodes.
No VS Code source change or browser shell is involved.

## Focused gates

| Gate | Result |
| --- | --- |
| Chrome headless dynamic CSSOM contract | 9/9 assertions pass |
| Native V8 Release before this fix | 2/9 assertions pass; dynamic overlay 0×0 |
| Native V8 Release with this fix | 9/9 assertions pass |
| Existing named-anchor geometry contract | 9/9 assertions pass |
| Dynamic panel-move budget | 64 moves remain below 500 ms in the native and Chrome contracts |

The contract is
`tests/WebPlatformSubset/contracts/css-anchor-overlay-cssom.html` and is
included in `webscene-css-anchor-overlay-profile.json`. Product acceptance
still requires an exact rebuilt SDK/CLI package with painted Markdown,
visual comparison, iframe lifecycle, and idle-CPU gates before #903 closes.
