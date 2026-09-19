# Details content keyword-size transition

Issue: [#754](https://github.com/SceneTech/WebScene/issues/754)

Unchanged Code OSS uses `::details-content` in its completed-response and
checkpoint disclosures. Both transition `block-size` between `0` and `auto`,
fade opacity, and defer `content-visibility` with `allow-discrete`.

The native runtime keeps the intrinsic content subtree laid out during an
active transition and stores only a used block-size fraction and used opacity
on the owning `details` node. Each host frame updates those two scalar values,
marks layout only when block size changes, and clips retained child paint to the
used pseudo-element rectangle. Selector matching and cascade do not run per
frame. Closing content remains connected until the final frame and is then
suppressed by the existing static disclosure path.

## Gates

- Focused native selector: `details-content-transition`
- Static regression selector: `details-content-pseudo`
- WPT candidate: `contracts/css-details-content-keyword-transition.html`
- Performance invariant: unrelated nodes allocate no animation runtime; frame
  demand ends when both pseudo transitions complete
- Product evidence: the two unchanged disclosure rule groups in
  `src/vs/workbench/contrib/chat/browser/widget/media/chat.css`

Validation is intentionally deferred for this implementation checkpoint per
the current integration directive. The focused PR must record exact runner
results before merge.
