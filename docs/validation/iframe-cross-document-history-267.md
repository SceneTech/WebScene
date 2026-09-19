# Nested cross-document history traversal contract

## Scope and baseline

This #267/#264 continuation started from exact WebScene `main`
`497b4dbbd6e5b576bbc07428e9b6e586a656f36e`, immediately after merged
#589/#590 restricted cross-origin WindowProxy behavior. Before the final local
commit it was rebased onto current `main`
`47dd9afc39748fa0adc804c7aaa567d40d71fc43` after #591/#592 merged.

The existing nested History model retained bounded same-document entries and
state across reload, but every ordinary child document navigation discarded
that list. Consequently `back()`, `forward()` and `go()` could only move among
hash/state entries belonging to the current Document. This slice extends the
same per-frame list across active-document replacement without introducing a
page cache.

## Bounded active-document traversal

Each retained entry now carries a logical document sequence. `pushState()` and
`replaceState()` retain the current sequence; an admitted child navigation
creates a new sequence and a null-state entry. Forward entries are discarded
before a new navigation. The existing limits remain authoritative: at most
1,024 entries, 8 KiB per URL, 1 MiB per cloned state and 16 MiB of cloned state
per child. When the entry cap is reached, the oldest entry and its state bytes
are retired.

Traversal within one sequence preserves the active realm and continues to
dispatch `popstate` and fragment-sensitive `hashchange`. Traversal to another
sequence runs the existing guarded `beforeunload`, `pagehide`, hidden
`visibilitychange` and `unload` path, cancels outgoing realm work, loads a new
Document, restores the selected entry/state into its History object, dispatches
`load`, then `popstate`. The owner WindowProxy stays stable, the iframe `src`
content attribute stays authored, and stale History objects cannot schedule a
second traversal. `Location.replace()` and `document.open()` replace the
current entry rather than appending one.

This is active-document reconstruction. No outgoing Document, JavaScript heap,
scroll position, form state or resource response is cached.

## Authored contracts

- `tests/WebPlatformSubset/contracts/iframe-cross-document-history-traversal.html`
  derives its queued traversal/state boundary from pinned WPT
  `html/browsers/history/the-history-interface/traverse_the_history_1.html`
  and the active-document cases under
  `html/browsers/browsing-the-web/history-traversal/`, at
  `2c705104a295c48053eeddf7fe0170d790a4e853`.
- The `iframe-navigation-lifecycle` native source gate covers A-to-B entry
  creation, back/forward resource admission, fresh Document identity, cloned
  state/null-state restoration, lifecycle order, post-load `popstate`, stable
  WindowProxy, reflected `src` preservation and exact request count.

Joint top-level/child session-history ordering, bfcache and `pageshow.persisted`,
POST resubmission, scroll/form persisted user state, failed traversal recovery,
redirect replacement and history traversal across opaque origins remain
deferred.

## Validation status

Per the task constraint, no build, browser/WPT run, native test, benchmark,
computer-use flow, package gate or CI job was run. Only `git diff --check` is
used for this commit. The authored profile and native assertions remain
unexecuted and are deferred to the cumulative #264/#267 validation pass.
