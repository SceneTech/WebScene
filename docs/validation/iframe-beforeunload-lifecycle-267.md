# Nested beforeunload lifecycle contract

## Scope and baseline

The #253 and #265-#268 audit started from exact WebScene `main`
`2b64b08965aeba5ecbd33fcc0bd1beaaceaf6243`. Before commit, the slice was
rebased onto exact current `main`
`eaf54e5f51a8377c7b260c2001dfe67a59c28c24`. The audit found the sandbox token
surface, Service Worker control/resource planes,
same-origin WindowProxy and `document.open/write/close`, clipboard Permissions
Policy, parser script CSP, per-frame navigation generations, and one-shot
`pagehide` already merged. The first remaining #267 lifecycle gap was a
cancelable `beforeunload` decision before an authored nested navigation.

The implementation dispatches one synchronous, non-bubbling, cancelable
`beforeunload` in the outgoing frame realm for iframe `src` mutation and
`document.open()`. `preventDefault()` or `event.returnValue = ''` vetoes the
navigation before `pagehide`, generation advance, pending hydration retirement,
drag/download retirement, or replacement resource preparation. This is the
unchanged Code OSS `browser/pre/index.html` confirm-before-close shape.

Navigation of the same frame from its own `beforeunload` handler is rejected.
The initiating source wins over a reentrant source write; sibling frame work is
not globally locked. An allowed navigation retains the stable owner-realm
WindowProxy and proceeds through the existing generation and teardown path.
Iframe detach keeps its existing non-cancelable discard behavior.

## Bounds and authority

The reentrancy set contains at most one entry per synchronously unloading live
frame and is cleared after dispatch, frame detach, replacement activation,
top-level navigation, and runtime teardown. A veto creates no navigation queue
or future, resource request, host request, replacement realm, or new generation.
No
browser process, WebView, modal prompt, filesystem authority, network authority,
or AppScene ABI is added. Existing sandbox/origin access, CSP, Permissions
Policy, resource admission, download leases, and generation ownership are
unchanged.

## Authored contracts

- `tests/WebPlatformSubset/contracts/iframe-beforeunload-navigation.html`
  derives synchronous dispatch and event ordering from pinned WPT
  `beforeunload-synchronous.html`, `prompt/001.html`, and
  `beforeunload-canceling.html` at
  `2c705104a295c48053eeddf7fe0170d790a4e853`.
- The `iframe-navigation-lifecycle` native gate covers 50 canceled
  `document.open()` replacements, 50 canceled reflected `src` mutations,
  same-frame reentrancy, unchanged Document/WindowProxy identity, zero
  pre-decision `pagehide`, the following allowed navigation, and the existing
  heap, DOM-node, RSS, 100-cycle, one-MiB replacement, origin, and teardown
  bounds.

Manual beforeunload prompt and sticky-activation WPTs are excluded because this
native runtime has no browser prompt surface. `onbeforeunload` return-string
conversion, visibility transitions, `unload`, `location.reload()`, history
traversal, cross-origin WindowProxy capabilities, and broad event-order product
qualification remain outside this slice. The next #267 implementation gap is
the outgoing document visibility transition and `unload` ordering after an
allowed navigation or discard.

## Validation status

Per the task constraint, no build, browser/WPT run, native test, benchmark,
product check, package gate, or CI job was run. Only `git diff --check` is used
for this commit. The authored profile and native assertions remain unexecuted.
