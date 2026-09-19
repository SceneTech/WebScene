# PRIMARY selection middle-paste source contract

WebScene issue #655 adds the provider half of the Linux PRIMARY middle-paste
stack. The exact baseline is WebScene
`5908986b08fb5001a26e3bba514f1efc27839d36`; AppScene #273 is the dependent
native consumer. The implementation remains product-neutral and has no X11,
browser, Electron, CEF or WebView dependency.

## Typed host boundary

`WEBSCENE_HOST_REQUEST_CLIPBOARD_PRIMARY_V1` is an additive v1 clipboard-read
flag. A zero flag continues to mean the normal clipboard for
`navigator.clipboard`, Ctrl/Command+V and the existing clipboard event path.
The PRIMARY flag is admitted only with a `text/plain` read, recent native user
activation and a connected, writable text-control target in the initiating
realm. Unknown flags, wildcard PRIMARY reads, readonly/disabled/inert targets
and malformed target wrappers reject before a request reaches the host.

The flagged request reuses the existing limits: 16 completion-bearing
clipboard operations, a 16 MiB completion payload, the aggregate 1,024-entry
host queue and the five-second activation window. It does not add a polling
loop or retained payload buffer. A host that does not implement a primary
selection may explicitly deny the request. AppScene #273 routes the flag to
its owner-thread X11 PRIMARY service and supplies the transfer deadline.

## Gesture and lifecycle

Button2 preserves the existing pointerdown/mousedown and pointerup/mouseup/
auxclick order. After mousedown defaults and handlers run, WebScene records at
most one focused writable text control by numeric DOM identity. A direct
middle press may focus a text control; unchanged Code's existing mousedown
handler may instead focus its hidden textarea and position the editor cursor.

Mouseup or auxclick `preventDefault()` vetoes the read. An admitted HTTP(S),
fragment or download anchor retains the #531 navigation/download default and
does not also paste. Otherwise one completed middle gesture queues at most one
flagged request. Successful UTF-8 text is delivered through the existing
clipboard `paste`/`clipboardData` path.

The pending Promise retains the request ID, DOM ID and V8 context rather than
a native node pointer. Completion resolves only while the same text control is
connected, writable and owned by that context. A second live-identity check
runs immediately before event dispatch. Detach clears the gesture ID;
navigation clears pointer state, cancels all native host Promises and discards
queued typed requests. Engine teardown uses the same cancellation path, and a
late or duplicate completion finds no live request binding.

## Authored contracts and remaining acceptance

The focused native contract `test_primary_selection_middle_paste` covers the
flagged request, single-request ordering, mouseup and auxclick cancellation,
unknown flags, wildcard rejection, detached-target completion, #531 anchor
precedence and saturation at 16 pending operations. Existing clipboard
contracts now assert zero flags for normal Clipboard API and shortcut reads.

Automatic PRIMARY publication remains outside #655. WebScene cannot infer an
unchanged Monaco model selection from pointer geometry, and the Code OSS web
entry point does not load the Electron-only Linux selection writer. A later
product-neutral selected-text provider contract is required before AppScene
can publish those selections without an application patch.

Only `git diff --check` is run for this fast implementation slice. The authored
native contract, compiler/build, browser oracle, repeated-gesture timing and
memory measurements, AppScene/X11 integration, packaged unchanged Code OSS,
VM and computer-use gates remain unexecuted.
