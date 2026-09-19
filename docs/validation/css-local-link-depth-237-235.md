# Functional local-link path-depth source contract

Issue: WebScene #237

Parent: WebScene #235

Base: `bcb171a34f4ac41c6553a5e92feb37b850b99ad1`

## Scope and semantics

This slice implements the Selectors Level 5 functional
`:local-link(<integer>)` form. The sole argument is a non-negative integer no
larger than `2147483647`. CSS standard unsigned zero spellings `0`, `+0`, and
`-0` all select depth zero; genuinely negative, empty, fractional, multiple,
identifier, and overflowing arguments invalidate the selector. The existing
non-functional `:local-link` behavior is unchanged.

Functional matching resolves an eligible HTML/SVG anchor or HTML area `href`
against the owning browsing context's effective base. Depth zero compares the
URL origins. Greater depths additionally require the target and document to
have equal first *N* path segments. Empty segments, including the one produced
by a trailing slash, remain segments. A missing document or target segment
fails the match. Credentials, query, and fragment do not participate; scheme,
host, effective port, and selected path segments do. Non-hierarchical or
malformed URLs fail closed. Explicit ports are omitted only when equal to a
known nonzero default; an explicit port zero on another hierarchical scheme
therefore remains distinct from no port.

Every input and resolved URL retains the existing 8 KiB refusal limit. Matching
uses bounded views over the resolved URLs and performs no per-node retained
allocation.

## Invalidation and privacy

Functional and non-functional forms share the existing `href` and synthetic
`$local-link-document` compiled dependencies. Matching enrolls only evaluated
hyperlink node IDs by browsing-context root. `href`, effective-base, and
same-document URL changes replay prepared subject, functional, sibling, and
relational routes for that bounded set in one style batch. Navigation and
teardown retain the existing stale-ID cleanup. This adds no whole-document
scan, polling, frame demand, browsing-history store, or visited-state signal;
`:visited` remains privacy-closed.

Native contracts cover argument acceptance/refusal including signed zero,
origin normalization, credentials/query/fragment exclusion, missing segments,
and trailing empty segments. The browser candidate covers signed-zero and
depth 0/1/2 matching plus `href` and same-document URL invalidation.

## Browser support and remaining #237 boundary

`:local-link()` remains an experimental Selectors Level 5 feature without a
shipping mainstream-browser oracle or an upstream WPT suite suitable for
qualification. The checked-in HTML is candidate coverage for WebScene; physical
browser/package/performance evidence remains acceptance work.

Shadow-specific selectors, shadow-inclusive location semantics, complete
pseudo-element matching, and the remaining Selectors Level 4/5 state,
specificity, and syntax inventory remain open. This slice changes no Code OSS
source.
