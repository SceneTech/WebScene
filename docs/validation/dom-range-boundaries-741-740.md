# Live Range boundaries for Code OSS Custom Highlights

Issues: WebScene #741, parent #740, CSS epic #235

Base: `6ec61c77d08c11d99229877e840ed3b7fd144a41`

Unchanged Code OSS: `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`

## Product dependency

Code OSS chat find builds live text ranges with `document.createRange()`,
`setStart()` and `setEnd()` before inserting them into `Highlight`. The bundled
Markdown preview uses `new Range()`, `setStartAfter()` and `setEndBefore()`
around diff marker elements. WebScene previously returned a plain object whose
only functional method was `createContextualFragment()`; `selectNodeContents()`
was a no-op and no constructible `Range` global existed.

## Bounded implementation

Each live Range owns two retained boundary points expressed as native node IDs
and UTF-16 or child-index offsets. `Range` and `document.createRange()` share
one prototype with boundary getters, common-ancestor lookup, endpoint setters,
marker-sibling helpers, `selectNodeContents()`, and the existing contextual
fragment method. Endpoint crossing collapses to the newly assigned boundary.
Wrong-document, parentless and out-of-bounds inputs fail before state changes.

Bindings are capped at 4,096 per runtime, weakly follow their JavaScript
wrappers, and are retired before navigation destroys node wrappers. Every
binding carries the document generation and root ID, preventing a stale object
from resolving a reused node ID. Boundary reads and writes create no DOM or
visual-tree nodes, schedule no frame, and perform no style or selector work.
Ordering work is limited to the two ancestor paths and the shared sibling
vector when endpoints are in different containers.

The browser candidate and native contract cover same-node and cross-text-node
UTF-16 offsets, marker siblings, common ancestors, selected contents, endpoint
crossing, constructibility, and atomic invalid-offset rejection.

## Explicit remainder

The stacked #740 work still needs `Highlight`, `CSS.highlights`, functional
`::highlight()` cascade and retained range paint. Full DOM Range editing,
cloning/extraction, mutation boundary adjustment, stringification, and
arbitrary disconnected-range behavior remain outside this Code OSS slice.
