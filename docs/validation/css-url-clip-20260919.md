# Same-document SVG URL clip — implementation checkpoint

Date: 2026-09-19

## Scope

The consumer slice of issue #504 resolves only `clip-path: url(#fragment)` in
the current DOM tree. The fragment must identify a connected `clipPath` in the
same document and shadow scope. Quoted fragments are accepted; external URLs,
empty IDs, controls, whitespace, transforms, nested groups, uses, and unknown
geometry fail closed.

The native scene compiler flattens a bounded set of direct `path`, `rect`,
`circle`, `ellipse`, and `polygon` children into the existing kind-12 SVG path
resource. It caps IDs at 256 bytes, children at 64, polygon coordinates at 256,
individual authored path data and the final flattened path at 16 KiB. Mixed or
unsupported clip rules fail closed.

Kind-12 bit 28 identifies `objectBoundingBox` units. Skia and Flutter translate
the retained path to the target border-box origin and scale normalized geometry
by its width and height. User-space geometry uses the existing relative path
projection. The low 28 flag bits remain the scene-string index, and visual
equality includes every high metadata bit plus flattened resource text.

## Authored regression coverage

- The native retained-scene fixture creates a connected object-bounding-box
  clip definition and asserts the flattened resource and ABI flags.
- The Skia backend fixture proves normalized half-width geometry scales and
  translates into the target reference box.
- The browser contract preserves the same-document fragment computed value.

These tests were authored but not executed under the current implementation
throughput directive.

## Explicit boundary

External-document and network clip resources, transforms, rounded SVG rects,
nested groups, `use`, text, strokes, animation, and mixed per-child fill rules
remain unsupported and fail closed. Add those only as separately bounded
provider/consumer work; do not broaden this fragment resolver implicitly.

## Required release gates

Before release promotion, run focused native/backend tests, malformed and
maximum-size cases, fragment replacement/removal/reparenting and shadow-scope
cases, Skia/Flutter pixel comparisons against the same Chromium fixture,
4,096-reference publication and memory benchmarks, detach/reattach cleanup,
the exact SDK/CLI package, and relevant CI from the final merged heads.
