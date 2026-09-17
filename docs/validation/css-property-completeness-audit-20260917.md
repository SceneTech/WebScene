# CSS property completeness audit and cold-template guard — 2026-09-17

## Scope

This stage follows the generated effective-property, identity, and grammar work
merged in PRs #309, #313, #316, and #317. It mechanically closes the remaining
cross-runtime drift that can be decided from existing behavior, and makes the
remaining semantic boundary explicit rather than inferring it from independent
switches.

The shared catalog now describes:

- 230 CSSOM-exposed canonical names shared by managed and native runtimes;
- 144 typed native property IDs and 201 accepted typed spellings/aliases;
- 54 deliberately storage-only CSSOM names;
- 120 typed IDs with modeled effective-property masks;
- 24 typed IDs explicitly classified as maskless; and
- 20 properties that inherit by default in the current native runtime.

The audit does not add cascade or initial-value semantics. Issue #238 owns the
browser/native winner matrix, global keywords, custom-property resolution, and
the remaining initial/computed-value behavior. Specified-value binary
serialization remains exhaustively covered for all 144 typed IDs by the #317
round-trip matrix. Computed CSSOM serialization remains property-specific and
must be consumed from #238's browser-referenced outcome before the parent epic
can close.

## Drift found and fixed

The generator initially rejected 16 canonical typed properties because their
parser IDs existed but the independent shared CSSOM exposure list omitted them:

- `border-block`, `border-inline`;
- `contain-intrinsic-size`, `container`, `container-name`, `container-type`,
  `content-visibility`;
- `grid-auto-columns`, `grid-auto-flow`;
- `list-style`, `list-style-position`, `list-style-type`;
- `scrollbar-color`, `scrollbar-width`, `text-anchor`; and
- `-webkit-font-smoothing` (the canonical spelling; the compatibility IDL-style
  spelling remains exposed too).

They are now exposed through the same generated CSS/IDL table used by both
runtimes. The catalog loader rejects any future canonical typed property that
is not CSSOM-exposed. The supported-name count moves from 214 to 230 and its
ordered SHA-256 is
`942b33d7fac2d56ecd448b0d5a5a3d2f34dee6f65cd471251fde7473c1494dd2`.

`cssom-inline-declaration-validity.html` has a sixth regression subtest that
assigns and reads all 16 names through named CSS and IDL accessors. It passes
6/6 with both native parser configurations. The new sixth subtest was not run
in Chrome on this host: the available in-app browser rejected local-file
navigation and no Chrome automation surface was available. The earlier five
subtests retain their prior Chrome result; this report does not claim a Chrome
result for the new coverage.

## Effective masks and inheritance

The generated identity table now carries an O(1) modeled-mask classification.
The current header test walks the first canonical spelling for every typed ID
and requires exact agreement with `property_mask()`. The 24 intentional
maskless IDs are:

`content`, `vertical_align`, `grid_auto_columns`, `grid_auto_flow`,
`table_layout`, `border_inline`, `border_block`, `outline`, `outline_width`,
`outline_color`, `animation`, `animation_name`, `animation_duration`,
`animation_delay`, `animation_timing_function`, `animation_iteration_count`,
`background_repeat`, `background_position`, `background_size`, `font`,
`text_transform`, `list_style`, `list_style_position`, and `list_style_type`.

Their ordered ID SHA-256 is
`ba22ffe33da3f7535acb7993d24b1b79232089eaf50b2782d47d1d12ccc4b37c`.
Every entry in the 61-row effective-property metadata table is additionally
required to have modeled storage and CSSOM exposure.

The previous handwritten descendant-propagation list is replaced by a sorted
generated 20-property inheritance catalog. This is a behavior-preserving
centralization of the runtime's current classification, not a claim that #238's
global-keyword and computed inheritance matrix is complete.

## Cold `CSSStyleDeclaration` template guard

The generator derives the exact startup work from the CSS/IDL table. The 230
properties require 418 native property accessor installations: one per IDL
name and a second when the CSS spelling differs. `cssText` and the four methods
are deliberately outside that generated count.

Certification builds time only construction of the complete style object
template, including `cssText`, generated property accessors, methods, and the
template reset. Diagnostics report the generated property count, the actual
installed accessor count, and elapsed nanoseconds. The focused test creates
seven fresh engines, verifies the exact 230/418 counts for every engine, and
bounds the seven-sample median to 5 ms.

Five independent focused runs on the Release AppleClang build produced these
seven-sample medians in microseconds:

`54.333, 61.667, 64.500, 45.875, 56.542`

The median of run medians is 56.542 microseconds. The largest individual sample
reported across those runs was 117.750 microseconds. The 5 ms guard therefore
allows substantial host noise while still rejecting order-of-magnitude startup
work growth. It is a template-installation guard, not a full engine-startup or
Spotify frame-time benchmark.

## Validation

- generator unit tests: 14/14; generated-output `--check`: pass;
- current and legacy native engine builds: pass;
- current and retained-header identity executables: pass in both build trees;
- focused `css-style-template-install`: pass in both build trees;
- five repeated current-parser startup-guard runs: pass;
- `cssom-inline-declaration-validity`: 6/6 current parser and 6/6 legacy parser;
- `WebScene.Css.Tests`: 383/383 on net8.0 and 383/383 on net10.0;
- selected NativeWeb CSS service, shared-style, and compiler CTests: 3/3;
- `git diff --check`: pass.

The native WPT result files are:

- `/tmp/css-completeness-wpt-native-current-final-20260917/results.json`;
- `/tmp/css-completeness-wpt-native-legacy-final-20260917/results.json`.

## Remaining parent-goal boundary

This completes the cross-runtime exposure/identity/mask/inheritance audit and
the cold-template guard. The full CSS optimization plan remains open until:

1. #238's cascade, global-keyword, custom-property, initial-value, and computed
   serialization outcome is integrated without duplicating its owner;
2. a fresh SDK is assembled from the combined result; and
3. the unchanged Spotify catalog passes continuous physical resize and the
   small-to-large media-query transition against the same Chrome build, with
   bounded CSS/layout work and no throttling, stale-frame stretching, deferred
   resize, or site-specific CSS.
