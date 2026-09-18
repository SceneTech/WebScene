# FileReader task-source fairness — 18 September 2026

## Product trigger

Unchanged Code OSS 1.137 receives remote protocol frames as `Blob` values and
uses `FileReader` to consume them. Exact packaged workspace acceptance recorded
149 reads over 13,209,295 bytes. The underlying `Blob.arrayBuffer()` work took
1.111 ms in total and at most 0.040 ms, while `FileReader` call-to-`loadend`
took 88,463.629 ms in aggregate and at most 2,219.394 ms.

WebScene's compatibility implementation queued both the read start and terminal
events through `setTimeout(0)`. This placed file-reading work behind the whole
application timer backlog even though file reading is an independent browser
task source.

## Generic fix

The native runtime exposes one private bounded enqueue operation while installing
`FileReader`, then removes it from the global object after the compatibility
implementation captures it. File-reading callbacks use a 1,024-entry FIFO task
source that rotates with Worker, ServiceWorker, MessagePort, and WebSocket work.
Due timers retain their existing alternating fairness. If the bounded native
queue is full, the implementation falls back to its existing timer path.

Navigation and engine disposal clear the pending file-reading queue before V8
contexts are released. Callback execution retains the existing context entry,
exception reporting, inspector lifecycle, style batching, microtask checkpoint,
and runtime-work accounting.

## Direct evidence

The native reduction places 4,096 zero-delay application timers before a
FileReader operation:

| Engine/revision | Timers before `load` | Completion |
| --- | ---: | ---: |
| WebScene `19ad3eb2` plus test only | 4,096 | 57.847 ms; gate fails |
| Candidate `fc0c7b2d` | 2 | 0.405 ms; gate passes |
| Chrome 153.0.8010.50 | 4,096 | 3.8 ms; browser control passes |

Task-source selection between independent sources is implementation-defined, so
the cross-engine contract gates observable latency, bytes, and event order. It
does not require WebScene and Chrome to select the same source first.

- Native and Chrome contract: 48/48 assertions pass.
- Native WebSocket/FileReader focused gate: 100 socket cycles, 20 product-shaped
  protocol cycles, the 4,096-timer reduction, exact event order, bounded queues,
  flat settled V8 heap, and bounded task/wake/scene counts pass.
- Node 24 compatibility contract: 8/8 tests pass, including native-scheduler
  selection, timer fallback, abort, error, sequential read, encoding, listener,
  and 8 MiB byte-integrity coverage.

Retained local evidence:

- `/private/tmp/webscene-287-wpt-native-3/results.json`
- `/private/tmp/webscene-287-wpt-chrome-3/results.json`
- native focused runner output in the implementation log

The unchanged packaged #280/#252 workspace gate must be rerun after this focused
fix merges. It remains the promotion evidence for the five-child Explorer path
and the two-second first-paint budget.
