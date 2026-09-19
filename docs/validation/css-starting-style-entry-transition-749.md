# Starting-style entry transition contract (#749)

WebScene retains document-level `@starting-style` as ordinary selector rules
marked with an internal, always-inactive media token. This reuses the existing
selector indexes, specificity, cascade layers, stylesheet ownership, mutation
invalidation, and teardown paths without maintaining a parallel rule engine.
Real media conditions surrounding a starting-style group remain attached and
are evaluated when an element enters the rendered state.

When an element is first rendered or returns from `display:none`, WebScene builds
one temporary initial cascade from the normal and matching starting rules. The
combined list is sorted by normal layer, specificity, source and important
precedence before declarations are replayed. Only
an admitted opacity, 2D transform, or background-color transition copies values
into the existing retained transition machine. The temporary style is released
before the normal computed style is exposed. Static and already-rendered nodes
pay two bits each and do not allocate snapshots or request frames.

`transition-behavior: allow-discrete` is retained per transition-list item. A
transition from a rendered display value to `none` keeps the used box until the
host-clock duration boundary while computed style reports the target `none`.
Cancellation, replacement, or a changed transition policy releases the deferred
state. Completion removes the box, dirties layout once, and stops frame demand.
A true DOM removal clears the subtree's before-change lifecycle and animation
runtime so reinsertion can enter again without detached frame demand. Atomic
connected-tree reparenting keeps the lifecycle generation and does not invent a
new entry.

Coverage:

- `webscene_css_parser_tests` proves the Rust syntax stream emits qualified
  children inside `@starting-style`;
- `native_web_css_service` proves preparation, nested-media retention, initial
  opacity/translate sampling, cascade precedence, midpoint/end values, discrete
  display deferral, completion, re-entry, and zero animation-runtime allocations
  across 256 unrelated nodes;
- `test_starting_style_entry_and_discrete_display_transitions` covers live
  `CSSStartingStyleRule` identity and mutation, host-clock entry/display behavior,
  removal/reinsertion, stylesheet teardown, and zero detached frame demand;
- `css-starting-style-entry-transition.html` is the browser/native candidate.

This slice does not add broad discrete-property interpolation, view transitions,
or new margin/border transition machines. Those declarations still participate
in the temporary cascade but only already-supported transition properties are
admitted.
