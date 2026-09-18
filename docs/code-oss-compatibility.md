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

- native Blob/FileReader, retained Performance Timeline, live owner-backed and
  constructed/adopted stylesheet CSSOM, DOM traversal and collection behavior;
- browser-shaped `HTMLAnchorElement` constructor/prototype identity in top-level
  and iframe realms, including stable `<a>` wrappers and namespace rejection;
- exact rounded-surface scene commands for structured SVG CSS backgrounds,
  including authored size, position, repeat, radii, and selection mutation;
- dedicated workers, MessageChannel/MessagePort, structured clone, transferable
  ownership, iframe realms, ordered errors, termination, navigation cleanup, and
  origin-scoped Blob object URLs shared across nested worker runtimes. MessagePort
  event-handler accessors retain per-instance identity across realms, and active
  dedicated-Worker ports stay reachable across forced garbage collection until
  listener removal, transfer, close, termination, navigation, or teardown;
- stable same-origin iframe WindowProxy lifecycle and document replacement, live
  Selection and Window.find state, and nested browsing-context focus ownership;
- bounded native clipboard and window requests, focus/fullscreen publication,
  browser-compatible keyboard events, user activation, and clipboard shortcuts;
- opaque File System Access handles, including granted reads, file and directory
  child creation, recursive and non-recursive entry removal, and atomic
  writable-file streams;
- compiled CSS caches, bounded invalidation dependencies, animation and rendering
  opportunity fixes, selector-cache lifetime protection, containment/container
  queries, content visibility, host accessibility preferences, policy-backed
  imported stylesheets with live CSSOM reflection, parsed mask/clip/effect
  values, and retained authored-order brightness, grayscale, contrast,
  saturation, and foreground blur lists with computed functional lengths and
  blur-aware localized damage, plus retained inset/circle/polygon/explicit-
  ellipse clips, single-layer linear-gradient alpha masks, standard/WebKit mask
  shorthand expansion, and policy-loaded URL-backed SVG alpha masks with
  destination-in Skia/Flutter projection and explicit fail-closed resources;
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
  Client discovery, messaging, and navigation lifecycle are also on `main`;
  representative webview resource consumers now have bounded packaged-worker,
  Chrome, and portable-V8 coverage under the completed #266 lane.

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

- #288: extend the merged dedicated-Worker active-port lifetime and release
  matrix to ServiceWorker and iframe-specific cross-realm acceptance. The
  packaged extension-host handshake, MessagePort event-handler accessors, and
  forced-GC Worker prerequisite are merged and passing;
- #267-#268: finish secure nested-document lifecycle and remaining
  interaction/accessibility after the merged resource-consumer, WindowProxy,
  document-replacement, find/selection, and focus slices;
- #289/#280/#252: requalify remote workspace resolve, Explorer timing, and watcher
  refresh in the current package after the merged scheduler correction;
- #248/#269: finish remaining File System Access mutations and qualify edit, save,
  revert, and recovery after merged opaque file/directory child creation and
  directory entry removal.

WebScene #227 is the top-level native release epic. Its planned direct
workstreams coordinate Inspector, workers, Web Crypto, CSS and visual-tree
compatibility, remote workspaces, File System Access, nested webviews, product
visuals, workbench behavior, accessibility, and Web API coverage. #235 retains
its existing CSS and native child hierarchy; #239 now includes constructed,
adopted, nested, replacement, grouping, imported stylesheet, and top-level
namespace-rule CSSOM reflection. Namespace-aware selector matching is not
claimed. #256 now includes merged single-layer linear-gradient and URL-backed
SVG alpha masks; it tracks backdrop sampling, remaining path/URL clips, raster
masks, multiple/radial layers, luminance mode, non-add composites, and richer
Flutter SVG geometry. The
generic rounded-background implementation is merged through WebScene #251 and
AppScene Stack #129, while #246 remains open for a fresh unchanged-Code-OSS
active-tab and Explorer-row comparison against Chromium.

The tracking issues and current PR/stack status are maintained in vscode-demo
PR #1 and in the linked AppScene and WebScene issues.
