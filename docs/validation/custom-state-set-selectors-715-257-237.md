# Custom state set and selector source contract

Issue: WebScene #715

Parents: WebScene #257 and #237

Base: `a508b8bfa36c8c407e2e10f98813f23e52db4c15`

## Implemented boundary

- `ElementInternals.states` is a same-object, branded `CustomStateSet` for every
  successfully constructed custom element. Its mutable setlike surface provides
  `size`, `add`, `delete`, `clear`, `has`, `entries`, `keys`, `values`,
  `forEach`, and iteration in insertion order. Detached method calls fail the
  brand check.
- An element admits at most 64 state names. A decoded name is capped at 128
  WTF-8 bytes and total native names are capped at 8 KiB per element. Invalid
  or over-limit additions fail before changing either the JavaScript set or
  native selector state.
- The Servo-backed selector parser admits one CSS `<custom-ident>` in
  `:state()`, rejects extra tokens and reserved custom-identifier keywords, and
  preserves canonical identifier escaping, pseudo-class specificity, and
  CSSOM selector serialization.
- Matching consults state only on the owning element. State names do not create
  arbitrary pseudo-classes and are not reflected as content attributes.
- Each compiled `:state(name)` contributes one internal dependency key for
  `name`. Mutation looks up only that token's rule bucket, then uses the existing
  bounded subject/ancestor/sibling/`:has()` invalidation routes. It does not
  scan all stylesheet rules or the document.
- Duplicate additions and absent deletions are no-ops. `clear()` is bounded by
  the 64-state cap and coalesces resulting style publication through the
  existing recascade batch.
- A failed custom-element upgrade invalidates its leaked internals object and
  removes native state. Native state is reclaimed with an unreachable detached
  subtree, cleared at navigation before the replacement realm runs, and owned
  by the runtime through final teardown.

## Runtime and performance contract

State mutation runs synchronously on the runtime owner thread. Matching reads
retained decoded names; it performs no JavaScript call, CSS parse, selector
matching rebuild, polling, timer scheduling, or frame request. Static custom
state therefore has no sustained frame demand. CSS identifier validation occurs
only when a state is added or removed, never during matching or rendering.

## Deferred acceptance

Upstream WPT execution, package builds, performance counters, and physical
platform qualification remain deferred under the fast implementation policy.
This slice adds no host ABI, AppScene surface, Code OSS change, shadow-part
support, or arbitrary pseudo-class registration.
