# Accessibility semantic live-region publication boundary

WebScene issue #546 adds a bounded, one-way semantic live-region provider for
the native peers tracked by AppScene issue #185. WebScene publishes immutable
UTF-8 event leases. It does not create platform accessibility objects or call
AppKit, UI Automation, AT-SPI, screen-reader or announcement APIs.

## Contract

`webscene_engine_take_semantic_live_events_v1` removes the next completed
worker publication and returns an immutable lease which may outlive its engine.
Each event contains a monotonic sequence, top-document and nested-frame
generations, semantic identity, live-region DOM identity, role, politeness,
coalescing flags and one UTF-8 text slice. AppScene can compare those numeric
generations with its current semantic peer before delivering an announcement.

The supported DOM subset recognizes explicit `aria-live="polite"` and
`aria-live="assertive"`, plus the implicit semantics of `status`, `alert` and
`log`. Explicit `aria-live="off"` disables the region. `status` and `alert`
default to atomic; `log` defaults to additions; other live regions default to
additions and text. Valid `aria-atomic` and `aria-relevant` tokens override
those defaults. An `aria-busy="true"` region or ancestor retains the pre-busy
baseline and emits one coalesced event after busy clears. A newly connected
alert may publish its initial content; other new regions establish a baseline.

Worker checkpoints compare final composed-tree state. Regions publish in
composed-tree order. Within a non-atomic region, additions and text changes use
current preorder, followed by relevant removals in prior preorder. Replacing a
text node at the same position is classified as text. Exact repeated state is
suppressed by semantic identity, changed-node identity, cause flags and text.
Nested live regions own their own descendants, so a parent does not duplicate
their text.

Hidden, `aria-hidden`, `display:none`, visibility/content-visibility hidden,
inert, detached and stale-document regions never enter an event. Navigation
and low-memory requests synchronously retire queued events. The worker checks
the iteration-start document epoch before publication, closing a race with
host-side retirement. Each complete checkpoint also prunes queued events whose
document generation or live-region semantic identity is no longer current.
Navigation resets runtime baselines for the new document;
low memory rebuilds the current document baseline without replaying alerts.
Teardown clears both pending actions and live-region events. Existing leases
remain valid because they own their event and string storage.

## Bounds and loss behavior

- 256 queued events, with deterministic oldest-first eviction;
- 64 events per take lease;
- 64 KiB of valid UTF-8 per event;
- 1 MiB of aggregate queued UTF-8;
- 256 live regions, 4,096 text fragments per region and 16,384 bounded DOM
  visits per worker checkpoint;
- explicit batch loss flag and saturated dropped-event count;
- one event per changed region per worker checkpoint.

If a checkpoint exceeds its scan, region or fragment budget, WebScene retains
the preceding baseline, retires queued events and publishes nothing from the
partial scan. Queue
eviction preserves the newest events and reports the exact bounded loss on the
next take.

## Authored qualification sources

- `semantic_live_region_header_tests.c` pins the C11 event, lease and cap ABI.
- `native_v8_runtime_semantic_live_region_tests.inc` covers terminal, Problems,
  Tasks and Chat ordering; polite/assertive/status/alert/log behavior;
  atomic/relevant/busy coalescing; duplicate, hidden and detached suppression;
  sequence order; navigation, low-memory and teardown retirement; queue/lease
  caps and a bounded worker-time gate.
- `accessibility-semantic-live-regions-source.html` and its dedicated profile
  retain the Chromium-shaped browser DOM oracle.
- `native-binary-interop.test.mjs` pins take/release export visibility.

Per the implementation-only instruction, these sources were authored but not
executed. Only `git diff --check` is used for this slice.

Incremental semantic tree deltas, platform peer mapping, announcement API
delivery, interruption policy, user preference integration and packaged
VoiceOver, Narrator and Orca acceptance remain in later WebScene/AppScene work.
