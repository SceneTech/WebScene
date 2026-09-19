# Form submit and requestSubmit source contract

## Implemented boundary

This WebScene #257 slice starts from exact `main`
`50bb4d01c070f719db5805c04167beedef503ad7`, after #606 established the shared
form-submission constraint-validation route. It exposes browser-shaped
`HTMLFormElement.submit()` and `requestSubmit(submitter)` methods.

`requestSubmit()` validates an explicit submitter before changing form state. A
non-submit button raises `TypeError`; a submit button owned by another form
raises `NotFoundError`. A valid call enters the existing #606 path, so owned
controls receive the submit-attempt user-validity transition, constraint
validation runs unless `novalidate` or `formnovalidate` applies, and an invalid
form stops before `submit`. The cancelable `submit` event exposes the explicit
submitter, or `null` when no submitter was supplied.

After an uncanceled `submit`, WebScene constructs the supported form entry list
with `FormData(form, submitter)`. Construction synchronously dispatches a
bubbling, non-cancelable `formdata` event carrying that same `FormData`
object. Canceling `submit` prevents entry-list construction. The legacy
`submit()` method goes directly to entry-list construction, bypassing
constraint validation, submit-attempt user validity, and the `submit` event.

This slice also gives `fieldset` elements their dedicated
`HTMLFieldSetElement` wrapper and constraint-validation IDL. A fieldset remains
barred from constraint validation: `willValidate` is false and its validation
methods return true. Its previously implemented aggregate `:valid` / `:invalid`
selector state remains based on descendant controls.

## Scope and routing

The new request method calls the existing `dispatch_form_submit` route. It does
not add a second validation loop or broaden control-type validity. Entry-list
construction reuses the existing supported `FormData` control filtering and
submitter projection. Navigation, encoding transport, interactive validation
UI, form-associated custom elements, file controls, dirname entries, and a
dedicated `FormDataEvent` constructor remain outside this bounded source slice.
The later `formdata-event-file-dirname-20260919.md` source contract extends the
last three items without changing this submission route.

No structural ancestor invalidation route is added. Fieldset aggregate selector
matching continues to read live descendant state directly, leaving descendant
validity-driven ancestor computed-style recascade with the structural
invalidation workstream.

## Authored evidence and validation status

`tests/WebPlatformSubset/contracts/form-submit-request-submit.html` records the
IDL shape, submitter errors and ownership, #606 validation reuse, cancellation,
submitter projection, `submit` / `formdata` order, legacy bypass behavior, and
dedicated fieldset IDL derived from the HTML form-submission algorithms in WPT
revision `2c705104a295c48053eeddf7fe0170d790a4e853`.

Per task direction, no build, generated-binding check, native test,
browser/WPT run, benchmark, computer-use check, scene/pixel check, product
check, package gate, or CI job was run. Only `git diff --check` is used for this
commit. The authored contract remains unexecuted source evidence.
