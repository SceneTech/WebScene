# Code OSS native runtime compatibility

Status: development integration. The reference consumer runs the unmodified
Code OSS 1.137 browser workbench and Node server in an AppScene window backed by
WebScene. It does not load Electron, CEF, WKWebView, or a browser process.

The reference consumer is
[`SceneTech/vscode-demo`](https://github.com/SceneTech/vscode-demo). AppScene
owns native application lifecycle and operating-system integration. WebScene
owns V8, DOM, CSS, layout, browser APIs, input dispatch, and scene rendering.
Application-specific launch and acceptance logic remains in vscode-demo.

## Integration rule

Code OSS stays on its supported `server-web` browser-workbench path. Do not port
VS Code to C++ and do not add a general Electron emulation layer. Generic browser
behavior belongs in WebScene, generic native application behavior belongs in
AppScene, and Code OSS changes should be avoided unless an upstream browser
contract requires them.

AppScene's developer guide may recommend a C++ port for small applications that
want direct native ownership. That option is unsuitable for Code OSS: the
existing web workbench is the product, and replacing it would fork its editor,
extension host, accessibility, worker, and browser-service architecture.

## Implemented WebScene compatibility

Current `main` includes the product-independent slices exercised by Code OSS:

- native Blob/FileReader, retained Performance Timeline, live stylesheet CSSOM,
  DOM traversal and collection behavior;
- browser-shaped `HTMLAnchorElement` constructor/prototype identity in top-level
  and iframe realms, including stable `<a>` wrappers and namespace rejection;
- exact rounded-surface scene commands for structured SVG CSS backgrounds,
  including authored size, position, repeat, radii, and selection mutation;
- dedicated workers, MessageChannel/MessagePort, structured clone, transferable
  ownership, iframe realms, ordered errors, termination, navigation cleanup, and
  origin-scoped Blob object URLs shared across nested worker runtimes;
- bounded native clipboard and window requests, focus/fullscreen publication,
  browser-compatible keyboard events, user activation, and clipboard shortcuts;
- compiled CSS caches, bounded invalidation dependencies, animation and rendering
  opportunity fixes, selector-cache lifetime protection, containment/container
  queries, content visibility, host accessibility preferences, and parsed mask,
  clip-path, filter, and backdrop-filter values;
- OS-backed secure random and the currently qualified SHA, AES-GCM, AES-CBC, and
  HMAC Web Crypto provider slices, including the installed SDK's transitive
  static crypto link closure;
- durable IndexedDB object-store records and cursors using an atomic per-partition
  disk snapshot;
- browser-compatible loopback fetch, WebSocket, response, cookie, and redirect
  foundations used by the Node server path, including empty-to-nonempty socket
  wakeup and fair Worker/MessagePort/WebSocket source rotation;
- ServiceWorker module registration, install/activate/update, controller change,
  `skipWaiting`, `claim`, lifecycle teardown, and controlled-navigation state.
  Clients messaging remains in draft PR #281 and is not claimed by `main`.

Each slice has a focused native or WPT-derived regression. Performance-sensitive
changes also have bounded load tests that assert queue, memory, cascade, layout,
or frame-publication behavior instead of relying only on wall-clock timing.

## Desktop capability boundary

The versioned `webscene_host_request_v1` ABI carries external URLs, clipboard
reads and writes, window focus/close/reload, and fullscreen enter/exit. Hosts
lease immutable request memory and release it explicitly. Clipboard byte payloads
therefore avoid base64 and JSON allocation. The older bounded JSON queue remains
available for compatibility with earlier hosts.

`navigator.clipboard` supports Promise-based text and typed operations with a
16 MiB representation limit, at most 16 pending completion-bearing operations,
explicit MIME rejection, recent user activation for reads, and navigation
cancellation. Native Ctrl/Command-C, X, and V input dispatches standard bubbling,
cancelable clipboard events. Window requests use a bounded queue. Scripted
`window.close()` dispatches cancelable `beforeunload` before reaching the host.

Native operating-system window close uses a separate versioned decision ABI. It
runs `beforeunload` and its microtask checkpoint, reports allow, veto, or error to
the host, and publishes `pagehide` only after an allowed close. Packaged consumers
must still prove that host wiring avoids re-entering the final close path.

Menus, dialogs, notifications, secondary windows, power events, and protocol
registration remain evidence-gated capabilities. They should be added through
small typed host contracts only when a reachable Code OSS feature needs them.

## Storage boundary

The merged IndexedDB slice persists database, store, record, and cursor state
across engine restart and serializes commits atomically. The broader standards
boundary remains open for Blob/File restoration, indexes, key paths and key
generators, IDBKeyRange, reverse and mutating cursors, complete transaction and
cross-context coordination, clear-data controls, released runtime packages, and
packaged-product acceptance.

## Network and authentication boundary

Code OSS uses a local Node server and authenticated loopback WebSocket. WebScene
preserves HTTP status and headers, response cookies across bounded same-origin
redirects, clean URL identity, and WebSocket authentication without
application-owned browser patches. Nested workers share bounded, origin-scoped
Blob object URLs, so an extension-host worker can return a child-worker URL to its
creator without resolving a colliding outer module source. Packaged acceptance
must cover the real management and extension-host sockets, same-origin policy,
token redaction,
shutdown, and reconnect behavior.

## Qualification contract

A Code OSS package is qualified only when the native application:

1. renders the real workbench and matches the Chromium reference geometry and
   versioned pixel-difference budgets;
2. opens a real upstream editor model, types a marker through native input,
   observes it in WebScene-rendered lines, and verifies undo/save/reload;
3. exercises Explorer, Search, Settings, command palette, integrated terminal,
   clipboard, dialogs, external links, focus, fullscreen, and normal close;
4. proves management and extension-host socket authentication and clean process
   shutdown;
5. stays within the versioned CPU, frame, latency, memory, artifact, bundle-size,
   temporary-storage, and startup budgets;
6. passes the focused native and WPT-derived regressions for every included
   WebScene change.

Fetching the page, starting Node, evaluating JavaScript, or rendering a
test-owned element is insufficient. Failed runs retain bounded diagnostics and a
native screenshot.

## Pull-request and stack policy

Product-independent fixes land as focused WebScene pull requests. Related PRs
use GitHub's native stack relationship; the cumulative top is the validation
target and the stack merges atomically once the changed behavior passes on the
required runner. Broad product/platform validation runs after major stacks and
the AppScene/WebScene consolidation revisions are assembled.

The consolidation PR contains this integration guide and pins the reviewed
focused work. It stays draft and unmerged. It must be based on current `main` so
it never removes newer runtime, SDK, security, packaging, or test work.

## Open acceptance areas

The reference integration still tracks packaged evidence for complete input
observability, terminal resize and reopen, released-runtime storage, close veto
and allowed-close lifecycle, accessibility, IME, drag and drop, extension
webviews, and the remaining desktop capabilities. The current critical path is:

- #81/#288: bound the packaged extension-host iframe/Worker/transferred-port
  handshake and retain active MessagePorts across garbage collection;
- #265/#281: finish ServiceWorker Clients messaging/navigation on current main;
- #266-#268: streams, FetchEvent, CacheStorage, admitted resources, nested
  documents, interaction, and accessibility for unchanged webviews;
- #280/#252: requalify remote workspace resolve/Explorer timing after #81;
- #286: decode CSS string and `url()` escapes once before resource resolution.

WebScene #227 is the top-level native release epic. Its planned direct
workstreams are #7 (V8 Inspector), #81 (workers), #102 (Web Crypto), #235
(CSS and visual-tree compatibility), #247 (unchanged remote picker), and #248
(typed native panels and File System Access). #235 retains its existing CSS and
native child hierarchy. The generic rounded-background implementation is merged
through WebScene #251 and AppScene Stack #129, while #246 remains open for a
fresh unchanged-Code-OSS active-tab and Explorer-row comparison against
Chromium.

The tracking issues and current PR/stack status are maintained in vscode-demo
PR #1 and in the linked AppScene and WebScene issues.
