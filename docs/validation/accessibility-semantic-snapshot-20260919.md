# Accessibility semantic snapshot provider boundary

WebScene issue #262 and AppScene issue #30 require a native semantic source
that does not infer accessibility from paint commands. The v1 provider walks
the live composed DOM on the engine worker after flushing layout and publishes
an immutable lease through `webscene_engine_acquire_semantic_snapshot_v1`.
AppScene can consume this source in a later change without reading V8 or
retained-scene internals.

## Contract

Each lease contains deterministic preorder arrays for documents, nodes and
resolved ARIA IDREF relationships plus one UTF-8 arena. Documents carry the
top-level document generation, nested frame owner, frame navigation generation,
origin and root index. The snapshot also carries the layout generation used for
its bounds. Nodes carry a generation-derived semantic identity,
native DOM identity, document and tree indices, role, computed accessible name,
live value, description, state bits and transformed layout bounds. The snapshot
also identifies the focused node across top-level and nested browsing contexts.

The first acquire may return null. It requests construction on the V8 owner
thread; completion invokes the existing work-available signal. Later acquires
return the latest completed lease and request a refresh. Navigation retires the
engine's previous publication before admitting replacement work. Low-memory and
engine teardown also drop the engine-owned lease. A lease already held by a host
remains immutable and releasable after engine teardown.

The provider is read-only. Native accessibility peers, announcements, hit-test
actions and editing actions remain owned by AppScene #30.

## Bounds

- 16,384 nodes;
- 256 documents;
- 65,536 relationships;
- 4 MiB total UTF-8 and 64 KiB for any computed text alternative.

Every exhausted budget sets a dedicated truncation flag. Hidden, inert,
`display:none`, `visibility:hidden`, `content-visibility:hidden` and detached
subtrees are omitted. Relationship resolution remains within the source
document, so an authored ID cannot cross an origin or browsing-context boundary.

## Authored qualification sources

- `semantic_snapshot_header_tests.c` pins the C11 ABI surface.
- `native_v8_runtime_semantic_snapshot_tests.inc` covers top-level and nested
  documents, roles, label and description inputs, live values, states,
  relationships, focus, bounds, immutable replacement, explicit node capping,
  construction time and lease survival after engine retirement.
- `accessibility-semantic-snapshot-source.html` and its dedicated profile retain
  a browser-shaped DOM source oracle for the same dynamic and nested inputs.

Per the implementation-only instruction, these sources were authored but not
executed. Only `git diff --check` is used for this slice.
