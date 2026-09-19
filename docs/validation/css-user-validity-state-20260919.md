# CSS user-validity state source contract

## Scope and baseline

This implementation-first slice starts from exact WebScene `main`
`c45cd36ce59c264206aace5d0d3f524e8ded675c`. The issue #257 audit found that
`:enabled` / `:disabled`, `:checked` / `:indeterminate`, and `:in-range` /
`:out-of-range` already have matcher and dependency paths. The selector parser
recognized `:user-valid` and `:user-invalid`, but both fell through matching and
there was no user-interaction state or invalidation dependency.

The implemented boundary adds one non-reflected boolean to the existing cold
form-control record. Native text edits and accessibility value actions set it.
JavaScript `value`, `defaultValue`, textarea child text, and attribute writes do
not create user interaction; after interaction they may change whether the
control is user-valid or user-invalid. Reset through the current form owner
clears the state. Detach and reparent preserve the element-owned state.

Both user pseudos consume the same current live-value validity classification
as `:valid` and `:invalid`. This slice therefore does not claim a broader
constraint-validation implementation. Checkbox/radio required-value rules,
select placeholder rules, pattern/type/step mismatch, date/time range types,
form/fieldset aggregation, submit-attempt state, external `form=` reset, and
the constraint-validation IDL remain later #257 work.

## Invalidation and ownership

The selector dependency compiler records `$live-form-user-validity` for the
two user pseudos alongside their existing `required`, `value`, live-value, and
textarea-child dependencies. The existing live form-value transition compares
the old and new value classification and user-interaction bit in one checkpoint.
It recascades through the compiled selector route only when one of those inputs
changes. Programmatic nonempty-to-nonempty writes after interaction remain a
paint/layout dirty operation without unnecessary user-validity selector work.

No child-list implementation is added. Textarea default-value changes continue
to reuse the merged structural route, and detach/reparent use the existing DOM
mutation checkpoints. Ordinary DOM nodes retain no new inline field because
the boolean remains in pay-for-use `form_control_data`.

## Authored source contracts

- `tests/WebPlatformSubset/contracts/css-user-validity-state.html` derives a
  browser-shaped native-input sequence from CSS UI user-validity behavior and
  checks selector APIs, computed style, programmatic writes, required mutation,
  detach/reparent, and owner-specific reset.
- The existing `live-form-state` native gate now checks the same distinctions
  using the host keyboard-input path beside its 4,096-control bounded
  invalidation gate.

## Validation status

Per the task constraint, no build, browser/WPT run, native test, benchmark,
scene/pixel check, product check, package gate, or CI job was run. Only
`git diff --check` is used for this commit. The new profile entry remains a
candidate until direct Chromium and native execution establish parity.
