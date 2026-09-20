# Range document ownership for Monaco measurement (#863)

## Root cause

`Document.createRange()` constructed a Range whose root came from V8's current
context. That is the caller's realm when a same-origin parent invokes
`child.contentDocument.createRange()`, and it is also the active realm when a
detached document method is called. The new Range therefore belonged to the
caller document even though the Document receiver belonged to another native
root. Selecting a valid node from the receiver then threw
`WrongDocumentError`.

This matches the packaged Code OSS failure in Monaco's reusable Range path:
the earlier boundary calls were caught, while `_detachRange()` called
`selectNodeContents()` in `finally` and exposed the wrong owner.

## Implemented contract

`Document.createRange()` now binds the new Range to the native root stored on
the Document receiver. `new Range()` continues to use the current realm's
active document. Range construction initializes both endpoints atomically to
the selected document root.

The regression uses a detached parsed document from the top-level realm. It
proves that its Range accepts nodes from that document, rejects a top-level
node with `WrongDocumentError`, preserves both prior endpoints after rejection,
and remains reusable afterward. This directly exercises the receiver/caller
realm split without network or iframe timing.

## Focused gate

The release-mode native `range-document-owner` test passed on macOS 26.6,
Apple Silicon. It also reused the same cross-realm Range for 10,000
`setStart`, `setEnd`, and `selectNodeContents` cycles:

```text
Range document-owner reuse gate: cycles=10000 durationMs=2.28229200839996
```

The path adds no style recalculation, layout, scene publication, timer, or
allocation per reuse. A Range registered with a custom highlight retains the
existing targeted highlight repaint behavior.

Exact packaged Code OSS verification remains required after this focused PR is
merged and the WebScene pin is advanced. That run owns the final zero-error
Monaco trace and editor geometry comparison.

## Exact-package follow-up

The first exact package after #866 reached the extension-host Ready and
Initialized states, but Monaco still raised `WrongDocumentError` when
`_detachRange()` selected its `_textRangeRestingSpot`. That node is created by
`document.createElement('div')` and deliberately never connected. WebScene had
been treating connectivity to the document root as document ownership.

Document factory calls now record the receiver document independently of the
parent tree. Range validation follows that identity, so a never-connected node
and its descendants remain valid endpoints while a node created by another
Document still fails atomically. Detached-subtree collection and navigation
clear the ownership records with the corresponding native nodes.
