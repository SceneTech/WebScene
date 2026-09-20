# IndexedDB terminal handoff promotion

Tracking: #882, parent #269.

## Exact product evidence

The unchanged Code OSS Release package built from AppScene
`cd0a02ecd15f0a635bccc242cdd657888db39e25` and WebScene
`ba71aa1b4e832c521b81cb3c252c513412ca2c7f` intermittently loses the
workspace `mcpInputs` record across reload. An instrumented acceptance run
proved that the value exists in VS Code memory immediately before reload and
is absent in the replacement realm:

```text
mcpStoredInputKeysBeforeReload = ["${input:sceneSecret}"]
mcpProfileInputKeysBeforeReload = []
mcpStoredInputKeys = []
mcpStoredInputPresent = false
```

The failure artifact is
`artifacts/smoke-cd0a02e-ba71aa1b-mcp-before-reload-probe-1` in the integration
repository. The exact executable SHA-256 is
`876d9eaf704655e0e6a9ee777501c4099cf03dbc9434fd7914bc7301ce3b2ecf`.

## Root cause

VS Code may start its idle storage flush before terminal navigation. That
flush can already have queued an IndexedDB compatibility transaction when
reload begins. The earlier terminal boundary retained work created by the
lifecycle listeners and their descendants, but it deliberately excluded
pre-existing work. JavaScript Promise dependency is not observable at the
host boundary, so the pending transaction could be retired during realm
teardown even though VS Code awaited it during shutdown. Whether the idle
transaction completed first made the product gate intermittent.

## Runtime contract

The IndexedDB compatibility layer schedules its zero-delay transaction work
through a private runtime task entry point. Those tasks are tagged as terminal
handoff candidates without changing the public timer contract. When terminal
navigation starts, WebScene promotes already queued candidate tasks and
already pending native IndexedDB operations into the existing bounded
persistence boundary.

Only IndexedDB compatibility work receives the candidate tag. Intervals,
animation frames, delayed timers, and arbitrary application tasks remain
outside the boundary. Candidate timers consume the existing 1,024-task budget;
native operations retain their existing bounded pending-operation capacity.

## Focused gate

The terminal navigation fixture now queues a real read-write transaction
before `location.reload()`, queues another transaction from `beforeunload`,
and aborts any active pre-existing transaction from `pagehide`. It asserts
that both writes, the native commit, and the commit continuation finish before
handoff. It then restarts the engine and reads both durable values. The
self-refilling task fixture continues to prove that the terminal boundary is
bounded.

```sh
cmake --build /private/tmp/webscene-882-build \
  --target webscene_native_engine_tests -j 3
WEBSCENE_NATIVE_ENGINE_TEST_FILTER=terminal-indexeddb-navigation \
  /private/tmp/webscene-882-build/webscene_native_engine_tests
```

Result on macOS arm64 Release: pass. The durable transaction-to-handoff gate
completed in 0.0037 seconds. The next gate rebuilds the exact AppScene/WebScene
SDK and repeats the unchanged Code OSS two-realm MCP persistence smoke.
