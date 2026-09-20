# NativeWeb pointer device adapter contract (#807 / AppScene #298)

## Boundary

AppScene's installed NativeWeb adapter previously reduced every
`ws_input_event` to the legacy `document::pointer` overload. The unchanged Code
OSS V8 runtime does not use this adapter, but the installed SDK must preserve
the same pointer metadata for other AppScene applications.

## Implemented slice

- Adds an additive `pointer_input` descriptor with mouse/touch/pen, stable ID
  1..15, and primary state.
- Keeps the existing `document::pointer` symbol and behavior as primary mouse
  ID 1.
- Appends device, ID, and primary fields to the public event prefix without
  shifting existing fields.
- Tracks pressed targets in a fixed 16-entry array so cancellation and release
  retire only the matching contact. Only primary input drives the legacy
  `:active` slot; touch never creates sticky mouse hover.
- Rejects invalid device values and IDs before DOM dispatch.

## Authored gates

The NativeWeb contract covers legacy identity, touch and pen identity, primary
state, two simultaneous contact IDs, matching cancellation, cancel-without-
activation, malformed IDs, and a 10,000-move bounded-state sequence.

## Performance and evidence

Metadata adds no DOM scan, layout, scene publication, timer, browser component,
hash table, or heap-backed contact registry. Implementation and gates are
authored from source review. Per the active fast direction, no local build,
test, package, performance, memory, or lifecycle command was executed.
