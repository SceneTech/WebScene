# CSS required-checkbox validity source contract

## Scope and baseline

This implementation-first slice starts from exact WebScene `main`
`2b64b08965aeba5ecbd33fcc0bd1beaaceaf6243`, after merged #571/#572.
The #257 audit found that `:valid` and `:invalid` treated every required
non-text control as empty when its authored `value` attribute was absent or
empty. That is not the required-checkbox rule: a checkbox's submission value
does not satisfy the constraint; its current checkedness does.

The implemented boundary changes required checkboxes only. Their validity now
reads initialized live checkedness when present and otherwise reads the
`checked` content default. Therefore `checkbox.checked = false` can make a
required checkbox invalid while leaving `checked` present, later content writes
do not overwrite that initialized live state, and reset restores checkedness
from the current content default. No new per-node state is added.

## Invalidation and ownership

The selector dependency compiler records `$live-form-checkedness` for
`:checked`, `:valid`, `:invalid`, `:user-valid`, and `:user-invalid`. It also
records the `checked` and `type` content inputs for validity. Programmatic and
native checkedness transitions now run through the compiled selector route for
the changed input. A simple subject selector therefore recascades that subject;
combinators and `:has()` retain their already-compiled related routes. The
transition does not scan unrelated controls or rules.

Reset compares old and restored checkedness while the existing reset batch is
active, so repeated work is coalesced with the value and user-validity reset.
Checkbox validity is element-owned and needs no form-wide walk. The existing
radio-only group refresh remains in place for its already-supported shared
`:indeterminate` state; radio constraint aggregation is not claimed here.

## Audit boundary and deferred slices

- Required radio validity still uses the incomplete generic non-text path. It
  does not aggregate checkedness or requiredness by same tree, name, and form
  owner; the existing activation group also needs external-owner partitioning.
- Required select validity does not inspect live selectedness or the single-
  select placeholder-label option. `value`, `selectedIndex`, and option
  `selected` setters do not yet share a dedicated owner-select transition.
- Pattern and type mismatch, step mismatch, bad input, length constraints, and
  date/time parsing and range states are absent. Existing range matching covers
  finite number/range min/max classification only.
- Constraint-validation IDL (`validity`, `willValidate`, `checkValidity`,
  `reportValidity`, and custom validity) is absent. Submit dispatch does not
  validate controls or establish submit-attempt user validity.
- `form`/`fieldset` validity aggregation is absent. External `form=` controls
  are discoverable for submission ownership, but `form.reset()` currently
  traverses descendants and therefore does not reset external owners. Owner
  changes through `form`/`id` mutation need old/new-owner invalidation.

These are separate owner- and group-sensitive slices. Folding them into this
checkbox change would duplicate the existing structural mutation paths before
their ownership model is made consistent.

## Authored source contracts

- `tests/WebPlatformSubset/contracts/css-checkbox-required-validity.html`
  derives a focused contract from WPT constraint-validation behavior. It checks
  selector APIs and computed style across content default, live IDL, dirty
  checkedness, and reset transitions.
- The native `live-form-state` source companion checks the same six-state
  sequence. The compiled live-state dependency provides the implementation
  bound: subject selectors touch one checkbox, while relational selectors touch
  only nodes reached by their compiled route.

## Validation status

Per the task constraint, no build, browser/WPT run, native test, benchmark,
scene/pixel check, product check, package gate, or CI job was run. Only
`git diff --check` is used for this commit. The profile entry remains a
candidate until direct Chromium and native execution establish parity.
