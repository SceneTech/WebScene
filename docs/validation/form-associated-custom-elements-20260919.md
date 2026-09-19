# Form-associated custom-element source contract

## Implemented boundary

This WebScene #257 slice starts from exact `main`
`0f71bc896740a7260591d6c85a7043c02f0aaace`, after #610 established the
`FormDataEvent`, file-entry, and `dirname` entry-list behavior.

The shared custom-element platform now exposes `HTMLElement.attachInternals()`
and a branded, illegally constructible `ElementInternals`. A custom-element
definition snapshots its constructor's static `formAssociated` value.
`attachInternals()` succeeds once for an autonomous custom element, including
inside its constructor. `ElementInternals.setFormValue()` is admitted only for
a definition that opted into form association.

`setFormValue(value, state)` accepts the bounded HTML union used by this slice:
`null`, `File`, `FormData`, or a value converted to string. Omitting `state`
stores the submission value as restoration state; an explicit state is stored
separately and never submitted. `null` omits the element. A string or `File`
uses the host's nonempty `name`, while a `FormData` contributes its own names,
values, file identities, duplicates, and order.

## Ownership, lifecycle, and entry ordering

`ElementInternals.form` tracks the nearest ancestor form or an explicit `form`
attribute in the same tree. The custom-element reaction route observes
connection, disconnection, reparenting, host `form` changes, form `id` changes,
and disabled host or fieldset transitions. It delivers the snapshotted
`formAssociatedCallback`, `formDisabledCallback`, and `formResetCallback` when
those transitions occur. Form reset callbacks run after the native controls
have reset and may replace the custom element's next submission value.

Both top-level and frame-realm `FormData(form)` constructors visit all elements
in tree order. They ask the custom-element platform to consume opted-in custom
elements before applying the existing native-control entry rules. This keeps
custom `FormData` entries, external form ownership, native controls, duplicate
names, and the final mutable `formdata` event in one ordered list. Disabled and
`datalist`-nested custom elements are omitted.

## Deferred surface

This bounded slice does not add `ElementInternals.setValidity()`, validity and
validation-message getters, label association, ARIA reflection, custom states,
interactive validation UI, or custom-element participation in form/fieldset
constraint aggregation. The restoration state is retained for a future
session-history/autofill restoration trigger; no `formStateRestoreCallback` is
dispatched yet. `HTMLFormElement.elements` remains the native-control
collection. No structural ancestor style invalidation route is added.

## Authored evidence and validation status

`tests/WebPlatformSubset/contracts/form-associated-custom-elements.html`
records constructor branding, one-shot attachment, static admission,
association/disabled/reset lifecycle, explicit and ancestor form ownership,
all supported submission-value variants, state separation, external ownership,
and exact mixed native/custom entry ordering. It derives from the WHATWG HTML
custom-elements and constructing-the-entry-list algorithms.

Per task direction, no build, generated-binding check, native test,
browser/WPT run, benchmark, computer-use check, scene/pixel check, product
check, package gate, or CI job was run. Only `git diff --check` is used for this
commit. The authored contract remains unexecuted source evidence.
