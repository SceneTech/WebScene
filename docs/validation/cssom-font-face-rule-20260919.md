# CSS font-face rule source validation

## Scope

This focused #239 slice is based on exact WebScene main
`7af9ec134a4d521690e5a0cff83178fec2a51890`. The audit and implementation
started at `2b64b08965aeba5ecbd33fcc0bd1beaaceaf6243`, then rebased cleanly after
the required-checkbox validity merge in PR #574. At the final base the native parser
already retains `@font-face` and the Avalonia resource bridge synchronously
consumes `font-family`, the first `src` URL, `font-weight`, and `font-style`.
Stylesheet consumption also clears the document font-measurement cache. The
CSSOM adapter previously exposed these native-supported rules as opaque
`CSSRule` objects.

The slice adds browser-shaped `CSSFontFaceRule` reflection with legacy type 5,
realm-local interface identity, stable `parentRule` / `parentStyleSheet`, and a
SameObject descriptor declaration whose assignment forwards to `cssText`.
Descriptor, named-property, `setProperty`, `removeProperty`, and complete
`cssText` changes serialize the containing rule on one line and use the existing
stylesheet publication path. Attached owner-backed changes therefore reach the
existing font registry, cascade, font-measurement invalidation, and layout
barrier synchronously. Imported changes bubble through their owning import;
constructed changes notify every adopter. Group deletion detaches nested font
rules recursively, and later detached descriptor mutation changes only the
retained rule snapshot.

This does not add `FontFace`, `FontFaceSet`, new descriptors, font formats,
transport behavior, or font matching rules. The descriptor object preserves
authored declarations for CSSOM serialization while the native host continues
to consume only its existing supported subset.

## Audit and contract sources

- CSS Fonts Level 4 `CSSFontFaceRule` / `CSSFontFaceDescriptors`, including
  `[SameObject, PutForwards=cssText]` and inheritance from `CSSRule`.
- Web Platform Tests revision
  `2c705104a295c48053eeddf7fe0170d790a4e853`,
  `css/cssom/CSSFontFaceRule.html`, whose focused assertion requires font-face
  rule serialization without newlines.
- The existing WebScene live style-rule, grouping-rule, import-rule,
  constructed/adopted-sheet, keyframes-rule, staged-publication, iframe-realm,
  variable-font, and late-font-registration contracts.

The repository audit found no open font-face or #239 implementation PR. The
only open repository PR was draft PR #76, the broader Code OSS compatibility
integration. Main already contained the focused container and keyframes slices
through PRs #568 and #570. No competing font-face branch was present on the
remote.

## Ownership, loading, and bounds

Each reflected rule adds one weak brand entry, one rule wrapper, and one live
descriptor map. Descriptor parsing and serialization are linear in the bounded
rule text. No polling, task queue, hidden DOM owner, duplicate native rule tree,
font byte buffer, or cross-realm strong registry is introduced.

The existing native boundaries remain authoritative:

- owner replacement staging remains capped at 1,024 pending owners and 32 MiB;
- adopted publication remains capped at 1,024 sheets and 32 MiB per root;
- imported loading remains capped at 1,024 loads, 16 nesting levels, and 32 MiB
  cumulative source;
- the Avalonia resource loader retains its URL resolution, request policy,
  security checks, cancellation behavior, cache, and font byte limits; and
- the host's existing source tuple de-duplication and process/document font
  registry lifetimes are unchanged.

Deleting or detaching a CSS rule stops later mutations from publishing. It does
not attempt to unload a font resource that the existing host registry already
accepted; this slice preserves that registry's established lifetime model.

## Authored regressions

- `tests/WebPlatformSubset/contracts/cssom-font-face-rule-mutation.html`
  covers owner-backed and nested rules in main and iframe realms, type/brand and
  parent identity, SameObject and forwarded declaration mutation, single-line
  serialization, real Roboto 400-to-700 registration and metric invalidation,
  imported ownership, constructed/adopted ownership, recursive detach, and
  detached isolation.
- `tests/WebPlatformSubset/webscene-cssom-font-face-rule-mutation-profile.json`
  pins the focused contract and WPT revision.
- `experiments/WebScene.NativeEngine.Probe/tests/native_v8_runtime_browser_dom_tests.inc`
  carries the corresponding native browser-DOM shape, mutation, publication,
  adopted ownership, realm, and detach regression beside the existing CSSOM
  contracts.

## Validation status

Per the task constraint, no build, browser run, native test, WPT profile,
benchmark, lifecycle loop, package gate, or CI job was run. Source review and
`git diff --check` are the only validation in this commit. Runtime correctness,
performance, memory, teardown, exact-package execution, and the authored font
metric differential remain explicit validation debt under #239.

After this slice, every at-rule family currently consumed by the native parser
has a typed CSSOM wrapper. The next #239 implementation gap is to audit
product-reachable opaque rules against current Code OSS input before selecting
another rule family; the known remaining work is the issue's cumulative
Chromium/native mutation, invalidation, performance, memory, teardown,
exact-package, and CI qualification.
