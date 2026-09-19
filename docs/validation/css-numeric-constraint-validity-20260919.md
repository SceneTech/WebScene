# CSS numeric constraint validity source contract

## Implemented boundary

This WebScene #257 slice is based on current `main`
`804266ab33a15d49a1a32a31f5ce4479e39a3d67`, after merged #595/#596. It
extends selector-facing constraint validity for `number` and `range` inputs.
The structured numeric result records bad input, range underflow, range
overflow, and step mismatch and joins the existing required and text results
used by `:valid`, `:invalid`, `:user-valid`, and `:user-invalid`.

Both selector matching and `valueAsNumber` consume the live value. Number and
range use the HTML floating-point grammar, unit scale, and default step of one.
The step base is a valid minimum, then a valid `value` content attribute, then
zero. `step=any` disables mismatch. Range inputs additionally use default bounds
of 0 and 100, clamp programmatic numeric values, and round a mismatching value
toward the positive direction when candidates are equally close. Invalid range
text falls back to its stepped midpoint.

Programmatic invalid number text is sanitized to the empty string. Native user
editing can retain a transient nonempty value such as `-`; that value produces
`NaN` through `valueAsNumber` and bad-input invalidity until it becomes a valid
number or is cleared.

Controls barred from constraint validation match neither `:valid` nor
`:invalid`. The shared `will_validate` gate covers readonly, directly or
fieldset-disabled, datalist-contained, and barred-type controls. Range
pseudo-classes remain value-range classifications, so a barred numeric control
can still match `:in-range` or `:out-of-range`.

## Compiled invalidation bound

Validity selectors compile subject and relationship routes for `min`, `max`,
and `step` in addition to the existing `type`, `readonly`, `disabled`, and live
value dependencies. A live edit compares the previous and current structured
numeric result and only visits rules indexed by `$live-form-value`; bound and
step mutations use their direct compiled attribute routes. No form-wide
validity index or document scan is introduced.

## Source contracts

- `tests/WebPlatformSubset/contracts/css-numeric-constraint-validity.html`
  derives focused cases from pinned WPT
  `form-validation-validity-badInput`,
  `form-validation-validity-rangeUnderflow`,
  `form-validation-validity-rangeOverflow`,
  `form-validation-validity-stepMismatch`, `input-valueasnumber`, and range
  sanitization coverage at WPT revision
  `2c705104a295c48053eeddf7fe0170d790a4e853`.
- `experiments/WebScene.NativeEngine.Probe/tests/native_css_invalidation_tests.inc`
  owns the native companion. It covers live value/valueAsNumber transitions,
  dynamic min/max/step changes, range sanitization, validation candidacy, and a
  physical bad-input edit that script-only browser tests cannot establish.

## Deferred slices and validation status

Date, month, week, time, and datetime-local require their own syntax parsers,
epoch conversions, per-type step scales, and daylight-independent arithmetic.
They remain a separate #257 slice rather than borrowing number semantics.
Constraint-validation IDL (`validity`, `willValidate`, `checkValidity`,
`reportValidity`, validation messages, and custom validity), submission/form
aggregation, and stepUp/stepDown also remain separate work.

Per task direction, no build, generated-binding check, native test, browser/WPT
run, benchmark, computer-use check, scene/pixel check, product check, package
gate, or CI job was run. Only `git diff --check` is used for this commit. The
browser and native contracts remain candidate source evidence until direct
execution establishes parity.
