# Details content border-image cutoff

Issue: [#756](https://github.com/SceneTech/WebScene/issues/756)

Unchanged Code OSS uses one bounded `border-image` shape on completed-response
`::details-content`: a vertical linear gradient with slice `1`, a solid request
border through `calc(100% - spacing)`, and a transparent terminal segment.

WebScene retains only the balanced gradient image after custom-property
resolution. Scene publication reuses the existing bounded `webscene-bg-v2`
gradient resource on the pseudo's inline-start border rectangle. The rectangle
follows LTR/RTL placement, animated used block size, overflow clipping, and the
pseudo opacity group. Other border-image grammars remain unsupported.

## Gates

- Native focused selector: `details-content-pseudo`
- Reftest candidate: `contracts/css-details-content-border-image.html`
- Exact scene resource: one kind-21 gradient rectangle with resolved variables
- Product evidence: unchanged Code OSS completed-response disclosure in
  `src/vs/workbench/contrib/chat/browser/widget/media/chat.css`

Gate execution is deferred under the current implementation-throughput
directive; the source contracts are committed for the focused PR lane.
