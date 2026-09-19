# CSS radio-group mutation invalidation source contract

## Implemented boundary

Required-radio validity now follows the old and new group when a mutation can
change group membership or ownership. The covered inputs are `name`, `type`,
`form`, `required`, a form owner's `id`, and DOM ancestry or tree membership.
The group definition remains case-sensitive nonempty `name`, the same DOM tree,
and the same resolved form owner. Moving a radio across a form or shadow-tree
boundary therefore updates surviving peers in both partitions.

Before a relevant mutation, the runtime snapshots the connected radios'
value-missing and group-indeterminate states. After the mutation it compares
those states and revisits only radios whose selector state changed. The revisit
uses the existing `$live-form-radio-group-checkedness` rule index and each
selector's compiled subject or relational route. It does not issue a document
or cascade-root subtree recascade for group peers. If no stylesheet consumes
the dependency, no snapshot is collected.

## Authored source contracts

- `tests/WebPlatformSubset/contracts/css-radio-required-group-validity.html`
  extends the WPT-derived required-radio contract with old/new partitions for
  `name`, `type`, `form`, `required`, owner `id`, form ancestry, and light/shadow
  tree moves. Every transition checks both selector matching and computed style.
- `experiments/WebScene.NativeEngine.Probe/tests/native_css_invalidation_tests.inc`
  repeats those transitions beside the native cascade implementation.

## Deferred slices

- Checked content-default mutation and form reset across externally associated
  radio groups remain deferred. Live checkedness and activation are covered by
  the preceding radio-validity slice.
- Constraint-validation IDL, submit-time validation, custom validity, and
  form/fieldset validity aggregation remain separate work.
- The state snapshot bounds cascade work to changed group members and compiled
  rules. Replacing that snapshot with a maintained group index is deferred
  until measurement shows tree scanning is material.

## Validation status

Per task direction, no build, native test, WPT/browser run, benchmark,
computer-use check, scene/pixel check, product check, package gate, or CI job
was run. Only `git diff --check` is used for this commit. The contract remains a
candidate until direct Chromium and native execution establish parity.
