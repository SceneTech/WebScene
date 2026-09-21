# Isolated iframe ServiceWorker — WebScene #892 / #264

## Exact installed-product trigger

Unchanged Code OSS `645f29cc` on WebScene `d3c030fa` and AppScene `34ba9e5` runs from a signed package built through the verified installed SDK/CLI. Native keyboard **Markdown: Open Preview to the Side** loads its generated isolated HTTPS prelude and then throws at inline line 237: `Service Workers are not enabled. Webviews will not work.` The prelude checks `!!navigator.serviceWorker`. The screenshot and resource/exception trace are in `vscode-demo/build/appscene-webview-398/` and the reduced ownership record is WebScene #892. The preview is still blank.

Nested context construction installed ServiceWorker only if the **parent** could access the child's DOM. A different-origin HTTPS child must have its own secure worker capability while remaining inaccessible to the parent. A second, initially masked security defect was exposed by the focused native test: the old sandbox check walked the child's body ancestors before its body had been attached to the owning iframe, so `sandbox="allow-scripts"` incorrectly exposed ServiceWorker to an opaque child. Pass the owning iframe to the installer, inspect it and every ancestor before child scripts run, and keep the parent DOM-access check only for parent DOM operations.

## Focused native gate

Run `WEBSCENE_NATIVE_ENGINE_TEST_FILTER=service-worker-cross-origin webscene_native_engine_tests` against the V8/graphics configuration of the installed macOS SDK. The product-neutral fixture uses a distinct HTTPS parent and child, with explicitly admitted installed document/worker bytes. The parent cannot access either child's `contentDocument`; the allowed child has `sandbox="allow-same-origin allow-scripts"` and registers its own same-origin module ServiceWorker. An opaque `allow-scripts` child and a nested `allow-same-origin allow-scripts` child under that opaque ancestor both lack `navigator.serviceWorker`.

The first runtime-only change made the allowed child register but incorrectly reported `opaque-sw-visible:true`; the final owner/ancestor check makes all three positive and negative assertions pass. Native registration plus both sandbox checks took **230.711 ms** against the focused 1 s budget. Related existing native gates pass: 100 ServiceWorker register/update/unregister cycles p95 **6.60546 ms**, 100 MessagePort/Clients cycles p95 **0.053208 ms**, and the 100-cycle iframe sandbox gate. The ServiceWorker lifecycle fixture reports identical 1,849,600-byte V8 heap before and after forced collection.

This is a macOS native source qualification with Inspector disabled in the local test configuration because the reused local V8 archive does not carry the separate Inspector console bridge ABI. The signed installed SDK/CLI package and full Code OSS Markdown/visual/lifecycle gate must be repeated after the focused PR merges. No installed Markdown success or Windows/Linux run is claimed here.
