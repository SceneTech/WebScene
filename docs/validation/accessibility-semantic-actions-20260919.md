# Accessibility semantic action routing boundary

WebScene issue #262 follows the immutable semantic snapshot provider with one
bounded host-to-DOM route for AppScene native accessibility peers. The v1 C ABI
copies a snapshot generation and semantic node identity from an acquired lease.
It carries only numeric fields and copied UTF-8; no AppKit, UI Automation,
AT-SPI or other platform object enters WebScene.

## Contract

Every semantic node advertises the actions its current connected DOM state can
accept. The slice covers focus, press, toggle, increment, decrement, set value
and set selection. Disabled, hidden, inert and detached nodes advertise no
actions. Readonly text controls retain focus and selection but reject value
edits. Press is limited to native buttons, links, action/radio inputs and ARIA
buttons. Toggle is limited to native checkboxes and authored ARIA checkbox or
switch widgets. Numeric adjustment is limited to enabled, writable native
range and number inputs. Value and UTF-16 selection edits are limited to text
selection controls.

Admission copies the payload before returning and reports invalid, stale,
unsupported, full and oversized requests distinctly. The V8 owner thread
revalidates the newest publication at or after the submitted generation, then
re-resolves the same semantic identity and checks top document generation,
nested frame generation, connection, visibility, inertness and current action
capability. Successful work requests a replacement semantic snapshot.
Navigation and low-memory retirement clear the published snapshot and pending
actions before replacement work is admitted; node removal fails worker-side
revalidation even when it races an already admitted request.

## Bounds

- 256 pending actions;
- 64 actions dispatched per fair worker batch;
- 64 KiB for one UTF-8 value;
- 1 MiB of aggregate queued UTF-8;
- strict UTF-8 without embedded NUL;
- zero payload for non-value actions and ordered UTF-16 offsets for selection.

## Authored qualification sources

- `semantic_action_header_tests.c` pins the C11 request, action and cap ABI.
- `native_v8_runtime_semantic_action_tests.inc` covers advertised capabilities,
  worker routing, event/state updates, UTF-16 selection, disabled/readonly
  rejection, payload and queue caps, node/navigation retirement and a bounded
  dispatch-time gate.
- `accessibility-semantic-actions-source.html` and its dedicated profile retain
  the browser DOM action oracle.
- `native-binary-interop.test.mjs` pins export visibility.

Per the implementation-only instruction, these sources were authored but not
executed. Only `git diff --check` is used for this slice.

Live-region announcements, incremental semantic deltas and native platform
peer implementations remain separate follow-ups.
