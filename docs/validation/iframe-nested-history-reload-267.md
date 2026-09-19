# Nested history traversal and reload contract

## Scope and baseline

This #267 continuation started from exact WebScene `main`
`60449c94680006a08ab4997fe1aa47f068030318`, immediately after merged
#581/#582 nested unload ordering. Before commit it was rebased onto current
`main` `a8a537f8649d487ae2efda9481c310deddc89978` after #583/#584 merged.

The existing child `History` surface could mutate the active URL with
`pushState` and `replaceState`, but did not retain entries and exposed no-op
`back`, `forward` and `go`. Child `location.reload()` also returned without
navigating. This slice gives each connected same-origin iframe browsing context
a bounded same-document entry list and routes explicit reload through the
existing guarded nested document replacement path.

## Lifecycle and bounds

Each entry retains an 8 KiB-bounded same-origin URL and at most 1 MiB of
structured-cloned state. A child list is capped at 1,024 entries and 16 MiB of
retained state; pushing after traversal discards forward entries before
applying those caps. Traversal is a queued realm task, ignores out-of-range
indices, dispatches `popstate` before `hashchange`, and blocks recursive
traversal while those callbacks run.

Same-document traversal updates the live Location and Document URL without
running `beforeunload`, `pagehide`, `visibilitychange` or `unload`. It preserves
the child Document, global, generation, timers and resource work. Explicit
reload runs the existing ordered `beforeunload`, `pagehide`, hidden
`visibilitychange`, and `unload` path. Only after that sequence does it advance
the frame generation, cancel outgoing realm tasks and connected resources, and
admit the replacement resource. The owner-facing WindowProxy and current
history entry/state survive the replacement. Reload and traversal calls made
from the active transition are ignored, and stale realm History objects reject
mutation.

## Authored contracts

- `tests/WebPlatformSubset/contracts/iframe-nested-history-reload.html` derives
  its queued traversal boundary from pinned WPT
  `html/browsers/history/the-history-interface/traverse_the_history_1.html`,
  its reload boundary from
  `html/browsers/history/the-location-interface/reload_post_1.html`, and its
  final event ordering from
  `html/browsers/browsing-the-web/unloading-documents/unload/004.html`, all at
  `2c705104a295c48053eeddf7fe0170d790a4e853`.
- The `iframe-navigation-lifecycle` native source gate covers cloned state,
  forward-entry truncation, event order, same-document identity/work
  preservation, reload lifecycle order, stable WindowProxy, session history
  retention, stale-task cancellation and transition reentrancy.

Cross-document history traversal, joint top-level/child session-history order,
POST resubmission, bfcache/persisted transitions and restricted cross-origin
WindowProxy capabilities remain outside this bounded same-origin slice.

## Validation status

Per the task constraint, no build, browser/WPT run, native test, benchmark,
computer-use flow, package gate or CI job was run. Only `git diff --check` is
used for this commit. The authored profile and native assertions remain
unexecuted and are deferred to the cumulative #264/#267 validation pass.
