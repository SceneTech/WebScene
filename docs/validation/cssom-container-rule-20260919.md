# CSSContainerRule source validation

## Scope

This slice completes the first opaque conditional group remaining in dynamic
CSSOM at WebScene base `b100d6f7c0f51ee9900c88332a17b7f40b109525`.
The native stylesheet parser and cascade already own bounded `@container`
evaluation. The compatibility layer previously preserved the authored rule text
but exposed no `CSSContainerRule`, condition fields, child rule list, parent
links, or nested mutation methods.

Pinned unchanged Code OSS recursively walks `CSSRule.cssRules` in
`src/vs/editor/browser/gpu/css/decorationCssRuleExtractor.ts` and ships
production `@container` groups. Treating those groups as opaque stops the walk
at a browser-standard conditional boundary.

## Contract sources

- CSS Conditional Rules Level 5, `CSSContainerRule` interface and readonly
  `conditionText`, `containerName`, `containerQuery`, and `conditions` fields.
- Web Platform Tests revision
  `2c705104a295c48053eeddf7fe0170d790a4e853`,
  `css/css-conditional/container-queries/container-rule-cssom.html`.
- Existing WebScene `CSSGroupingRule`, `CSSConditionRule`, staged stylesheet
  publication, constructed-sheet replacement, and iframe-realm contracts.

The authored browser contract covers interface inheritance, legacy `type === 0`,
SameObject live child lists, frozen single and multiple condition reflection,
readonly fields, child ownership, exact mutation exception names, synchronous
container-cascade invalidation, containing-media serialization, iframe realms,
and detached-rule mutation isolation.

## Ownership and bounds

The change reuses the existing stylesheet rule tree and staged native
publication. Each container rule adds one weak brand entry and one frozen array
whose entries correspond one-for-one with top-level authored conditions. Parsing
is a single linear scan over the already-admitted rule prelude. Mutation retains
the existing one-publication-per-operation behavior and does not add queues,
timers, hidden DOM owners, native rule copies, or cross-realm strong references.

Existing native boundaries remain authoritative: at most 1,024 pending owner
replacements totaling 32 MiB, and at most 1,024 adopted sheets totaling 32 MiB
per publication. Imported sheets retain their 1,024-load, 16-level, 32 MiB
cumulative limits. Deleting a rule clears its parent links; deleting or
disconnecting the owning tree leaves no new native state to retire.

## Authored regressions

- `tests/WebPlatformSubset/contracts/cssom-container-rule-mutation.html`
  exercises main and iframe realms, WPT condition reflection, exact errors,
  live insertion/deletion, media nesting, synchronous cascade changes, and
  detached mutation.
- `tests/WebPlatformSubset/webscene-cssom-container-rule-mutation-profile.json`
  pins the focused contract and WPT revision.
- `experiments/WebScene.NativeEngine.Probe/tests/native_v8_runtime_browser_dom_tests.inc`
  carries the corresponding native source regression beside the existing
  mutable stylesheet, supports, layer, imported, constructed, and adopted
  stylesheet coverage.

## Validation status

Per the current implementation-throughput direction, no build, browser run,
native test, WPT profile, benchmark, lifecycle loop, package gate, or CI job was
run. Source review and `git diff --check` are the only validation in this commit.
Runtime correctness, performance, memory, teardown, and exact-package execution
remain explicit validation debt under #239.
