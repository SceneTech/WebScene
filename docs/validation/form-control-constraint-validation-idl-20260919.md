# Form-control constraint-validation IDL source contract

## Implemented boundary

This WebScene #257 slice is based on current `main`
`2cd3913fceff7a0c9660aca05ab232fa237bda1d`. It exposes the existing
element-owned constraint model through `validity`, `validationMessage`,
`willValidate`, `setCustomValidity()`, `checkValidity()`, and
`reportValidity()` on input, textarea, select, and button controls.

`ValidityState` is an illegal-constructor Web IDL interface. A control retains
one wrapper, and every flag is read live from its native node. The flags project
the merged required, text, number/range, date/time, radio-group, and select
state. `customError` is stored with the control and participates in both
`ValidityState.valid` and `:valid`/`:invalid`. A barred control can retain a
custom error while `willValidate` is false; its validation message is empty and
its validation methods return true.

Both validation methods dispatch `invalid` at an invalid validation candidate.
The event is cancelable and does not bubble. WebScene #708 extends
`reportValidity()` with the bounded interactive behavior described in
`interactive-constraint-validation-20260919.md`; `checkValidity()` remains a
noninteractive validity query.

## State and invalidation bound

The implementation does not introduce form-wide aggregation, a validity index,
or a document scan. Reading control validity uses the existing element-owned
helpers, including the already-bounded radio group lookup. Changing a custom
message only requests dynamic-state recascade when its empty/nonempty status
changes, which is the transition that can affect `:valid` and `:invalid`.
No CSS structural invalidation source is changed.

## Source contract and deferred work

`tests/WebPlatformSubset/contracts/form-control-constraint-validation-idl.html`
derives focused behavior from the constraint-validation sections of WPT at
revision `2c705104a295c48053eeddf7fe0170d790a4e853`: live `ValidityState`,
required/select/radio flags, text and numeric flags, custom messages, barred
controls, and `invalid` event shape.
`experiments/WebScene.NativeEngine.Probe/tests/native_css_invalidation_tests.inc`
owns the native companion for prototype placement, wrapper identity and
liveness, flag projection, custom-error selector changes, event dispatch, and
barred-control behavior.

Form and fieldset aggregation, submission-time validation, `datetime-local`,
file-input value-missing state, and the UI-only
bad-input and user-edited length transitions remain separate work.

Per task direction, no build, generated-binding check, native test, browser/WPT
run, benchmark, computer-use check, scene/pixel check, product check, package
gate, or CI job was run. Only `git diff --check` is used for this commit. The
authored contract remains unexecuted source evidence until direct execution
establishes parity.
