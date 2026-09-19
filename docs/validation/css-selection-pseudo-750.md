# Scoped `::selection` retained paint for Code OSS

Issue: WebScene #750. Parent selector/paint trackers: #237, #241, #235.

Unchanged Code OSS: `645f29cc3176500b4b5762ba887cf2a7f0ffdf2c`

WebScene now separates `::selection` rules from ordinary element cascade,
matches global or selector-qualified origins against each selected text run,
and applies the winning `color`/`background-color` declarations with author
layer, specificity, importance, source-order, and root custom-property rules.
The Window Selection is normalized into same-text-node retained range records,
then shares the Custom Highlight text-fragment intersection and command path.

Selection changes rebuild one bounded paint ledger and dirty one retained
scene. Rules are indexed once per refresh, so unrelated ordinary CSS does not
scale with selected text nodes. Static selections add no DOM or visual-tree
nodes and request no idle frames.

Focused native coverage checks scoped theme colors through `window.find`,
global cross-node colors through `selectAll`, and clear cleanup. The neutral
browser candidate covers a forward cross-node Range. Native `Selection.addRange`
and input/textarea byte-oriented selection paint remain separate API/control
slices; this issue claims the already-supported Window Selection mutations.
