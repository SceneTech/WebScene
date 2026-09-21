# Iframe URL reflection and nested history — 21 September 2026

The native `iframe-navigation-lifecycle` gate fails on clean merged WebScene
`06d6a4f5` and `99cf2f86`: its `src` attribute is silently rewritten from
`./fake.html?id=allowed` to an absolute URL after remote navigation.
The gate otherwise observes 101 beforeunload events and the ordered
pagehide/visibilitychange/unload sequence. This establishes #896 as a
merged-main bug, separate from the merged parent-messaging PR #895.

The focused dependent changes are #896 (`61415b87`: preserve the authored
attribute and store the pinned resolved URL separately for fetching,
origin checks and speculative preparation), #897 (`1fe801ca`: resolve
fragment-only and query-only references against the current document path),
and #898 (forward `popstate`/`hashchange` to a same-origin stable WindowProxy,
retain at most 128 listeners per frame/event and release them at detach,
while refusing to construct an owner document for an opaque proxy).
Cross-document traversal may serve one of its four document loads from a
fresh origin-partitioned cache entry; the gate accepts three or four
network requests while requiring four exact page/document replacements
and the browser-order lifecycle events.

On the top branch, a native Release build on macOS with the portable local
V8 monolith and V8 Inspector disabled passed:

| Focused gate | Result |
| --- | --- |
| `iframe-navigation-lifecycle` | 100 create/load/write/replace/detach cycles, 1 MiB HTML, p95 3.63 ms, heap 1,849,528 bytes before and after; opaque and cross-origin DOM denial and history checks passed |
| `iframe-cross-origin-parent-message` | 71.36 ms; exact-origin transfer, blocked parent DOM and source identity passed |
| `history-same-document` | pass; no unintended layout or scene work |
| `query-iframe-worker-bootstrap` | 20 cycles, p95 145.27 ms, heap 1,849,540 bytes before and after |

The direct URL assertions cover `#one` retaining the path and query,
`?next=1#two` replacing the query without dropping the path, and ordinary
relative sibling traversal. Upstream Linux native/portable V8 and metadata
CI on the top PR remain required before merging the stack. The unchanged
Code OSS installed Release must then be rebuilt from exact merged SDK heads
for Markdown visuals and runtime acceptance under #264/#267; this native
fixture alone does not qualify the webview pane.
