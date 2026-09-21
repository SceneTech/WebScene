# Terminal cancelled storage flush handoff

Tracking: #884, parent #269, predecessor #882 / PR #883.

## Exact product evidence

The unchanged Code OSS 1.137 package built from AppScene
`cd0a02ecd15f0a635bccc242cdd657888db39e25` and WebScene
`2520661b599b8f6f0b9996967bbee816451c0e85` still lost its workspace
`mcpInputs` record across reload after IndexedDB-owned work promotion landed:

```text
mcpStoredInputKeysBeforeReload = ["${input:sceneSecret}"]
mcpProfileInputKeysBeforeReload = []
mcpStoredInputKeys = []
mcpStoredInputPresent = false
```

The integration artifact is
`artifacts/smoke-cd0a02e-2520661-mcp-fixed-1`. Its executable SHA-256 is
`1f729289f7f71727bbee538f08c2491dcce7289dc2262bc083eb124b36a7e4e4`.
The same exact sources pass when the validation overlay explicitly awaits
upstream `IStorageService.flush(SHUTDOWN)` before reload. That isolates the
remaining failure to lifecycle scheduling rather than IndexedDB durability.

## Root cause

During `beforeunload`, VS Code emits its final storage state and schedules a
zero-delay flush. Its browser workbench then synchronously publishes shutdown
and disposes the storage `Delayer`; disposal calls `clearTimeout()` before the
lifecycle event returns. WebScene consequently removed the only task that
could start the IndexedDB write and observed no terminal persistence work, so
`pagehide` and reload handoff proceeded immediately.

After retaining that callback, repeated installed-package runs exposed the
second half of the shutdown race. VS Code also closes its storage database from
a shutdown microtask before the retained timer starts. One run passed in
16.204 seconds, while the next failed in 25.623 seconds with the key present
before reload and absent after reopening; the failed log reported that
`vscode-web-db` was closed.

A focused native fixture reproduces the exact order. Before the fix it reports:

```text
order=[beforeunload, preexisting-commit, pagehide]
committed=false
continuation=false
```

## Runtime contract

A zero-delay, one-shot timer created during an accepted terminal lifecycle is
already charged to WebScene's bounded terminal handoff budget. If synchronous
shutdown disposal clears that handle during the same lifecycle dispatch,
WebScene retains the accepted callback privately until the handoff boundary.
Its microtasks, zero-delay descendants, and IndexedDB operations inherit the
same bounded terminal ancestry.

If `IDBDatabase.close()` runs from that same lifecycle turn while accepted
terminal persistence remains, the compatibility layer queues the close behind
the retained flush. The database still closes before `pagehide` and host
handoff. Calls made outside this narrow terminal condition keep synchronous
close semantics.

This terminal-only durability rule is intentionally narrower than ordinary
timer cancellation. Timers created outside terminal lifecycle, delayed timers,
intervals, animation frames, and work beyond the 1,024-task budget keep their
existing behavior. The focused fixture explicitly verifies that an ordinary
zero-delay `clearTimeout()` still cancels.

## Focused gate

```sh
cmake --build /private/tmp/webscene-882-build \
  --target webscene_native_engine_tests -j 6
WEBSCENE_NATIVE_ENGINE_TEST_FILTER=terminal-indexeddb-navigation \
  /private/tmp/webscene-882-build/webscene_native_engine_tests
```

Result on macOS arm64 Release: pass. The durable transaction-to-handoff gate
completed in 0.0037 seconds. The fixture also covers a shutdown-microtask
database close before the retained flush, restart durability,
pre-existing IndexedDB work, exact lifecycle order, ordinary cancellation,
future timers, intervals, native completion descendants, and the bounded
self-refilling-task ceiling.
