# Nested document unload ordering contract

## Scope and baseline

This #267 continuation started from exact WebScene `main`
`607ab1be63d4d3651d8a480a4239a62c7d2c68c8`, immediately after the merged
#577/#578 nested `beforeunload` gate, and was rebased before commit onto exact
current `main` `9eb82958ee6ab00030fedb891968868e286ffb3d`. That audit found one existing
`pagehide` path shared by allowed iframe navigation and iframe detach. It
intentionally did not add a second pagehide path. The remaining lifecycle gap
was the outgoing document's hidden transition and final `unload` before realm
work is retired.

The shared nested teardown now dispatches `pagehide` at the outgoing Window,
changes only that Document's visibility state to `hidden`, dispatches
`visibilitychange` at the Document, and dispatches `unload` at the Window. An
allowed navigation runs the sequence after its `beforeunload` decision and
before generation advance, pending preparation retirement, or replacement
resource admission. Iframe removal remains a non-cancelable discard: it skips
`beforeunload`, runs the same final sequence, then cancels context tasks and
releases its resource, storage, listener, worker-client, inspector and realm
state. Descendant iframe documents unload before their detached owner document.

## Identity, reentrancy and bounds

The outgoing Document, Window and stable owner-realm WindowProxy remain current
through all three final events. The hidden state is keyed by the outgoing native
document root, so a replacement Document starts visible without changing the
top-level host visibility state. Detached Documents retain `hidden` while their
wrappers remain observable and the root marker is reclaimed with the detached
subtree.

A per-frame in-progress guard spans pagehide, visibilitychange, unload and their
microtask checkpoints. Navigation or removal of the same frame from those
handlers cannot recursively dispatch lifecycle events, advance the generation,
admit a replacement resource, or retire the realm early. For an allowed
navigation, the initiating reflected source is restored after handlers return;
if a handler detached the owner, no replacement generation or resource is
created. Sibling frame work remains independent. One-shot state resets only
when the replacement realm becomes active and is cleared on detach, top-level
navigation and runtime shutdown.

## Authored contracts

- `tests/WebPlatformSubset/contracts/iframe-unload-ordering.html` derives the
  hidden transition from pinned WPT `page-visibility/iframe-unload.html`, and
  final event/task ordering from
  `html/browsers/browsing-the-web/unloading-documents/unload/004.html` plus
  `004-1.html`, all at `2c705104a295c48053eeddf7fe0170d790a4e853`.
- The `iframe-navigation-lifecycle` native source gate covers allowed navigation
  and non-cancelable detach, event targets and shapes, exact synchronous order,
  hidden state, stable identity, same-frame reentrancy, initiator source
  precedence and cancellation of a timeout queued by the outgoing realm.

`unload` deprecation policy, bfcache/persisted page transitions, top-level host
visibility, explicit reload/history traversal, cross-origin WindowProxy
capabilities, unload timing entries and broad product qualification remain
outside this slice.

## Validation status

Per the task constraint, no build, browser/WPT run, native test, benchmark,
computer-use flow, package gate or CI job was run. Only `git diff --check` is
used for this commit. The authored profile and native assertions remain
unexecuted and are deferred to the cumulative #264/#267 validation pass.
