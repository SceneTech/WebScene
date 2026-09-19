# Restricted cross-origin nested WindowProxy contract

## Scope and baseline

This #267 continuation started from exact WebScene `main`
`dc17f4684ee195dab07b1ed74e2801e186a25112`, immediately after merged
#585/#586 nested reload and history traversal. Before the final local commit it
was rebased onto current `main`
`383fe6578c501396f3f2f2ade0fea6b0166479fc` after #587/#588 merged.

The native iframe path already separated resource admission, sandbox origin,
Content Security Policy script admission and Permissions Policy capability
checks. It also retained one owner-side object across same-origin navigation,
but returned `null` for `contentWindow` as soon as reflected `src` appeared
cross-origin. This slice keeps those admission layers unchanged and makes the
owner-side object a restricted WindowProxy when the committed document is not
same-origin.

## Origin and proxy boundary

Each connected iframe now retains its committed active document origin. The
initial `about:blank` document inherits the owner origin unless sandbox makes
it opaque. Updating `src` does not change access while the outgoing document
runs `beforeunload`, `pagehide`, `visibilitychange` and `unload`; the
replacement origin becomes active only as its realm commits.

`contentWindow` retains one identity across same-origin, cross-origin and
opaque commits. A restricted proxy exposes the browser cross-origin identity
and hierarchy properties, `postMessage`, `focus`, `blur`, `close`, and the
write-only navigation part of `location`. Privileged reads, mutation,
deletion, definition and descriptor inspection throw `SecurityError`.
`contentDocument` remains `null`. Cross-origin `location` assignment and
`replace()` use the existing guarded frame navigation path and do not change
resource, CSP, sandbox or Permissions Policy admission.

Successful replacement resets the persistent handles for the outgoing frame
global, context and Document while the stable proxy retargets the committed
realm. A later cross-origin commit hides properties from a formerly
same-origin realm. Detach clears origin/location bookkeeping; a retained proxy
reports closed, cannot route messages to the top-level fallback, and continues
to deny privileged access.

## Authored contracts

- `tests/WebPlatformSubset/contracts/iframe-cross-origin-window-proxy.html`
  derives its allowed cross-origin keys and `SecurityError` boundary from
  pinned WPT
  `html/browsers/origin/cross-origin-objects/cross-origin-objects.html`, and
  its identity transition from
  `html/browsers/the-window-object/windowproxy.html`, both at
  `2c705104a295c48053eeddf7fe0170d790a4e853`.
- The `iframe-navigation-lifecycle` native source gate covers the inherited
  initial document, committed cross-origin denial, allowed operations,
  write-only Location navigation, same/cross-origin retargeting, descriptor and
  expando denial, stale realm hiding and detached proxy retirement.

Auxiliary windows, named child lookup, opener relationships, broad Location
reflection, document.domain, COOP/COEP agent clusters, and full cross-origin
property descriptor/enumeration parity remain deferred.

## Validation status

Per the task constraint, no build, browser/WPT run, native test, benchmark,
computer-use flow, package gate or CI job was run. Only `git diff --check` is
used for this commit. The authored profile and native assertions remain
unexecuted and are deferred to the cumulative #264/#267 validation pass.
