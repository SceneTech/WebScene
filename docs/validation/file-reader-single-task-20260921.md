# Single-task FileReader completion — 21 September 2026

## Product boundary

The exact unchanged Code OSS package at WebScene `62c4c617`, AppScene
`cd0a02ec`, and Code OSS `645f29cc` resolves the selected remote root in
1,219.824 ms, but paints its first Explorer child in 2,530.115 ms. The complete
functional oracle passes: exact selected root, registered provider, five
resolved children, five Explorer model children, five visible rows, and 64
Explorer nodes.

The remaining trace records 90 FileReader operations over 11,833,296 bytes.
Native `Blob.arrayBuffer()` work takes at most 0.0393 ms, while FileReader
call-to-`loadend` takes up to 1,114.299 ms. The compatibility implementation
scheduled one file-reading task for `loadstart`, then a second task for
`progress`, `load`, and `loadend`. Both tasks closed independent
microtask/style batches.

## Focused change

One asynchronous file-reading task now owns `loadstart`, the Blob promise
continuation, and the ordered terminal events. The promise continuation runs at
the browser task's required microtask checkpoint, before the task closes its
style batch. This preserves asynchronous observation, event order, cancellation,
errors, immediate chained reads, and the bounded dedicated FileReader queue,
while removing one task and one style boundary per protocol frame.

## Focused gates

- Node 24.18.1 FileReader compatibility: 8/8 pass.
- Native WebSocket/FileReader gate: 100 socket cycles and 20 product-shaped
  protocol cycles pass.
- Product-shaped native FileReader p95: 0.0253 ms.
- 64-timer backlog: one timer runs before FileReader; completion takes 0.314 ms.
- Native protocol gate reports stable settled V8 heap and bounded queue high
  water marks of one.

The exact installed SDK/package rerun remains the promotion gate for the
two-second Explorer first-paint budget in WebScene #280 and #252.
