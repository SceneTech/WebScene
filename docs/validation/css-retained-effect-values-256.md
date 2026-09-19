# CSS retained-effect value qualification

This gate covers the first product-neutral slice of issue #256:

- syntax validation and computed values for `mask-image`, `mask-size`,
  `mask-position`, `mask-repeat`, and `mask-composite`;
- syntax validation and computed values for `clip-path`, `filter`, and
  `backdrop-filter`;
- invalid CSSOM write retention and class-mutation refresh; and
- bounded allocation, mutation, and cleanup across 4,096 styled descendants.

Retained-scene mask, clip, filter, and backdrop painting is outside this slice
and remains separately qualified by later issue #256 work.

## Radial and multiple mask extension — 19 September 2026

Issue #505 adds circle/ellipse radial gradients and up to sixteen ordered mask
layers. The `webscene-mask-v2` ABI uses UTF-8 byte-length-prefixed fields for
each layer's image, repeat, position, size, mode, view box, and SVG markup.
Linear, radial, and policy-loaded SVG `add` layers are unioned on one temporary
surface bounded to the element paint box, then destination-in is applied once.
Unsupported image functions, excessive layers, malformed resources, luminance
mode, and non-add composites fail closed; #506 owns the remaining composites.

Native, portable WPT, Avalonia, and Flutter regression contracts are authored
but have not been executed under the current fast-merge directive. No build,
pixel, performance, memory, lifecycle, or package result is claimed.

## Backdrop command extension — 19 September 2026

Issue #503 extends this contract with a bounded kind-48 retained command. The
resource preserves authored `blur()`/`saturate()` order, the command carries
rounded output bounds and maximum blur sigma, and localized damage includes a
backdrop when earlier sampled content changes. The producer forces these boxes
into the foreground phase so reference presenters execute the command after
retained canvas/GPU layers and before the element background.

The native regression now authors 4,096 backdrop commands, the WebKit alias,
the Code OSS `blur(8px) saturate(1.08)` sequence, phase metadata, and resource
bounds. Avalonia 12 records Skia backdrop save layers; Flutter replays bounded
prior content through nested filters. AppScene #174 owns the packaged native
Skia/Graphite consumer.

These additions have not been executed under the current fast-merge directive.
Build, WPT, pixels, runtime, performance, memory, and package results remain
deferred and must not be inferred from the historical results below.

## Direct gates

| Gate | Denominator | Result |
| --- | ---: | --- |
| Native WPT profile | 1 document / 10 subtests | 1/1 and 10/10 passed |
| Chrome 153 oracle | 1 document / 10 subtests | 1/1 and 10/10 passed |
| Native effect-value lifecycle/performance | 4,096 descendants / 100 cycles / 200 states | passed in 11,171.1 ms; 4,098..4,102 fixture-node bound; at most 1 retained node |
| Portable CSS tests | 377 tests / 2 target frameworks | 377/377 on net8.0 and 377/377 on net10.0 |
| Portable native build | 42 build steps | 42/42 passed |
| V8 dedicated native target | 1 target / 1 CTest | 1/1 built and 1/1 passed |

The native gate has a 15,000 ms ceiling, requires zero host animation-frame
demand after the final settled state, waits for a completed low-memory
notification, and limits post-cleanup V8 heap growth to 32 MiB. Its peak
effect fixture adds 4,099 accounted textual-style records and 7,727,735 bytes,
within a 32 MiB textual-style growth bound. The two retained runner reports
are bounded to 64 KiB each; their combined observed size is 5,149 bytes.

Machine-readable results and report digests are recorded in
`docs/validation/evidence/css-retained-effect-values-256-20260917.json`.
