# CSS required-select validity source contract

## Implemented boundary

This WebScene #257 slice follows merged #585/#586 at exact `main`
`dc17f4684ee195dab07b1ed74e2801e186a25112`. Required selects previously fell
through the generic non-text validity rule, which inspected a `value` attribute
on the select itself. The matcher now reads each option's current selectedness.
It reports value missing when no option is selected, or when the only selected
option is the empty first direct child of a non-multiple select whose display
size is one.

An empty selected option under `optgroup` is a real option rather than a
placeholder label. A selected empty option also satisfies a multiple select or
a select with display size greater than one. Missing, invalid, and zero `size`
values retain the HTML default display size.

The existing option state keeps content defaults separate from live
selectedness. `selected` content mutation affects an option whose selectedness
has not been initialized. The `option.selected`, `select.value`, and
`select.selectedIndex` setters initialize live selectedness; later content
default changes do not overwrite it. Form reset clears that live state and
recomputes selection from the current content defaults.

## Compiled invalidation bound

Validity selectors compile a `$live-form-select-selectedness` dependency.
Programmatic selectedness, native keyboard and popup selection, reset, option
value/text/default mutations, and option insertion, removal, or movement revisit
the owning select through only rules indexed by that dependency. `required`,
`multiple`, and `size` keep their normal attribute transitions. Related
selectors retain their compiled subject/ancestor route; unrelated selectors
and selects are not cascade targets.

Child-list invalidation finds at most the containing select for each changed
parent. It does not collect document-wide select state or introduce retained
indexes. The native source contract exercises selector matching and computed
style after the same IDL, content-default, reset, and structural transitions as
the browser-facing contract.

## Authored source contracts

- `tests/WebPlatformSubset/contracts/css-select-required-validity.html` derives
  focused cases from WPT constraint-validation select tests and the HTML
  placeholder-label option definition. It covers single and multiple selects,
  display size, direct versus optgroup ancestry, no selection, live setters,
  content defaults, reset, and option value/text/tree mutation.
- `experiments/WebScene.NativeEngine.Probe/tests/native_css_invalidation_tests.inc`
  owns the native companion for the same matcher and compiled cascade path.

## Deferred slices

- Constraint-validation IDL (`validity`, `willValidate`, `checkValidity`,
  `reportValidity`, and custom validity), submit-time validation, and
  form/fieldset validity aggregation remain absent.
- External `form=` ownership is not part of select value-missing state, but the
  existing descendant-only `form.reset()` implementation still does not reset
  an externally associated select. That owner traversal remains a separate
  forms slice.
- Complete select normalization for malformed parser states, disabled-option
  picker behavior, and full `size` Web IDL reflection remain outside this
  focused selector-validity change.

## Validation status

Per task direction, no build, native test, browser/WPT run, benchmark,
computer-use check, scene/pixel check, product check, package gate, or CI job
was run. Only `git diff --check` is used for this commit. The contract remains a
candidate until direct Chromium and native execution establish parity.
