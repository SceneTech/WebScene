# Typed semantic value and selection contract (#760)

## Scope

The semantic snapshot ABI now carries optional typed state for native and ARIA value controls and live text-control selections. The fields live in fixed-size `webscene_semantic_typed_value_v2` companion tables exposed by new v2 snapshot and delta lease symbols. Existing v1 node and operation array layouts, strides, symbols, and versions remain unchanged. Each v2 table has exactly one typed record per corresponding v1 node or delta operation, and presence flags identify available values.

No string parsing is required by a platform adapter. Numeric current/minimum/maximum/increment values are finite `double` fields. Text caret and selection offsets use DOM UTF-16 code units, matching `selectionStart`, `selectionEnd`, and the existing semantic `SET_SELECTION` action.

## Producer rules

- Native `input[type=number]` and `input[type=range]` publish live current state and normalized min/max/step metadata.
- ARIA slider, spinbutton, scrollbar, progressbar, and meter roles publish finite `aria-valuenow`, `aria-valuemin`, and `aria-valuemax` values.
- Native `progress` and `meter` publish finite value and range data with their platform defaults.
- Inverted bounds are omitted. Malformed and non-finite values never set presence flags.
- Text inputs and textareas publish live UTF-16 selection start/end and the active caret endpoint. Backward selections place the caret at the start endpoint.
- Typed fields are copied into INSERT and UPDATE deltas and participate in unchanged-node suppression.

## Compatibility and performance properties

The extension adds one bounded fixed-size companion vector. Snapshot construction reserves it alongside the already-bounded node vector, and platform reads remain immutable snapshot reads. The existing 16,384-node cap, string budgets, delta operation cap, retained leases, and worker-owned publication model are unchanged. The existing large-snapshot and 100-cycle delta performance gates therefore cover the larger record footprint without introducing platform-triggered cascade, layout, paint, or scene publication.

Header contracts pin the v1 prefix layouts, v2 companion field order, new export symbols, and presence-bit values. Runtime contracts cover native and ARIA numeric values, malformed and inverted ranges, UTF-16 offsets through a supplementary Unicode scalar, backward caret direction, dynamic selection deltas, and complete typed delta payloads.

## Validation status

The contracts and gates are authored in this change. They were not executed in this implementation pass under the current fast-merge instruction; CI remains the recorded execution source for the focused PR.
