# CSS keyframes rule source validation

## Scope

This focused #239 slice starts from exact WebScene main
`c45cd36ce59c264206aace5d0d3f524e8ded675c`. The native stylesheet parser and
animation cascade already accept standard `@keyframes` and
`@-webkit-keyframes` definitions for opacity, rotate transforms, and bounded
foreground filters. The CSSOM compatibility adapter previously left both
native-supported rule forms opaque.

The slice adds live owner-backed, imported, and constructed
`CSSKeyframesRule` / `CSSKeyframeRule` wrappers. It does not broaden native
animation property support, timing behavior, CSS parser grammar, or vendor
prefixes beyond the two keyframes at-rules already recognized by native code.

## Contract sources

- CSS Animations Level 1 `CSSKeyframesRule` and `CSSKeyframeRule` interfaces,
  including type constants 7 and 8, name, key text, declaration, indexed getter,
  append, last-match lookup, and last-match deletion semantics.
- Web Platform Tests revision
  `2c705104a295c48053eeddf7fe0170d790a4e853`,
  `css/cssom/CSSKeyframesRule.html`, plus the same upstream suite's current
  focused indexed-getter, deletion-detach, invalid-append, and key-text
  regressions used to make the reduction explicit.
- Existing WebScene owner stylesheet, imported stylesheet, constructed/adopted
  stylesheet, staged publication, iframe-realm, and native host-clock animation
  contracts.

The authored reduction covers interface inheritance and realm constructors,
legacy rule types/constants, SameObject live `cssRules`, indexed access and
`length`, normalized percentage/from/to reflection, ordered selector-list
matching, duplicate last-match lookup/deletion, silently ignored invalid
`appendRule` input, Web IDL conversion errors, `keyText` `SyntaxError`,
SameObject mutable declarations, containing-rule serialization, reserved-name
quoting, synchronous animation-definition invalidation, recursive detachment,
and isolated detached mutation. The native-supported prefixed form retains its
`@-webkit-keyframes` serialization; no unsupported prefix is synthesized.

## Ownership and bounds

Keyframes reuse each stylesheet state's existing rule array and native staged
publication path. A reflected keyframes rule adds one weak brand entry, one live
rule-list proxy, and one wrapper plus declaration object per admitted keyframe.
Selector parsing performs one linear pass over the bounded selector text;
append, lookup, and deletion retain the interface's required linear behavior
over that rule's children. No polling, timer, hidden DOM owner, duplicate native
tree, task queue, or cross-realm strong registry is introduced.

Existing native boundaries remain authoritative: at most 1,024 pending owner
replacements totaling 32 MiB, at most 1,024 adopted sheets totaling 32 MiB per
publication, and imported-sheet limits of 1,024 loads, 16 nesting levels, and
32 MiB cumulative source. Each attached mutation performs the existing single
staged publication. Deleting a child or containing rule clears parent links
recursively; disconnected owner and adopter teardown retain no new native
animation authority.

## Authored regressions

- `tests/WebPlatformSubset/contracts/cssom-keyframes-rule-mutation.html`
  exercises standard and native-supported prefixed rules in main and iframe
  realms, constructed sheets, exact mutation behavior, live animation
  invalidation, serialization, and detached snapshots.
- `tests/WebPlatformSubset/webscene-cssom-keyframes-rule-mutation-profile.json`
  pins the focused contract and WPT revision.
- `experiments/WebScene.NativeEngine.Probe/tests/native_v8_runtime_browser_dom_tests.inc`
  carries the corresponding native browser-DOM regression beside existing
  stylesheet CSSOM coverage.

## Validation status

Per the task constraint, no build, browser run, native test, WPT profile,
benchmark, lifecycle loop, package gate, or CI job was run. Source review and
`git diff --check` are the only validation in this commit. Runtime correctness,
performance, memory, teardown, and exact-package execution remain explicit
validation debt under #239.
