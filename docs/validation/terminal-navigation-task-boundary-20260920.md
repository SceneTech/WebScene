# Terminal navigation task boundary

Tracking: #876, parent #269.

## Reproduction

The exact unchanged Code OSS package built from AppScene
`cd0a02ecd15f0a635bccc242cdd657888db39e25` and WebScene
`f5df884af35cf0d05eb4864f507ee3e468360e4e` dispatches `beforeunload` and
`pagehide`, but the native reload request is visible to AppScene in the same
JavaScript turn. VS Code's `onWillSaveState` listener queues its IndexedDB
commit with a zero-delay timer. AppScene can therefore retire the realm before
that timer runs. The reopened MCP editor finds its durable encrypted secret but
no workspace-scoped `mcpInputs` record.

#874 supplied the missing lifecycle events and #875 removed same-runtime
IndexedDB commit conflicts. The exact package no longer reports the conflict,
which isolates this remaining task-ordering boundary.

## Runtime contract

Reload and same-origin navigation reserve bounded host-request capacity before
dispatching lifecycle. Approved requests remain in one bounded terminal queue
until every non-animation timer queued no later than the end of lifecycle has
run. WebScene then publishes the terminal requests as one browser task and
notifies the host.

The cutoff is captured after both lifecycle event microtask checkpoints. A
timer queued from either listener, including one queued by a Promise microtask,
therefore precedes handoff. A timer created later by one of those tasks has a
newer deadline and cannot indefinitely delay navigation. Navigation teardown
and runtime shutdown release pending requests and their reservations.

Recursive navigation from a lifecycle listener is coalesced into the outer
request. Reload/navigation veto and error paths release their reservation.
Focus, external URL, window close, nested-document, and same-document behavior
retain their existing task and host contracts.

## Focused gate

The focused Release V8 regression covers reload, same-origin navigation,
listener and Promise-microtask zero-delay work, veto behavior, request order,
one lifecycle dispatch for 1,000 queued terminal requests, and a two-second
performance bound.

```sh
cmake --build /private/tmp/webscene-876-build \
  --target webscene_native_engine_tests -j 3
WEBSCENE_NATIVE_ENGINE_TEST_FILTER=top-level-navigation-lifecycle \
  /private/tmp/webscene-876-build/webscene_native_engine_tests
```

Result on macOS arm64: pass. The 1,000-request gate completed in 0.0058 seconds.
The next acceptance step is rebuilding the exact AppScene/WebScene SDK and
unchanged Code OSS release package, then repeating the two-navigation MCP
persistence smoke.
