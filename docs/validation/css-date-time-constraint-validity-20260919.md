# CSS date and time constraint validity source contract

## Implemented boundary

This WebScene #257 slice is based on current `main`
`eb724a194bc4f6be982af213dbf76a759adcb4a5`, after merged child #597/PR
#598. It extends the shared numeric constraint-validity engine to `date`,
`month`, `week`, and `time` inputs. Strict type-specific parsing supplies the
live value, `valueAsNumber`, range underflow/overflow, step mismatch, and bad
input used by `:valid`, `:invalid`, `:in-range`, and `:out-of-range`.

Date values use proleptic Gregorian calendar validation and UTC milliseconds
from 1970-01-01. Month values use whole months from January 1970. Week values
accept valid ISO week numbers and use UTC milliseconds to the week's Monday.
Time values use milliseconds since midnight without a local timezone or
daylight-saving conversion. Programmatic invalid strings are sanitized to the
empty string; native editing may retain transient nonempty invalid text, which
produces `NaN` and bad-input invalidity.

The allowed step remains in the existing numeric engine. Date scales authored
steps by 86,400,000 milliseconds, month by one month, week by 604,800,000
milliseconds, and time by 1,000 milliseconds. Defaults are one date day, one
month, one week, and 60 time seconds. A valid `min`, then a valid `value`
content attribute, supplies the step base. Week otherwise uses −259,200,000,
the Monday of 1970-W01; the other types use zero. `step=any` disables mismatch.
Time additionally uses its periodic domain when a valid minimum is later than
a valid maximum, so values in the gap suffer both underflow and overflow.

## Compiled invalidation bound

No new selector index or document traversal is introduced. Live changes compare
the previous and current structured numeric result and visit only rules indexed
by `$live-form-value` (and `$live-form-range` when range classification changes).
Dynamic `min`, `max`, and `step` mutations continue through the compiled direct
attribute routes added by the preceding numeric slice. Type changes continue
through the existing compiled `type` route and sanitize the retained live value
for the new input state.

## Source contracts

- `tests/WebPlatformSubset/contracts/css-date-time-constraint-validity.html`
  derives focused cases from pinned WPT `date.html`, `month.html`, `week.html`,
  `time.html`, `input-valueasnumber.html`,
  `form-validation-validity-rangeUnderflow.html`,
  `form-validation-validity-rangeOverflow.html`, and
  `form-validation-validity-stepMismatch.html` at WPT revision
  `2c705104a295c48053eeddf7fe0170d790a4e853`.
- `experiments/WebScene.NativeEngine.Probe/tests/native_css_invalidation_tests.inc`
  owns the native companion. It covers conversion scale, leap dates, ISO-week
  base, time wrapping, dynamic bound/step transitions, selector style changes,
  and a physical invalid date edit that script-only browser tests cannot retain.

## Deferred slices and validation status

`datetime-local` parsing and conversion, `valueAsDate`, `stepUp`/`stepDown`, and
the complete constraint-validation IDL (`validity`, `checkValidity`,
`reportValidity`, validation messages, and custom validity) remain separate
#257 work. Extremely large years beyond the bounded native calendar conversion
domain also remain deferred.

Per task direction, no build, generated-binding check, native test, browser/WPT
run, benchmark, computer-use check, scene/pixel check, product check, package
gate, or CI job was run. Only `git diff --check` is used for this commit. The
browser and native contracts remain candidate source evidence until direct
execution establishes parity.
