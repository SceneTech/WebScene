# Owner-document URL reflection — WebScene #904

On the merged WebScene `5b4739c8` installed Code OSS build, the nested
Markdown prelude belongs to a virtual HTTPS document and authors
`./fake.html?id=...`. Reading its iframe `src` from the main realm returned a
loopback `/fake.html` URL, while `ownerDocument.baseURI` remained virtual
HTTPS. This is a URL-property reflection defect; actual request navigation
uses a separate owner-root base path and was not shown to be wrong.

`get_element_url` now resolves URL-valued properties through the element's
owning cascade/document root instead of the caller's current document. Its
existing fallback remains for unavailable bases. The change shares the same
owner-root/base cache as resource navigation. It leaves the authored content
attribute and cross-origin access policy unchanged.

## Executed focused gates

- Production-style V8 native engine and source-test targets built in Release
  with pinned V8 pointer compression and shared cage.
- `WEBSCENE_NATIVE_ENGINE_TEST_FILTER=iframe-navigation-lifecycle` passed.
  The added regression reads connected `a.href` and `input.src` from a
  same-origin parent realm before and after a nested `<base>` change and
  checks that `getAttribute` stays authored. The existing gate also passed
  100 iframe create/load/write/replace/remove cycles, 1 MiB update p95
  3.73 ms (250 ms limit), flat heap before/after at 1,849,528 bytes,
  cross-origin/opaque access denial, and history ordering.
- The checked-in `tests/WebPlatformSubset/chrome/owner-document-url-reflection-reference.html`
  returned `PASS` under installed Chrome 152 with `--headless=new
  --virtual-time-budget=3000 --dump-dom`. It checks the same nested `href`,
  `src`, base mutation, and authored-attribute expectations.
- `git diff --check` passed.

The existing standalone cross-document-history WPT profile cannot currently
serve its nested fixture through this native runner, and a new `srcdoc` or
initial `about:blank` contract timed out. Those attempts were removed from
the PR; no WPT pass is claimed. The direct native source gate and checked-in
Chrome reference carry this focused regression. The broader WebScene #267
WPT/browser matrix and unchanged Markdown rendering remain open. The Chrome
CDP runner also failed to expose a debuggable page target on this host, so
the standalone headless DOM check was used for the browser oracle.
