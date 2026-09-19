# ElementInternals constraint-validation source contract

## Implemented boundary

This WebScene #257 slice starts from exact `main`
`58bf86f18aef59eb84393170816788158846ea7e`, after the form-associated
custom-element entry-list and lifecycle foundation landed.

`ElementInternals` now exposes `setValidity()`, a retained live `ValidityState`,
`validationMessage`, `willValidate`, `checkValidity()`, and
`reportValidity()`. The validity-flags dictionary covers all ten HTML flags.
An invalid flag set requires a nonempty message, and an optional validation
anchor must be an element descendant of the custom host. Clearing the flags
updates the already-returned `ValidityState` object.

The native form model marks definitions that snapshot static
`formAssociated = true` as constraint-validation candidates. Their internals
flags feed host `:valid`/`:invalid` matching, form and fieldset aggregation,
form-level validation, and cancelable non-bubbling `invalid` events. Disabled,
disabled-fieldset, and `datalist` ancestry bar the host while retaining its
internals validity flags; barred hosts have an empty validation message and
their validation methods succeed.

## Interaction and restoration

A submit attempt marks owned form-associated custom elements as interacted,
so the host participates in `:user-valid` and `:user-invalid` alongside native
controls. Form reset clears that interaction state before invoking the custom
element's `formResetCallback`.

The custom-elements platform publishes the bounded host hook
`__webSceneRestoreFormAssociatedState(element, mode)`. It dispatches the
definition-snapshotted `formStateRestoreCallback` with the restoration value
retained by `setFormValue()` and either `"restore"` (the default) or
`"autocomplete"`. This supplies a concrete integration point for a host
session-history or autofill driver without inventing restoration timing in the
DOM layer.

## Deferred surface

WebScene #708 adds interactive `reportValidity()` focus and feedback and uses
the retained validation anchor for both direct and form-level reports.
Selector reads and subject recascade are live, but this slice adds no
descendant-driven structural recascade for a form or fieldset whose aggregate
validity changed. Label association, ARIA reflection, `CustomStateSet`,
`HTMLFormElement.elements` integration, and a persisted session-history or
autofill store remain separate work.

## Authored evidence and validation status

`tests/WebPlatformSubset/contracts/element-internals-constraint-validation.html`
records the Web IDL surface, form-associated admission, same-object live
validity, all flags, message and anchor rules, barred behavior, subject and
aggregate matching, invalid-event dispatch, submit-attempt user validity,
reset, and restoration callback delivery. It derives from the WHATWG HTML
form-associated custom-element and constraint-validation algorithms and the
CSS UI user-validity behavior.

Per task direction, no build, generated-binding check, native test,
browser/WPT run, benchmark, computer-use check, scene/pixel check, product
check, package gate, or CI job was run. Only `git diff --check` is used for this
commit. The authored contract remains unexecuted source evidence.
