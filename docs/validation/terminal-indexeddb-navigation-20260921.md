# Terminal IndexedDB navigation persistence

Tracking: #878, parent #269.

## Reproduction

The exact unchanged Code OSS Release package built from AppScene
`cd0a02ecd15f0a635bccc242cdd657888db39e25` and WebScene
`a7943723d7b33dd50de2a4ef323cde4dd8500905` reaches the first browser task
after `beforeunload` and `pagehide`, then exposes reload to AppScene while an
IndexedDB transaction descended from that task is still committing. The next
realm retains the encrypted MCP key but has no workspace `mcpInputs` record.

The absence of the earlier IndexedDB revision-conflict diagnostic confirms the
serialized commit fix in #875 is active. Lifecycle counters confirm #874 and
#877 are active. The remaining boundary is the descendant transaction and its
native asynchronous load/store completion.

## Runtime contract

Non-repeating zero-delay tasks scheduled by terminal lifecycle listeners,
their microtasks, or another tagged task inherit a terminal-persistence tag.
Native IndexedDB work started in that lineage inherits the tag through storage
completion and Promise continuations. WebScene keeps the reserved terminal
reload/navigation request private until tagged timers and IndexedDB operations
drain.

Intervals, animation frames, delayed timers, pre-existing work, and unrelated
IndexedDB operations do not hold navigation. A 1,024-task lineage budget
prevents a self-refilling zero-delay chain from starving the host handoff.
Veto, error, shutdown, and successful publication clear the remaining budget
and preserve the existing bounded host-request ownership.

## Focused gate

`webscene_native_terminal_indexeddb_navigation` creates a real durable
IndexedDB partition, starts a write from a lifecycle zero-delay task, includes
a Promise and nested timer after native commit, and requests reload. It asserts
that the native reload is unavailable until the transaction and continuation
finish, restarts with a different loopback port, and reads the durable value.
It also includes unrelated interval and delayed timers and a two-second bound.

```sh
cmake --build /private/tmp/webscene-sdk-build-f5df884a \
  --target webscene_native_engine_tests -j 3
WEBSCENE_NATIVE_ENGINE_TEST_FILTER=terminal-indexeddb-navigation \
  /private/tmp/webscene-sdk-build-f5df884a/webscene_native_engine_tests
```

The next product gate rebuilds the exact AppScene/WebScene SDK and unchanged
Code OSS Release package, then repeats the two-realm MCP persistence smoke.

## Result

The focused macOS arm64 Release gate passed. Durable commit-to-handoff took
0.0036 seconds. The related 1,000-request navigation gate also passed in
0.0044 seconds. The self-refilling zero-delay fixture exhausted the 1,024-task
lineage budget and still exposed reload while leaving its later untagged work
outside the persistence boundary.
