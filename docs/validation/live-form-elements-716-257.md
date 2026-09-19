# Live form controls collection source contract

Issue: WebScene #716

Parent: WebScene #257

Base: `17bd90c88829b45d1e08721e07584af3110fdc4b`

## Implemented boundary

- `HTMLFormElement.elements` returns one branded
  `HTMLFormControlsCollection` per form wrapper. `length`, indexed access,
  `item()`, named properties, `namedItem()`, and supported property names read
  the same live tree-order membership index. The form's legacy indexed and
  named getters use that index as well.
- Membership covers listed button, fieldset, input other than image, object,
  output, select, textarea, and form-associated custom elements. The existing
  form-owner algorithm includes descendants and controls associated through
  `form=` in the same tree.
- Duplicate `id`/`name` lookup returns a branded live `RadioNodeList`. Its
  indexed access, iteration, and `value` getter/setter use current membership;
  the setter updates checkedness through the existing radio-group transition
  path. Empty and single matches retain browser null/node behavior.
- The existing #713 form-id and explicit-owner reverse indexes bootstrap the
  collection index once per document generation. Existing child-list,
  attribute, reparenting, and custom-element registration hooks then update
  only the changed subtree, explicit dependents, and affected owner vectors.
  Reads do not scan the document, match selectors, or rebuild ownership.
- `id` and `name` are read at named lookup time, so the runtime retains no
  separate named-result cache. Collection identity is held on its form wrapper;
  a `RadioNodeList` retains that wrapper while live. Native membership is
  bounded by retained DOM nodes, is reclaimed with detached native subtrees,
  and is cleared with reverse indexes on navigation and teardown.

## Runtime and performance contract

All ownership updates and collection reads run synchronously on the runtime
owner thread. Mutation sorting is limited to control vectors for affected
forms. The implementation schedules no polling, timer, animation frame,
stylesheet parse, selector match, cascade, or visual-tree work for collection
liveness. Select option collection behavior remains on its existing path.

## Authored coverage and deferred acceptance

`tests/WebPlatformSubset/contracts/dom-form-associated-collections.html` now
records same-object branding, live insertion/removal, external ownership and
tree order, form-id/form-attribute reassociation, indexed/named lookup,
duplicate `RadioNodeList.value`, supported property names, and custom-element
membership.

The authored browser contract is unexecuted under the fast implementation
policy. Upstream WPT execution, native builds, package checks, performance
measurements, and physical-platform qualification remain deferred. This slice
adds no host ABI, Code OSS change, browser dependency, or general collection
dependency engine.
