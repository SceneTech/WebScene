# Top-level navigation lifecycle

## Problem

`location.reload()` and same-origin `Location` navigation emitted a native host
request without first dispatching the outgoing document lifecycle. Applications
that flush deferred state from `beforeunload` or `pagehide` lost that state when
the host replaced the realm.

The unchanged Code OSS MCP registry exposed the failure: its encrypted secret
key persisted immediately, but the workspace-scoped `mcpInputs` metadata is
written by the storage service during `pagehide`. After reload, Code OSS could
no longer reopen the saved input.

## Runtime contract

Top-level reload and same-origin navigation now reuse the existing bounded
window-close decision path before a host request is queued:

- dispatch one cancelable `beforeunload` event;
- drain its microtasks;
- stop when the event cancels navigation;
- dispatch one non-cancelable `pagehide` event with `persisted === false`;
- drain its microtasks; and
- enqueue the typed reload or navigation request only after approval.

Repeated terminal decisions remain coalesced by the existing lifecycle state.
Focus and external-window host requests do not enter this path.

## Regression gate

The `webscene_native_top_level_navigation_lifecycle` CTest target exercises:

- `location.reload()` event order and host handoff;
- same-origin `location.href` event order and host handoff; and
- a canceled reload that emits no `pagehide` and no host request.

Validated on macOS arm64 in a Release V8 build with:

```sh
cmake --build /private/tmp/webscene-872-worker-build \
  --target webscene_native_engine_tests -j 3
WEBSCENE_NATIVE_ENGINE_TEST_FILTER=top-level-navigation-lifecycle \
  /private/tmp/webscene-872-worker-build/webscene_native_engine_tests
```

The focused executable completed successfully on 2026-09-20.

Tracking: #269.
