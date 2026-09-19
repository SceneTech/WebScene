# Target-within matching and invalidation source contract

Issue: WebScene #237

Parent: WebScene #235

Base: `573aa124ac3500e4af6cd19b664e595de5ea1faa`

## Gap and scope

The parser admitted non-functional `:target-within`, but native compound
matching never activated it. Existing `:target` matching also read the caller's
V8 `location` object, which could select the wrong browsing context during a
same-origin cross-frame cascade and did not decode percent-encoded fragments.

This slice resolves the URL target from the styled node's own cascade root and
a mutation-invalidated, one-target-per-root cache. `:target` matches that target;
`:target-within` matches the target and its DOM ancestor chain up to the owning
browsing-context root. Fragment identifiers accept their authored and decoded
forms. The cache retains the inclusive chain alongside the first tree-order
target so a duplicate-ID winner change can replay the branch that lost target
state after a detach or reparent. Shadow-inclusive ancestry remains outside
this slice.

## Bounded invalidation

The compiled dependency plan maps target state to `id` and a synthetic
`$target-document` input. `:target-within` extends each dependency through the
inclusive ancestor chain and marks child-list changes for the changed parent
and its ancestors. Existing prepared routes continue through nested functional,
sibling, descendant, and relational selectors.

History URL, `location.hash`, and same-document anchor transitions resolve only
the old and new target nodes, then replay their compiled target routes in one
coalesced style batch. ID mutations temporarily refresh the target cache for
previous/current matching. Insertions, removals, and reparenting reuse the
child-list transition path and refresh that cache once for the mutation. When
either mutation changes the first matching duplicate ID, the cached old chain
and freshly resolved new chain replay through the prepared routes in one style
batch. Work stays proportional to those two chains plus the single target
lookup for each evaluated mutation state; no whole-tree recascade is
introduced. There is no polling, retained
frame demand, selector reparsing, or per-frame document work.

The WPT-aligned browser contract covers direct `:target`, inclusive
`:target-within`, nested functional matching, adjacent-sibling effects,
fragment/history changes, ID changes, and target reparenting. Native source
contracts pin percent decoding and subject/ancestor/child-list compiled routes;
the browser contract also covers duplicate-ID winner transfer by ID and tree
mutation.

## Remaining #237 boundary

Shadow-specific selectors and shadow-inclusive target ancestry, complete
pseudo-element matching, the remaining Selectors Level 4/5 state and
specificity inventory, and cumulative browser/package/performance
qualification remain open. This slice changes no Code OSS source.
