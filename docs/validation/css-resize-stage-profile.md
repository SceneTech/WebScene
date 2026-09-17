# CSS resize stage profile — 2026-09-17

Engine and original benchmark baseline: `66af2789`. Production, macOS ARM64,
Apple Clang test-library build. This is diagnostic evidence, **not** completion of
the original smooth Spotify resize / Chrome-parity acceptance.

## Findings

The [retained machine-readable reports](evidence/css-resize-stages-20260917.json)
cover separate processes, not an A/B optimization comparison:

| Workload | Applied resizes | Layouts per resize | Mean native dispatch | Host observation |
| --- | ---: | ---: | ---: | --- |
| Generic grid/flex/text, 5 seconds, 60 Hz | 300 | 3 | 2.670 ms | Headless timer ~30 Hz; 150 rendered scenes |
| Same fixture, composition + phase tracing, 2 seconds | 120 | 2.975 | 2.384 ms | 63 draw callbacks; tracing changes timing |
| Live Spotify, composition + phase tracing, 5 seconds | 246 of 300 inputs | 1 | 1.828 ms | 84 draw callbacks; last retained draw 18.444 ms |

These are individual diagnostic observations, not statistically established
improvements. The Spotify page was loaded through the Avalonia resource/renderer
path, **not the AppScene native demo**. Live content equivalence was not established.
The final-frame draw value is not a p95 or an aggregate stage attribution. It is a
reason to profile the host/paint path, not to declare it the universal bottleneck.

Phase traces include startup and warmup. Generic-fixture tree layout accounts for
590.104 ms of 622.334 ms in the five named phases (539 passes), compared with
12.460 ms sticky positioning, 9.155 ms paint ordering and 10.602 ms canvas retention.
Do not combine these trace totals with only the measured interval's resize counts.

A separate 15-second composition run was sampled for seven seconds using macOS
`sample` after warmup. Native hot leaf symbols include `compute_intrinsic_size`,
`resolve_length`, and text-metric lookup. Main-thread cadence spin-wait also appears
prominently. Summed samples across sleeping threads are not CPU percentages.
The current intrinsic cache is pass-local and available-size keyed; further reuse
requires dependency/lifetime proof, not a viewport-only cache.

## Invalid comparison discovered and corrected

The native v2 driver used a sawtooth (`index % span`), while Chrome v1 used a
triangle. Native reports omitted source identity and requested bounds, and the
comparator accepted two missing options as equal. The README described a real
window despite `BenchmarkApp` selecting Avalonia.Headless. Missing certification-only
stage telemetry appeared as zero. None of this is evidence of physical presentation.

Native v3 / Chrome v2 now record the triangle waveform and requested bounds. Native
reports identify the local fixture by SHA-256 or the supplied URL, and paired native
comparisons reject missing/different workload identities, dimensions and host modes.
Production stage counters unavailable at compile time are null. The former
`--enforce-chrome-reference` flag now rejects qualification: informational callback
comparisons explicitly say viewport/content equivalence has not been verified.
Even matching requested bounds does not equate Chrome's outer window with Avalonia's
headless content viewport. Warmup behavior and live content also require qualification.

Validation: three focused .NET workload tests; three Python comparator tests
(including 18 missing/mismatched metadata cases); benchmark build; Chrome script
syntax check; native v3 fixture smoke; Chrome v2 live-page smoke; native consumption
of that v2 reference. These validate the harness changes, not browser performance.

## Native AppScene attempt: rejected before performance qualification

The September 17 native attempt used WebScene `5e9b0d13`, AppScene main `9f434e0`
and SpotifyCatalog sample `8576fb6f`. The fresh SDK built with the pinned LLVM
22.1.8 toolchain and passed `qualify_macos_profile.py`, including the independent
consumer build/run and system-runtime closure check. Existing V8/Dawn packages
were reused; this is not qualification of all SDK release gates or macOS 26.0.

The native process completed its 40-second probe, but **the workload was invalid**:
Spotify client `1.3.2.203.geee177de` reached `document.readyState === 'complete'`
with 80 DOM elements and **zero catalog cards, links or images**. The logged
failures included `NodeList is not defined` in the cookie banner and a rejected
Spotify `fetchCurrentSession` promise. Neither diagnostic alone establishes the
root cause. A subsequent manual launch also displayed a blank window.

The programmatic window driver separately failed with `AXError` before completing
its resize sequence. Therefore neither the process's successful exit, its Metal
presentation trace, nor its CPU sample qualifies Spotify resize performance.
These failed acceptance attempts must not be combined with the earlier populated
Avalonia workload or used as an optimization baseline. The next native measurement
must first establish populated catalog content and a completed resize workload.

## Next acceptance steps

1. Rebuild the actual AppScene Spotify demo using the qualified compiler and record
   exact engine/host identities. At this profile's start the local compiler was
   LLVM 22.1.1, whereas the source SDK requires an exact LLVM 22.1.8 binary/config/
   header closure; its build correctly rejected the old compiler.
   The original 22.1.8 bottle was subsequently installed side-by-side on the SSD;
   compiler/configuration/header hashes and a system-runtime C++20 smoke passed.
   A custom-root propagation bug in the macOS toolchain was fixed with a regression:
   the full SDK now configures through C/C++/Objective-C++ ABI checks using that root.
   SDK build/consumer qualification subsequently passed as described above;
   native-demo content and presentation qualification did not.
2. Qualify equal content and CSS viewport sequences in AppScene and headed Chrome.
   Separate event scheduling, forced layout, final layout, scene generation,
   retained rendering and actual presentation. Include continuous real live-resize,
   not only programmatic window changes.
3. Reduce the dominant proven engine-owned cost to a product-neutral workload.
   Coordinate layout/cache changes with #240 and profiling with #243. Do not create
   a second invalidation implementation or optimize Avalonia-specific draw costs
   under a claim about the AppScene demo.
4. Retain a candidate only after semantic/geometry/paint regressions and repeated
   A/B or ABBA evidence. No stretched frames or delayed-layout resize are allowed.
