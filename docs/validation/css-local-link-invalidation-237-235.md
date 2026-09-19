# Local-link matching and invalidation source contract

Issue: WebScene #237

Parent: WebScene #235

Base: `64e7eef6178204281f9a5c95b4679fc274b1b068`

## Gap and scope

The standards parser admitted the non-functional `:local-link` pseudo class,
but native matching always failed it. This slice uses the hyperlink eligibility
shared with `:link` and `:any-link`, resolves the authored `href` against the
owning browsing context's effective document base, strips fragment identifiers
from both absolute URLs, and compares every remaining URL component. Query and
path differences therefore remain non-local. URL inputs are refused above the
existing 8 KiB navigation bound.

No browsing-history state is read or retained. `:visited` remains
privacy-closed. Functional path-depth behavior is implemented by the dependent
`css-local-link-depth-237-235.md` slice.

## Bounded invalidation

`href` mutations reuse the compiled subject, functional, sibling, descendant,
and reverse relational routes delivered by #148, #245, and #730. The compiler
also records a `$local-link-document` dependency at the same route.

Only hyperlink nodes encountered by a compiled `:local-link` matcher are
enrolled, keyed by browsing-context root and native node ID. An effective
`<base href>` or same-document History URL change replays the prepared routes
for those enrolled nodes inside one coalesced style batch. Stale, detached, and
reparented IDs are removed during replay, while navigation and teardown clear
the index. This performs no whole-document selector scan, selector reparsing,
polling, or sustained frame work.

The browser contract covers fragment-insensitive and query-sensitive matching,
subject, nested functional, adjacent sibling, and relational invalidation,
same-document URL changes, effective-base insertion/removal, `href` removal,
and metadata `link` exclusion. The native source contract additionally pins URL
resolution and the compiled dependency routes. The HTML fixture is WPT-aligned
candidate coverage; current mainstream browsers do not yet provide a shipping
`:local-link` oracle, so physical browser qualification remains acceptance work.

## Remaining #237 boundary

Shadow-specific selectors, complete pseudo-element matching, the remaining
Selectors Level 4/5 state and specificity inventory, and cumulative browser/
package/performance qualification remain open. This slice changes no Code OSS
source.
