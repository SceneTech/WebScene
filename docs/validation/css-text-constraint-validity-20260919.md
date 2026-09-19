# CSS text-control constraint validity source contract

## Implemented boundary

This WebScene #257 slice is based on exact `main`
`497b4dbbd6e5b576bbc07428e9b6e586a656f36e`, after merged #589/#590. It
extends the existing selector-facing validity engine with a reusable structured
text constraint result. Text, search, telephone, URL, email, password, and
textarea controls now contribute pattern mismatch; email and URL controls
contribute type mismatch; and applicable controls contribute minimum/maximum
length state after native user editing.

The engine reads the live value rather than the `value` content attribute.
Length is counted in UTF-16 code units. The existing dirty-value flag remains
the live/default boundary, while a separate user-edit bit prevents script
`.value` and `setRangeText()` writes from manufacturing too-short or too-long
state. Reset clears both user-edit provenance and user-validity interaction.

Controls barred from constraint validation match neither `:valid` nor
`:invalid`. This includes hidden/button/reset inputs, button/reset buttons,
readonly controls, directly or fieldset-disabled controls (with the first
legend exception), and controls under `datalist`.

## Pattern and type scope

Patterns are whole-value ECMAScript matches; an absent or invalid pattern and
an empty value do not mismatch. Multiple email controls apply the pattern to
each comma-separated address. This slice uses the native standard-library
ECMAScript grammar for the focused ASCII source contract. HTML `v`-flag
Unicode-set intersection/subtraction and complete URL-parser equivalence remain
deferred rather than being claimed by this candidate.

Email validation covers the HTML email-address shape and comma-separated
`multiple` lists. URL validation accepts absolute scheme URLs and requires a
nonempty authority for common network schemes. Full URL Standard parsing is a
separate prerequisite for broader URL-input conformance.

## Compiled invalidation bound

Validity selectors compile subject/relationship routes for `pattern`,
`minlength`, `maxlength`, `readonly`, `type`, `multiple`, and the existing live
value dependency. `disabled` uses the existing inherited fieldset route. A live
edit compares the previous and current structured text state and visits only
rules indexed by `$live-form-value`; unrelated rules and controls are not
cascade targets. No document-wide validity index or walk is introduced.

## Source contracts

- `tests/WebPlatformSubset/contracts/css-text-constraint-validity.html` derives
  focused cases from pinned WPT `form-validation-validity-patternMismatch`,
  `form-validation-validity-typeMismatch`, `form-validation-validity-tooLong`,
  `form-validation-validity-tooShort`, `form-validation-willValidate`, and
  `input-pattern-dynamic-value` at WPT revision
  `2c705104a295c48053eeddf7fe0170d790a4e853`.
- `experiments/WebScene.NativeEngine.Probe/tests/native_css_invalidation_tests.inc`
  owns the native companion, including real native text input for too-short and
  too-long transitions that script-only browser tests cannot establish.

## Deferred slices and validation status

Constraint-validation IDL (`validity`, `willValidate`, `checkValidity`,
`reportValidity`, validation messages, and custom validity), submission/form
aggregation, bad input, step mismatch, complete numeric/date constraints,
HTML `v`-mode patterns, and complete URL parsing remain separate slices.

Per task direction, no build, native test, browser/WPT run, benchmark,
computer-use check, scene/pixel check, product check, package gate, or CI job
was run. Only `git diff --check` is used for this commit. The browser and native
contracts remain candidate source evidence until direct execution establishes
parity.
