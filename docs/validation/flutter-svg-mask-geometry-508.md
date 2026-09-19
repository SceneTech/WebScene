# Flutter SVG mask geometry parity — implementation checkpoint

Issue #508 replaces the Flutter mask-specific regular-expression path extractor
with a reusable synchronous SVG mask resource. Each accepted document is
compiled once to an immutable `ui.Picture`; repeated CSS mask tiles draw that
picture without reparsing markup.

The compiler accepts bounded paths, rectangles, circles, ellipses, polygons,
polylines, lines, groups, affine transforms, inherited presentation attributes,
inline styles, simple type/class/id style rules, even-odd and nonzero fills,
group/fill/stroke opacity, and solid strokes. Local fragment `defs`/`use`
expansion is cycle checked. External references, unsupported elements, paint
servers, dashed strokes, malformed values, and unsupported style selectors fail
closed.

The resource boundary caps UTF-8 markup at 1 MiB, elements at 4,096, nesting at
64, path commands at 65,536, and `use` expansions at 4,096. The LRU owns at most
256 successful or failed entries and 64 MiB of estimated markup/recording
storage. Scene reset and projector disposal release every cached picture.

Mask placement now applies the root `preserveAspectRatio` value with the
provider viewBox, including nonzero origins, `none`, `meet`, `slice`, and all
min/mid/max alignments. Existing CSS repeat, position, intrinsic ratio,
percentage/pixel/calc size, and 4,096-tile limits are applied to the compiled
resource.

`svg_mask_resource_test.dart` authors pixel checks for group/use/transform,
inherited opacity, even-odd holes, strokes, repeat/position/size, viewBox origin,
and aspect-ratio alignment. It also supplies cycle/depth/byte/command failure
cases, cache-hit and parse-time counters, a 4,096-resource bounded workload,
and 100 release/rebuild cycles.

Per the implementation-first direction, builds, formatters, and tests were not
run. Only `git diff --check` was used for mechanical validation; runtime pixels,
performance, memory, lifecycle, complete CI, and Release qualification remain
deferred and are not claimed here.
