# Link-state matching and invalidation source contract

Issue: WebScene #237

Parent: WebScene #235

Base: `40a311c3dbfb0f5f86c94824cd609c232f0f555b`

## Gap and scope

The Servo-backed parser and DOM selector validation already admitted `:link`
and `:any-link`, but native compound matching treated both as inactive. The
compiled invalidation plan also did not associate either pseudo class with the
`href` attribute. An anchor could therefore parse successfully without ever
matching or responding to a later `href` addition or removal.

This slice makes HTML/SVG `a[href]` and HTML `area[href]` match both unvisited
pseudo classes.
Attribute presence is sufficient, including an empty `href`. Other elements,
including stylesheet `link` metadata, remain ineligible. WebScene retains no
browsing-history database, so `:visited` remains privacy-closed and never
matches.

## Routed mutation boundary

The compiled dependency collector maps `:link` and `:any-link` to `href` at the
existing selector route. A subject selector recascades its subject; combinator
and nested functional selectors retain their child, sibling, descendant, or
reverse `:has()` route. Attribute mutation continues through the merged #148
and #245 indexes. This adds no whole-document scan, polling, frame request, or
new retained DOM state.

`css-link-state-invalidation.html` covers initial non-link state, empty and
nonempty `href` additions, removal, DOM matching/query APIs, `:is()`, `:not()`,
adjacent sibling and relational descendant style changes, HTML `area`, metadata
`link` exclusion, and the privacy-closed `:visited` result. The selector parser
contract separately checks subject, child, and reverse relational dependency
routes.

## Remaining #237 boundary

`:local-link`, `:target-within`, shadow-specific selectors, complete pseudo
element matching, the remaining Selectors Level 4/state/specificity inventory,
and cumulative browser/package/performance qualification remain open. This
slice changes no Code OSS source and does not duplicate merged structural or
functional-selector work.
