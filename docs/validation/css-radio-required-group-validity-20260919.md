# CSS required-radio group validity source contract

## Implemented boundary

WebScene #257 previously evaluated a required radio as an isolated non-text
control. A nonempty submission `value` could make an unchecked radio appear
valid, while checking a peer did not update the required member's selectors.

This slice models the radio button group's value-missing rule for `:valid`,
`:invalid`, `:user-valid`, and `:user-invalid`. A named radio group contains
radios in the same DOM tree with the same case-sensitive nonempty `name` and
the same form owner. If any member is required and no member is checked, every
member is invalid. An unnamed radio remains its own group. Explicit `form=`
owners are resolved inside the radio's tree, so a same-name radio belonging to
another form cannot satisfy or clear the group.

The matcher, runtime activation path, and existing radio `:indeterminate`
matching now share that group definition. Programmatic checkedness, normal
activation, and cancelled activation update the changed radio and its group
peers. Shadow-tree radios remain partitioned from light-tree radios, and the
native unified iframe representation is not crossed while discovering a group.

## Compiled invalidation bound

Validity and radio `:indeterminate` selectors compile a
`$live-form-radio-group-checkedness` dependency. The changed radio uses the
existing subject transition, and each peer is revisited with only rules indexed
by that group dependency. This replaces the previous generic dynamic-state
refresh for peer radios. Relational selectors still follow their compiled route;
unrelated selector rules and radios outside the group are not cascade targets.

## Authored source contracts

- `tests/WebPlatformSubset/contracts/css-radio-required-group-validity.html`
  derives its requiredness aggregation and group partition cases from the WPT
  constraint-validation coverage and the HTML radio-button group definition.
  It covers an external form-associated member, another form owner, a shadow
  tree, checked IDL mutation, activation, cancelled activation, selector APIs,
  and computed style.
- The native `live-form-state` companion owns the same group/owner partitions
  and transition sequence so the native matcher and compiled cascade path have
  a focused source contract beside the browser-facing contract.

## Deferred slices

- Dynamic `name`, `type`, `form`, `required`, form-owner `id`, and DOM
  membership transitions are covered by the companion
  `css-radio-group-mutation-invalidation-20260919.md` source contract.
- Checked content-attribute mutation after parsing still uses the generic
  attribute transition. The live `checked` IDL property and activation paths
  are covered here; content-default/reset interactions across a group remain a
  follow-up with external-owner reset traversal.
- Complete constraint-validation IDL, submit-time validation, custom validity,
  and form/fieldset validity aggregation remain absent.
- Required select validity and the remaining pattern, type, step, length, and
  date/time constraints remain separate slices.

## Validation status

Per the task constraint, no build, browser/WPT run, native test, benchmark,
computer-use check, scene/pixel check, product check, package gate, or CI job
was run. Only `git diff --check` is used for this commit. The profile entry
remains a candidate until direct Chromium and native execution establish
parity.
