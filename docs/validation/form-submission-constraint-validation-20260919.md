# Form submission constraint-validation source contract

## Implemented boundary

This WebScene #257 slice starts from exact `main`
`79d76c3a2ec92f08d6120232114d9271c3b35a83`, after the control-level
constraint-validation APIs merged in #604. It adds live `:valid` / `:invalid`
aggregation for forms and fieldsets. Forms inspect candidate controls with that
form owner, including controls associated through `form=`. Fieldsets inspect
candidate descendants. Barred controls do not make either aggregate invalid,
and form/fieldset elements never match `:user-valid` or `:user-invalid`.

`HTMLFormElement.checkValidity()` and `reportValidity()` statically validate all
owned candidates in tree order. Every invalid candidate receives the existing
cancelable, non-bubbling `invalid` event, and either method returns false when
any candidate was invalid. `reportValidity()` deliberately adds no native
validation UI in this slice.

Submit-button and implicit submission now set user validity on each owned
input, select, and textarea before validation. A failing validation dispatches
`invalid` and stops the `submit` event. `novalidate` and `formnovalidate` skip
constraint validation while retaining the submit-attempt user-validity
transition. Successful or bypassed submission continues through the existing
cancelable `submit` event and submitter projection.

## Invalidation and ownership bound

Aggregate selector matching reads current owner/descendant state directly. The
implementation does not add a validity index, duplicate child-list machinery,
or a competing structural invalidation route. Consequently, this source slice
claims live selector APIs for form/fieldset aggregation; ancestor computed-style
recascade after a descendant validity transition remains with the structural
invalidation workstream.

The submit-attempt transition reuses the existing bounded
`$live-form-user-validity` subject path. It does not broaden validation to
datetime-local, file-input value-missing, form-associated custom elements, or
the remaining UI-only bad-input and user-length transitions. The
`requestSubmit()` / `submit()` method surface and dedicated fieldset IDL
exposure also remain separate work.

## Authored evidence and validation status

`tests/WebPlatformSubset/contracts/form-submission-constraint-validation.html`
derives the focused aggregate, form-method, invalid-event, submit gating,
no-validate, and user-validity behavior from the HTML constraint-validation and
submission algorithms and the CSS validity/user-interaction selector contracts
represented in WPT revision `2c705104a295c48053eeddf7fe0170d790a4e853`.
The native companion records the same observable boundary through the generated
form prototype and native activation path.

Per task direction, no build, generated-binding check, native test,
browser/WPT run, benchmark, computer-use check, scene/pixel check, product
check, package gate, or CI job was run. Only `git diff --check` is used for this
commit. The authored contracts remain unexecuted source evidence.
