# CSS animation transform-component source validation

Issue #675 extends WebScene's retained eight-track CSS animation runtime under
#242 with bounded two-dimensional translate and scale keyframe components. A
valid supported `transform` keyframe compiles once into one replace-composed
tuple: x/y translation, x/y scale, and rotation. Animation-list order remains
the composition order, so the last contributing track replaces the whole
supported transform tuple deterministically.

## Grammar and retained representation

The accepted keyframe grammar is `none` or a list containing at most five
non-duplicated `translate`, `translateX`, `translateY`, `scale`, `scaleX`,
`scaleY`, and `rotate` functions. Translation accepts finite `px`, percentage,
or unitless-zero operands. Scale accepts finite numbers. Rotation retains the
existing finite zero, degree, radian, and turn forms. Commas or CSS whitespace
separate the two-argument translate and scale forms. Unsupported functions,
duplicate components, malformed operands, nested functions, non-finite values,
and transform text over 1,024 bytes do not create a transform animation track.

Each valid declaration emits aligned translation, scale, and rotation stops;
an omitted component uses its identity. This preserves CSS replace composition
inside a track and between the runtime's maximum eight coordinated tracks.
Stops, timing metadata, and signatures are retained during cascade. Sampling
does no string parsing, selector matching, cascade, or visual-tree mutation.

## Timeline, geometry, and lifecycle

Translate and scale consume the same resolved progress as opacity, rotation,
and filters, including delay, negative delay, direction, finite, fractional,
infinite, and zero iteration counts, fill, pause/resume, and zero duration.
Translation uses the retained linear pixel/percentage representation and enters
the existing transform geometry invalidation path. Scale and rotation remain
paint-only. The scene, hit testing, computed transform matrix, and client
geometry all consume the same sampled tuple.

Identity mutation resets only the changed track. Removal, detach, navigation,
and teardown discard its retained tuple. A completed filled tuple stays static
without host-frame demand. At most eight tracks and their bounded stop vectors
are scanned per host frame; there is no per-frame parsing, cascade, allocation
of transform stops, or visual-tree reconstruction.

## Evidence and deferred scope

`test_transform_keyframes_compose_retained_geometry_and_paint` covers pixel and
percentage translation, one- and two-axis scale, rotation composition, a
paused negative-delay midpoint, list-order replacement, reverse zero-time fill,
computed matrices, client geometry, retained scene scale/rotation commands,
identity mutation, removal, detach, and settled frame demand. The dedicated
browser reftest is a WPT-aligned midpoint pixel and geometry oracle against an
equivalent static transform.

Arbitrary ordered transform-list matrix multiplication, 3D transforms, nested
`calc()` or custom-property operands inside keyframes, additive animation
composition, individual `translate`/`scale` properties, more than eight live
tracks, expanded animation events, and Web Animations API objects remain
deferred. Under the fast implementation policy, only `git diff --check` is
executed. Native, browser, package, and unchanged Code OSS acceptance remain
unexecuted.
