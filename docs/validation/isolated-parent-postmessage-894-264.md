# Restricted cross-origin parent messaging — 21 September 2026

An unchanged Code OSS `645f29cc` Markdown Preview ran in a signed installed-CLI
Release from merged WebScene `06d6a4f5` and AppScene `1142abe`. The isolated
HTTPS prelude and its ServiceWorker script load, but the native preview remains
blank. Its first prelude exception is `Cannot read properties of null (reading
'postMessage')` at inline line 313, the unchanged source's
`window.parent.postMessage({target:ID,channel:'webview-ready'}, parentOrigin,
[this.channel.port2])`. See `vscode-demo/build/appscene-webview-398/merged-1142abe-06d6a4f5/native-preview.log`
and `preview-parent-window-null.png`. WebScene installed `parent=null` in a
cross-origin child because the parent cannot read the child's DOM. The child
still needs a restricted WindowProxy to send a target-origin message.

The focused fix creates a restricted child-realm parent proxy that only grants
safe WindowProxy properties and the existing bounded `postMessage`/structured-
clone route. The actual parent global, document, storage and arbitrary
properties are denied with `SecurityError`. The owner-realm iframe WindowProxy
remains stable so `MessageEvent.source` retains identity. A direct child
addresses its top parent; a nested cross-origin child addresses its immediate
parent and gets a separate restricted top reference. Its owning frame pointer
and the existing frame task queue refuse detached targets; no polling or frame
scheduling is introduced.

The focused native cross-origin HTTPS test verifies a parent DOM denial, parent
proxy read/write denial, exact child message origin/source and targetOrigin,
non-delivery to wrong origins, one transferred MessagePort and reply, nested
child-to-immediate-parent delivery and stable nested source. The full direct
and nested handshake took **76.2577 ms** against the 1,000 ms budget. Existing
isolated ServiceWorker registration and direct/inherited opaque sandbox test
passes in **219.654 ms**, the 100-cycle iframe sandbox gate passes, and the
100-cycle ServiceWorker lifecycle passes at **7.84092 ms p95** with flat
1,849,544-byte heap. This native build uses the existing local macOS V8
monolith with Inspector disabled; installed production SDK/CLI and CI follow.

`WEBSCENE_NATIVE_ENGINE_TEST_FILTER=iframe-navigation-lifecycle` fails on
`reentrantUnloadBlocked:false` both with this patch and after fully reverting
all its source/test changes and rebuilding the clean merged `06d6a4f5` in the
same configuration. This independent merged-main navigation gap is recorded
under WebScene #267 and must not be attributed to the restricted parent proxy.

The focused branch must pass relevant runners before merge. An exact merged
installed SDK/CLI Release and native Markdown preview must then prove the
next webview stage, including inner frame, visual, lifecycle and performance
acceptance under WebScene #264 and AppScene #398. No Code OSS source was
modified; no Electron, Chromium, CEF or WebView is introduced.
