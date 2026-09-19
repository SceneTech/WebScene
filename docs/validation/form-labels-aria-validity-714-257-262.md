# Form labels and ARIA validity source contract

WebScene issue #714 completes the label and accessibility relationship slice of
#257 and #262. `labels` is exposed as a same-object live `NodeList` on the
implemented labelable control interfaces, and `ElementInternals.labels` returns
the host custom element's same retained collection. Each length, indexed,
`item()`, or iterator read resolves the current tree in document order.

Explicit `for` associations use the first element with the matching ID in the
same document tree. Implicit labels select their first labelable descendant.
Empty `for` attributes suppress implicit association. The supported labelable
set includes non-hidden inputs, buttons, meters, outputs, progress elements,
selects, textareas, and registered form-associated custom elements. Label click
activation uses the same resolver.

The retained dependency data consists of ID-to-element and `for`-to-label sets.
It is populated once with the existing form dependency index, updated by all
ordinary attribute/property/Attr and inserted-subtree hooks, filtered for
connection and document tree on read, and cleared on navigation. Removal leaves
only bounded stale pointers to retained DOM nodes; they cannot participate while
detached. Reparenting and duplicate-ID order changes need no global mutation
scan because ordering is derived from the affected nodes' live ancestor paths.

Semantic snapshots now:

- derive native names and `LABELLED_BY` relationships from every associated
  explicit or implicit label in document order;
- expose native constraint invalidity together with authored `aria-invalid`;
- resolve `aria-errormessage` text and publish the additive
  `ERROR_MESSAGE_V1` relationship kind;
- retain existing `aria-describedby` relationships; and
- include the bounded validation message in the description while the invalid
  control is focused or owns the engine validation message.

All additions use the existing semantic node, relationship, document, and UTF-8
caps. Snapshot and delta publication remain demand driven. There is no DOM
injection, mutation-time document scan, polling, or frame request.

This slice adds native `labels` bindings for button, input, textarea, select,
meter, output, and progress interfaces plus `ElementInternals`.

Per the fast implementation policy, no native build, browser/WPT run,
accessibility backend validation, benchmark, or package gate was executed.
Those acceptance gates remain unexecuted.
