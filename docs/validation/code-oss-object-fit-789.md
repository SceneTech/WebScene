# Code OSS object-fit and object-position implementation (#789)

## Product trigger

Unchanged Code OSS uses `object-fit` in avatars, account imagery, thumbnails,
extension surfaces, and session UI. WebScene previously exposed neither
`object-fit` nor `object-position`, and every retained `<img>` stretched to its
layout box.

## Implemented slice

- Adds generated native and managed property identities plus CSSOM aliases.
- Supports `fill`, `contain`, `cover`, `none`, and `scale-down`.
- Supports one- and two-component positions with horizontal/vertical keywords,
  percentages, supported lengths, and existing math-function parsing.
- Serializes computed position keywords as browser-shaped percentages while
  preserving the authored inline declaration.
- Computes image destination geometry from intrinsic dimensions and the content
  box without changing layout geometry.
- Publishes an explicit content clip around retained raster and SVG image
  commands, including the inner rounded corners.
- Preserves the active rectangular clip when Flutter defers retained SVG
  rasterization.
- Treats CSSOM mutations as paint-only scene invalidations.

The bounded position parser deliberately leaves three- and four-component
edge-offset syntax outside this slice because it is not present in the current
Code OSS inventory.

## Authored gates

- `contracts/css-object-fit-position.html`: CSSOM defaults, cascade, mutation,
  removal, intrinsic dimensions, and unchanged element geometry.
- `test_image_elements_load_and_reach_scene`: exact `cover`/right-bottom and
  `contain`/center destination and clip commands.
- `test_percentage_radius_reaches_raster_image_scene_clip`: rounded content clip
  regression.
- `object-fit-performance`: 4,096 images, ten style transitions, zero added
  layout passes, and a five-second guardrail.

## Evidence state

Implementation and gates are authored from source review. Per the active fast
merge direction, no build, test, WPT, visual, package, or performance command
was executed for this change. Runtime, exact-pixel, and benchmark evidence is
therefore zero until the later full-validation pass.
