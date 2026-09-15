# Code OSS native runtime compatibility

Status: development integration. Code OSS 1.137.0's complete native
workbench/editor smoke passes against the development runtime. The typed desktop
capability revision still requires a fresh combined package qualification. This
work does not add or depend on Electron, CEF, WKWebView, or a browser process.

The reference consumer is
[`SceneTech/vscode-demo`](https://github.com/SceneTech/vscode-demo). It starts
the upstream Code OSS web server as an owned local Node process and presents its
web workbench through an AppScene window backed by WebScene's V8, DOM, CSS,
layout, and scene engine.

## Compatibility slices moved into WebScene

The runtime now installs these adapters in top-level and iframe realms after
the underlying WebScene Blob, DOM, stylesheet, event, and timing objects exist:

- a bounded retained User Timing timeline, including `getEntries`,
  `getEntriesByType`, `getEntriesByName`, marks, measures, and clears;
- asynchronous `FileReader` reads over WebScene's byte-preserving Blob;
- top-level stylesheet insertion, deletion, live rule access, and style-rule
  declaration mutation through the owning native style element.

These are transitional WebScene implementations with focused regression tests.
They remove application-owned global patches while preserving the native
renderer as the destination of stylesheet mutations. Product-independent
native implementations and their complete acceptance boundaries are tracked in
[Performance Timeline #72](https://github.com/SceneTech/WebScene/issues/72),
[FileReader #73](https://github.com/SceneTech/WebScene/issues/73), and
[stylesheet CSSOM #74](https://github.com/SceneTech/WebScene/issues/74).

## Authentication boundary

The resource response ABI still cannot transport general HTTP status/headers
or `Set-Cookie`, and the engine cookie jar is not browser scoped. The reference
consumer therefore still needs a narrowly validated loopback token bridge for
its WebSocket authentication. The durable response/cookie design and security
tests are tracked in
[resource ABI and cookies #75](https://github.com/SceneTech/WebScene/issues/75).
The token-specific bridge must remain outside WebScene until that general
contract exists.

## Native desktop capability boundary

WebScene exposes standard browser behavior instead of an Electron emulation
layer. The versioned `webscene_host_request_v1` ABI carries external HTTP(S)
URLs, clipboard reads/writes, window focus/close/reload, and fullscreen
enter/exit. Hosts lease immutable request memory and release it explicitly.
Clipboard byte payloads therefore avoid base64 and JSON allocation. The older
bounded JSON queue remains available for application messages and compatibility
with earlier hosts.

`navigator.clipboard` supports Promise-based text and typed operations with a
16 MiB representation limit, at most 16 pending completion-bearing operations,
explicit MIME rejection, recent native user activation for reads, and
navigation cancellation. Window requests share a bounded queue. Native hosts
can publish focus and fullscreen state back into the active document, which
updates `document.hasFocus()`, fullscreen state, and their standard events.
Scripted close dispatches cancelable `beforeunload` before reaching the host.

The Code OSS `server-web` target does not load Electron main/sandbox entry
points. Its shipped `out/vs` tree has no direct `electron` module import, so a
general Electron shim would add surface area without helping DOM, Monaco,
layout, input, worker, rendering, or scheduling performance. AppScene owns the
operating-system side of the typed ABI. Menus, dialogs, notifications,
secondary windows, power events, and protocol registration remain
evidence-gated capabilities; they are not installed speculatively.

## Open PR interaction

- WebScene #70 changes CSS compilation/cache representation. CSSOM publication
  must invalidate and reuse that cache without storing document cascade state.
- WebScene #71 and AppScene #63 add build-time V8 code caches. Code OSS still
  requires full V8/JIT and original JavaScript sources; code caching is an
  optional startup optimization after runtime compatibility passes.
- Neither PR provides the APIs above, so this branch remains independently
  reviewable and can be rebased after either change lands.

## Qualification contract

A Code OSS package is qualified only when the native application renders the
real workbench, opens an upstream editor model, types a marker, observes that
marker in native rendered lines, and verifies upstream undo. Fetching the page,
starting Node, evaluating JavaScript, or rendering a test-owned element is not
sufficient. Retain runtime diagnostics and a native screenshot with every
failed smoke run.

The current adapters do not certify extension webviews, transferable
MessagePorts, persistent IndexedDB storage, complete HTTP/fetch behavior,
accessibility, IME, drag and drop, or all extension APIs.

## Quality and performance gates

`tests/RuntimeCompatibility` is part of the three-platform CI matrix. It checks
byte-exact FileReader results and event order, bounded timeline semantics,
native stylesheet publication, malformed input, abort/failure paths,
idempotence, and isolation between reused V8 contexts. The performance budgets
are deliberately much larger than current measurements so they reject severe
regressions without turning temporary hosted-runner load into a flaky result:

| Operation | CI budget | macOS ARM64 | Ubuntu ARM64 VM | Windows 11 ARM64 VM |
| --- | ---: | ---: | ---: | ---: |
| Read an 8 MiB Blob | 3,000 ms | 9 ms | 528 ms | 33 ms |
| Retain 50,000 marks | 1,500 ms | 54 ms | 239 ms | 75 ms |
| Publish 450 stylesheet mutations | 2,000 ms | 19 ms | 71 ms | 23 ms |
| Complete 10,000 one-byte typed clipboard writes | 10,000 ms | pending latest package matrix | pending latest package matrix | pending latest package matrix |
| Drain 10,000 typed window requests | 5,000 ms | pending latest package matrix | pending latest package matrix | pending latest package matrix |
| Apply 10,000 native focus transitions | 1,000 ms | pending latest package matrix | pending latest package matrix | pending latest package matrix |

The clipboard load gate uses batches of 16, reports that pending-request high
water mark, completes every Promise, and verifies an empty queue without timing
sleeps. A separate gate moves the maximum 16 MiB clipboard payload through the
typed ABI within five seconds. Native contracts also cover no-activation reads,
unsupported input, cancellation, queue saturation, stale/double completion,
navigation teardown, close veto, and native-initiated fullscreen changes.

Measurements were recorded on 2026-09-15 with Node 25.1.0 on macOS, Node
18.19.1 in Ubuntu 24.04, and Node 24.19.0 in Windows 11. The VM runs used the
same uncommitted source through read-only host shares. Native V8 engine tests on
macOS, Linux, and Windows remain required because these JavaScript fixtures do
not exercise V8 handle and context lifetimes.
