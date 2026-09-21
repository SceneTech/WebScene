# CSS anchor overlay slice for Code OSS webviews — 21 September 2026

WebScene issue #903 records a real installed AppScene/Code OSS Release where
unchanged Markdown Preview opens a tab but its overlay and iframe measure 0×0.
Code OSS authors `anchor-name`, `position-anchor`, `anchor(top/left)`, and
`anchor-size(width/height)` in `overlayLayoutElement.ts`. The baseline native
runtime passed only the independent absolute-inset case (1/5 geometry checks);
Chrome passed all five.

This slice retains the named-anchor tokens in cold style storage and resolves
the used box in the grid, inline, and general positioned layout paths. An
anchor record contains a snapshot of its used rectangle, so a later layout
pass cannot erase its box while resetting inline children. A second tree pass
is taken only when an overlay precedes its anchor in document order. Changes
to a named anchor's CSSOM geometry invalidate the full document, because an
overlay may live outside that anchor's positioned subtree. Ordinary documents
perform no anchor lookups or extra layout pass.

## Focused gates

| Gate | Result |
| --- | --- |
| Chrome 152 headless geometry fixture | 9/9 assertions pass, including CSSOM replacement, resize/move, and 64 panel moves |
| Native V8 Release geometry fixture | 9/9 assertions pass; 64 move-and-relayout cycles stay within the fixture's 500 ms budget |
| Native rendered scene | anchor paint reftest equals its explicit-coordinate reference |
| Chrome rendered scene | anchor paint PNG and explicit-coordinate PNG have identical SHA-256 `494c7a9c7df2a72a770a41d19dcebd8f108f36a417e3afdc5c53220aab82dad7` |
| Existing positioned-height regression | paint and descendant contracts pass; focused combined run 4/4 documents, 15/15 subtests |

The checked-in profile is
`tests/WebPlatformSubset/webscene-css-anchor-overlay-profile.json`. The
browser CDP adapter could not attach to a debuggable page on this host, so the
Chrome geometry fixture was run with headless DOM output and the paint pair
with headless screenshots. The native Release runner produced the checked-in
profile result at `/private/tmp/webscene-anchor-903-visual-native/results.json`;
the temporary combined regression result is at
`/private/tmp/webscene-anchor-903-regression-native/results.json`.

This is the exact function set needed by the current Code OSS overlay, not
general CSS Anchor Positioning conformance. Named-anchor collision scoping,
fallback syntax, and arbitrary `anchor()` forms remain outside this slice.
The earlier signed AppScene package predates this WebScene change; #903 and
downstream AppScene #402 stay open until a new exact SDK/CLI Release paints
Markdown and the product visual, lifecycle, and idle-CPU gates pass.
