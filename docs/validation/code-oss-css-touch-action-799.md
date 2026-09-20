# Code OSS touch-action and contact pointer implementation (#799)

## Product trigger

Unchanged Code OSS authors 52 `touch-action` declarations across 27 files on
editors, tabs, split views, sliders, terminal, and Chat surfaces. WebScene
previously accepted only legacy mouse input and did not retain a native contact
gesture policy.

## Implemented slice

- Adds generated native/managed CSS identity and CSSOM for non-inherited
  `touch-action`: `auto`, `none`, `manipulation`, `pan-x`, `pan-y`, and the
  two-axis combination, including CSS-wide values, mutation, and removal.
- Decodes the versioned AppScene #298 pointer metadata from unused input flag
  bits while preserving the 48-byte ABI and legacy primary-mouse behavior.
- Exposes stable 1..15 pointer IDs, mouse/touch/pen identity, primary state,
  pointer capture by ID, and terminal `pointercancel` teardown.
- Freezes the intersected ancestor policy at contact start and performs
  axis-qualified retained scrolling without synthetic mouse events.
- Uses a fixed 16-entry gesture array. CSS mutation is interaction-only, adds
  no layout invalidation, document scan, scene node, timer, browser shell, or
  Code OSS patch.

## Authored gates

- `webscene_pointer_input_metadata_tests`: legacy fallback, touch identity,
  stable ID, malformed input, and future-version rejection.
- `contracts/css-touch-action.html`: non-inheritance, CSSOM, canonical axis
  order, invalid-write preservation, CSS-wide inheritance, and removal.
- Native input contract: frozen `pan-y`, touch/pen event fields, capture,
  cancellation, no mouse compatibility events, and `none` suppression.
- `css-touch-action-performance`: 4,096 nodes, 40,960 CSS mutations, 1,000
  gestures, no layout passes, bounded scene publication, no animation-frame
  leak, and a five-second guardrail.

## Evidence state

Implementation and gates are authored from source review. Per the active fast
merge direction, no build, test, WPT, visual, package, memory, lifecycle, or
performance command was executed. Evidence remains zero until full validation.
