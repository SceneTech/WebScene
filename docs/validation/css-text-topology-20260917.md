# Text topology and CSS work — 2026-09-17

Base: `e1f634af17c496a9f28435fd14a11511303b0ffb` (draft #292).
Workstream: remaining structural DOM/CSS boundary from #235/#237.

## Scope and invariant

This stage implements `Text.splitText()` and `Node.normalize()` through the
generated Web IDL binding catalog and the legacy installation lane. Offsets use
UTF-16 code units and preserve lone surrogates through WTF-8 storage. Conversion
runs before reading the current data or parent, matching browser reentrancy.
Normalization recursively removes empty Text children and joins contiguous Text
siblings into the first node while preserving removed-node data and identity for
external references.

Both operations preserve the concatenated text of every parent and do not change
the element-child list. Therefore they cannot change selector subjects, including
`:empty`, sibling, positional or relational results. The implementation dirties
layout/paint but deliberately performs no selector invalidation or cascade. It
also preserves connected stylesheet text and textarea default/value semantics.

## Detached-wrapper complexity found during qualification

The first 12,800-pair split/normalize stress case performed zero CSS work but took
about **307–308 ms**. Each removed suffix was registered by comparing it with every
historical detached root, making repeated leaf detachments quadratic.

Detached roots now have an ID index and leaf roots take a constant-work registration
path. Non-leaf roots retain the existing containment reconciliation. A certification
counter records those containment comparisons. After the change, the same 12,800
pairs took **23.6 ms** with 32 unrelated nodes/rules and **28.7 ms** with 1,024;
the containment-comparison delta was zero. These local timings diagnose the fixed
algorithmic path; they are not a cross-machine release benchmark.

## Browser-referenced regression coverage

`css-text-split-normalize.html` covers:

- exposure, arity, receiver checks and Attr's inherited no-op;
- detached and connected split behavior, node order and first-node identity;
- UTF-16 boundaries, lone surrogates, offset conversion/reentrancy and errors;
- recursive normalization, empty nodes and comment-delimited runs;
- preserved CSS/layout, stylesheet text, textarea value/defaultValue and removed
  Text references.

The contract passed Chrome 153 **6/6**. Before implementation the same six tests
failed native **0/6**; final WebScene passes **6/6**. The adjacent CharacterData
contracts pass **70/70**.

The native scaling gate varies 8/128 affected parents independently of 32/1,024
unrelated nodes and rules, with 100 split/normalize cycles per parent. All nine
counter deltas are zero in all four cases: plan lookups, candidate/fallback visits,
compound/rule matches, cascade applications/candidates, positional sibling visits,
and detached-root containment comparisons.

Generated binding validation, the certification native test/shared-library build,
the detached-DOM GC group, and the production static SDK runtime build with
certification disabled and Inspector enabled all pass. The local certification test
binary contains the preserved uncommitted IndexedDB fixture edit; no focused test
above executes it, and it is excluded from this change. The production runtime does
not compile that test fixture. CI was not monitored.

See [machine-readable evidence](evidence/css-text-topology-20260917.json) for artifact
paths, hashes and result summaries.

## Remaining scope

This does not implement live Range boundary adjustment or MutationObserver records
for split/normalize, and does not claim complete DOM conformance. It does not replace
physical Spotify resize acceptance or the remaining structural CSS work in #235.
The non-leaf detached-root containment path remains intentionally unchanged; its
complexity should be addressed only with a separately qualified ownership design.

