# URL-backed CSS SVG masks — implementation checkpoint

Date: 2026-09-19

## Scope

Issue #500 adds a focused consumer on top of the mask-shorthand provider in
#499 / PR #501. It keeps unchanged Code OSS `mask` and `-webkit-mask` icon
assets inside the existing native CSS resource policy and retained scene.

The native engine loads an allowed SVG resource through the same host-backed
loader used by CSS backgrounds. A successful load publishes one versioned
`webscene-mask-svg-v1` string containing the view box, repeat, position, size,
and immutable SVG markup. Kind 30 opens the existing isolated effect group and
kind 47 applies the mask before the group closes. Failed or unsupported mask
resources publish `webscene-mask-invalid-v1`, which clears the isolated group
instead of exposing the unmasked foreground.

The Skia projection reuses `SharedSvgPictureCache`, paints every tile into one
bounded destination-in layer, and supports the existing SVG renderer. The
Flutter projection has no asynchronous SVG decoder on this command path, so it
uses a deliberately bounded synchronous geometry cache for untransformed,
filled path, rect, circle, ellipse, and polygon elements. Unsupported groups,
definitions, uses, images, text, line/polyline, strokes, transforms, and
nonzero-incompatible fill rules fail closed.

## Authored regression coverage

- The native retained-scene fixture loads one policy-approved SVG and asserts
  a kind-47 `webscene-mask-svg-v1` command with exact geometry longhands.
- The Skia backend fixture repeats a half-filled 4 by 4 SVG across a 12 by 4
  foreground and asserts the three retained islands plus transparent gaps.
- Existing shorthand and computed-value contracts cover standard/WebKit alias
  expansion, mutation, omitted-component reset, and retained longhand values.

These tests were authored but not executed under the current implementation
throughput directive.

## Explicit boundary

This slice does not claim raster masks, multiple mask layers, luminance mode,
non-add composites, SVG stroke/group/transform parity in Flutter, or controlled
ServiceWorker-backed asynchronous mask resource replacement. Those forms fail
closed or remain separately scheduled under #256.

## Required release gates

Before release promotion, run the focused native and backend tests, the full
browser/native contract set, Skia/Flutter pixel comparison against the same
Chromium fixture, resource-policy and mutation tests, 4,096-icon decode/cache
and scene-publication benchmarks, 100 replacement/detach cycles with memory
return-to-baseline checks, exact SDK/CLI packaging, and relevant CI jobs from
the final merged heads.
