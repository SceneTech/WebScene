# CSS `path()` clip — implementation checkpoint

Date: 2026-09-19

## Scope

The provider slice of issue #504 projects CSS `clip-path: path(...)` through
the existing retained kind-12 clip command. It accepts one quoted SVG path,
an optional `evenodd` or `nonzero` fill rule, bounded CSS string escapes, and
a maximum decoded path length of 16,384 bytes. Path coordinates are relative
to the element border-box origin.

Kind 12 retains bit 31 for a string-backed SVG path, adds bit 30 for even-odd
fill and bit 29 for border-box-relative coordinates, and uses the low 29 bits
as the scene-string index. Skia and Flutter consume the same flags. Invalid or
unparseable SVG data produces an empty clip instead of exposing unclipped
content.

Scene equality now compares kind-12 resource text and its metadata flags. This
prevents fill-rule or coordinate-space changes with identical path text from
being mistaken for an unchanged visual, while allowing string-table indices to
change without unnecessary damage when the effective resource is identical.

## Authored regression coverage

- The browser contract preserves quoted path syntax and the even-odd rule.
- The Skia backend fixture verifies that even-odd clipping cuts a transparent
  inner region from a filled outer path.
- Existing retained scene contracts remain the integration point for kind-12
  resource identity and localized publication behavior.

These tests were authored but not executed under the current implementation
throughput directive.

## Explicit boundary

This provider does not resolve `url(#clip)` or external SVG clip resources.
The same-document URL consumer remains the second PR in #504. Arbitrary CSS
shape commands, geometry boxes outside the current border-box behavior, and
unbounded path/resource inputs remain unsupported or fail closed.

## Required release gates

Before release promotion, run the focused native/backend tests, WPT-derived
contracts, malformed and maximum-size inputs, fill-rule and mutation damage
cases, Skia/Flutter pixel comparisons against the same Chromium fixture,
4,096-path publication and memory benchmarks, repeated detach/reattach cleanup,
the exact SDK/CLI package, and the relevant CI jobs from the final merged heads.
