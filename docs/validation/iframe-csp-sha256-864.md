# Inline iframe CSP SHA-256 admission (#864, #81, #267)

## Exact VS Code failure

The Code OSS browser extension host loads
`webWorkerExtensionHostIframe.html` successfully, but its inline bootstrap
script never runs. The page authorizes that script with a `script-src`
SHA-256 source expression. WebScene previously noticed that a hash source was
present, disabled the `unsafe-inline` fallback, and then rejected every inline
script because it did not calculate or compare the hash.

The exact 5,588-byte Code OSS bootstrap script hashes to
`sha256-daEgfo2VIXpx2Np71KqCCbkeQwv+68vPrx54XRcbdcs=`. This explains why the
iframe was fetched while no bootstrap message or transferred `MessagePort`
reached the workbench.

## Implemented boundary

For an inline script and a `sha256-...` source expression, WebScene now hashes
the parser's exact script bytes, including leading and trailing whitespace,
and compares the Base64 digest case-sensitively. The algorithm token remains
ASCII case-insensitive. The digest is calculated lazily and at most once per
script, even when a policy contains multiple sources.

Malformed and nonmatching expressions fail closed. The existing CSP rule that
a nonce or hash suppresses `unsafe-inline` remains intact. SHA-384 and SHA-512
source expressions remain unsupported and fail closed; they require their own
implementation and conformance coverage.

## Regression and performance gate

The native query-bearing iframe/Worker/MessagePort lifecycle test now uses a
matching SHA-256 CSP source expression around the extension-host-shaped
bootstrap. A second iframe combines `unsafe-inline` with a deliberately
nonmatching hash and proves that its script cannot post a message.

Focused release-mode result on macOS 26.6, Apple Silicon:

```text
Query iframe extension-host lifecycle gate: cycles=20
p95=162.900915980339ms max=163.047624945641ms
heapBefore=1689768 heapAfter=1689768
rssBefore=35930112 rssAfter=83984384
```

The test passed. Heap use returned to its baseline after 20 lifecycle cycles.
The one-time digest calculation is limited to inline scripts governed by a
SHA-256 source expression and adds no frame, timer, style, layout, or visual
tree work.

The local test build required `CMAKE_OSX_DEPLOYMENT_TARGET=26.0` because the
installed Xcode 26.4 SDK marks the repository's existing floating-point
`std::from_chars`/`std::to_chars` uses unavailable for older deployment
targets. That toolchain constraint is independent of this change.
