# Accessibility semantic delta publication boundary

WebScene issue #547 adds a measured, bounded incremental companion to the full
semantic snapshot provider. The delta route is platform neutral: it contains
numeric identities, generations, geometry and copied UTF-8 only. AppKit, UI
Automation, AT-SPI and native accessibility peer objects remain in AppScene.

## Request and lease contract

`webscene_engine_request_semantic_delta_v1` accepts an exact retained full
snapshot generation. The engine retains the latest snapshot and at most its
immediate predecessor so the refresh requested by snapshot acquisition cannot
race the following delta request. Admission pins that immutable base, consumes
the predecessor slot and schedules one worker comparison. One request may be
pending or completed at a time. A second request is rejected as queue-full; a
generation outside the two-snapshot window is rejected as stale. The worker
performs no delta DOM or snapshot scan until a request is admitted.

Completion publishes the newly built full snapshot and one immutable delta.
`webscene_engine_take_semantic_delta_v1` removes that completion. Its lease may
outlive the engine and carries base/new snapshot, top-document and layout
generations. A consumer applies it only when its peer generation equals the
base and advances the peer generation exactly to the new value.

Any nonzero delta flag requires the consumer to discard every operation and
acquire the full new snapshot. Top-document replacement, a truncated base or
new snapshot, operation exhaustion, or UTF-8 exhaustion never produces a
partial patch. Navigation and low-memory requests synchronously retire pinned
bases and completed deltas. The iteration document epoch prevents a worker
completion racing retirement from being republished. Teardown releases all
engine-owned delta state while preserving already taken leases.

## Deterministic operations

Operations use semantic identities rather than snapshot indices. INSERT and
UPDATE carry a complete node projection and copied strings. REPARENT carries
the new parent and next-sibling identities. REMOVE carries the retired identity.
Relationship add/remove operations carry source, target and relationship kind.
FOCUS carries the new focused identity, or zero when focus clears.

The deterministic order is:

1. relationship removals in base relationship order;
2. node removals in reverse base preorder, children before parents;
3. insertions in new preorder, parents before children;
4. for each retained node in new preorder, reparent then value update;
5. relationship additions in new relationship order;
6. one focus operation last when focus changed.

Each node-bearing operation carries top-document generation, nested frame owner
and frame navigation generation. Replacing a nested document therefore removes
old-generation identities and inserts new-generation identities without
crossing a browsing-context boundary.

## Bounds

- one pinned request;
- one completed untaken lease;
- two retained base snapshots (latest plus its immediate predecessor);
- 8,192 operations;
- 2 MiB of copied UTF-8;
- underlying full-snapshot bounds of 16,384 nodes, 256 documents, 65,536
  relationships, 4 MiB UTF-8 and 64 KiB per computed text alternative.

Overflow returns `FULL_SNAPSHOT_REQUIRED | OVERFLOW`, zero operations and zero
string bytes. A truncated input returns `FULL_SNAPSHOT_REQUIRED |
TRUNCATED_SNAPSHOT`. Recovery uses the new full snapshot already published by
the same worker computation.

## Authored qualification sources

- `semantic_delta_header_tests.c` pins the C11 request, operation, lease and cap
  ABI.
- `native_v8_runtime_semantic_delta_tests.inc` covers all operation kinds,
  stale and full admission, terminal/editor/panel churn, nested replacement,
  overflow recovery, lease survival, navigation/low-memory retirement, a
  2,048-node virtualized tree, 100 update cycles, retained-memory bounds and a
  50 ms request-to-publication p95 gate.
- `accessibility-semantic-delta-source.html` and its profile retain the browser
  DOM oracle for large-tree churn and nested document replacement.
- `native-binary-interop.test.mjs` pins request/take/release exports.

Per the implementation-only instruction, these sources were authored but not
executed. Only `git diff --check` is used for this slice.

AppScene #30 still owns per-platform peer mutation, strict base/new generation
validation, full-snapshot recovery, main-thread accessibility rules, and
packaged VoiceOver, Narrator and Orca measurement and acceptance.
