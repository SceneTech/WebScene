# Beforeunload persistence before pagehide teardown

Date: 2026-09-21  
Issue: [#880](https://github.com/SceneTech/WebScene/issues/880)

## Product evidence

An exact AppScene `cd0a02e` / WebScene `a39280e4` Release reproduced an
immediate VS Code OSS reload after accepting a real MCP password input. The
replacement workbench reported one `beforeunload`, one `pagehide`, and one
reload, but `IMcpRegistry.getSavedInputs(StorageScope.WORKSPACE)` returned no
keys. A copy of the transient native database also contained no `mcpInputs`
record.

VS Code's `beforeunload` handler asks its storage service to flush immediately.
The flush starts through a zero-delay timer. Its synchronous `pagehide` handler
then disposes the workbench and cancels that timer. The terminal persistence
lineage from #878 therefore never reached IndexedDB.

## Runtime change

Top-level terminal lifecycle now keeps `beforeunload` synchronous so veto and
error behavior stays unchanged. When that event creates bounded terminal
persistence work, WebScene delays `pagehide` until the existing #878 lineage
drains. It then dispatches `pagehide` once as its own browser task and waits for
any bounded zero-delay/IndexedDB descendants before exposing the host request.

Intervals, positive-delay timers, animation frames, unrelated work, and tasks
beyond the 1,024-task terminal budget remain excluded. Pending phase state is
cleared on cancellation, failure, successful handoff, navigation reset, and
realm teardown.

## Focused gate

The native fixture models the unchanged VS Code sequence: `beforeunload`
schedules a zero-delay durable write, while `pagehide` clears that timer. It
requires this exact order before the reload request becomes visible:

```text
beforeunload,flush-timer,commit,continuation,pagehide
```

The restarted engine must read the durable value. The retained self-refilling
timer case also consumes the bounded budget and cannot starve navigation.

```text
ctest --test-dir /private/tmp/webscene-878-build \
  -R '^webscene_native_terminal_indexeddb_navigation$' --output-on-failure

1/1 passed
Total real time: 1.21 seconds
```

The exact unchanged-product Release retest is the downstream acceptance gate.
