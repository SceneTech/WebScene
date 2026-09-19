# Form ownership invalidation source contract

Issue #713 completes mutation-local validity invalidation for the form work in
#257. A retained reverse index maps explicit `form=` tokens to controls and
form IDs to their candidate owners. The index is built once from the active
document, updated by insertion and attribute hooks, bounded by retained DOM
nodes, and cleared on navigation. Form-ID and explicit-owner mutations therefore
do not rescan the document.

Child-list mutation routes pass the changed parent and inserted or removed
subtree to one invalidation helper. It visits controls in only those subtrees,
the changed fieldset when first-legend ordering can matter, explicit dependents
of moved forms, and the form/fieldset aggregate ancestors. The helper feeds a
synthetic `$live-form-ownership` dependency through the existing compiled
selector-transition planner. Its existing style batch coalesces duplicate
subjects and routed descendants; it does not poll, request animation frames,
or schedule a whole-document recascade.

The same route covers `form` and form `id` changes, fieldset `disabled`,
`setCustomValidity()`, and form-associated custom-element registration and
validity changes. Existing radio-group snapshots still surround structural and
attribute operations, and submission continues to resolve ownership from the
live DOM.

Parser construction needs no transition because its first cascade observes the
completed ownership graph. Custom-element reactions that synchronously perform
another mutation enter that mutation's normal attribute or child-list hook;
this slice does not add a separate reaction-queue dependency mechanism.

Per the fast implementation policy, no native build, browser/WPT run,
benchmark, or package validation was executed. Those acceptance gates remain
unexecuted.
