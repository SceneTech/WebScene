# Build-time JavaScript compilation (Precompiled)

`Precompiled` packages V8 code-cache data produced by the same WebScene runtime
that executes the app. It is **not native AOT, a reduced V8 build, a security
sandbox or a promise of zero runtime compilation**. Ignition and the JIT remain.
The existing WebScene bootstrap snapshot stays enabled/disabled as configured.
No engine size or whole-app startup improvement is claimed without a matched
application benchmark.

## CMake usage

Use a newly built native SDK which exports `WebScene::JavaScriptCompiler`:

```cmake
find_package(WebScene REQUIRED CONFIG)
# Create your existing application target and native/runtime host as usual.
webscene_precompile_javascript(my_app
  SOURCES assets/app.js assets/vendor.js
  MODULES assets/module.mjs
  HTML assets/index.html
  DEPENDS frontend_build)
```

Declare each emitted JavaScript bundle/chunk or module, or use HTML discovery for
local script tags and inline scripts. Duplicate source/kind pairs are compiled
once. HTML discovery does not follow module imports: declare their emitted files
under MODULES. It rejects remote/absolute/escaping script URLs and `<base>` rather
than guessing their build-time contents. List the resolved local scripts directly
for those documents. Data scripts and import maps are not JavaScript inputs.

Run the existing TypeScript/JSX/bundler step **before** this step. Raw TS, TSX and
JSX inputs fail with a transpilation diagnostic. No TypeScript runtime or Node
runtime is added to the application. The SDK driver itself uses Python only at
build time; the native `webscene-jsc` uses WebScene's V8, never Node's V8.

Keep the original JS assets in the application. V8 code-cache APIs still consume
the original source and may need it for lazy functions, debugging and fallback
inside V8. This feature does not remove, encrypt or obscure source. Assets and
normal `<script>` / module loading remain unchanged.

The generated C++ source is added directly to the executable target (not hidden
inside an unreferenced static archive). Its static initializer registers the
embedded immutable caches before application startup. No new writable cache
directory is required. Existing normal runtime caches continue working.

## Compiler and loading contract

- The compiler creates a temporary isolate with **the runtime's initialization,
  flags and bootstrap snapshot**. It does not execute application scripts,
  instantiate modules, resolve imports or invoke top-level side effects.
- Classic scripts use V8's eager compile option. Modules retain the supported V8
  module compilation/lazy-function policy. Lazy functions and JIT optimization
  may still compile at runtime.
- The embedded descriptor contains source SHA-256, script kind, V8 version/cache
  tag, snapshot fingerprint, payload SHA-256 and cached data. Registration checks
  sizes and payload hashes, copies borrowed memory and bounds registry growth.
- Runtime lookup uses exact source bytes and classic/module kind, not an absolute
  build-machine URL. Moving the app does not invalidate matching caches.
- A matching cache with incompatible V8 flags/version/snapshot fails explicitly.
  A V8-rejected cache is **not executed as a source fallback**. V8's API may have
  compiled internally before reporting rejection; this is not a compiler-free
  execution guarantee.
- Non-declared or changed source follows the normal Full execution path. Dynamic
  `eval`, Function constructors and runtime-loaded source remain available. This
  is an optimization policy, not closed-world source enforcement.
- Registrations are process-lifetime immutable data, not shared isolates, V8
  objects or application state. Ordinary per-context isolation is preserved.

The build uses the target-native SDK compiler. Cross-compiling caches on a
mismatched host is rejected. SDK producer and application consumer must agree on
V8 flags/environment and snapshot settings; rebuild caches after SDK changes.
Compilers, SDK static libraries and compiler-side ICU/snapshot copies are not
application payloads. The application's ordinary V8/runtime assets are unchanged.

## Diagnostics and tests

`webscene_get_precompiled_javascript_stats_v1` exposes registered count/bytes,
accepted-cache count and rejection count. A zero hit count is not proof of a
failure when an existing isolate-local cache has already served that script.

The focused CTest target executes a precompiled native DOM counter with retained
closures and event listeners, and a separate process tests incompatible-cache
rejection. These run against the actual configured V8/WebScene engine.

For an inexpensive independent real-V8 smoke lane, the Node addon test compiles
the same compiler/consumer implementation against Node's installed V8 headers.
Separate producer/consumer processes prevent in-memory cache hits from faking
serialization coverage. This verifies classic/module code-cache consumption,
closures/classes/exceptions, encoding, relocation, compile-only behavior,
corruption/version/flags/snapshot rejection and generated C++ compilation. It is
**not a replacement for the pinned WebScene V8 15.3.10/SDK application lane**.

Python/CMake fixture tests cover HTML discovery, strict input validation,
dependency files, generated-output aggregation and incremental rebuilds. Native
engine/WPT regressions continue in the existing runtime CI; this PR does not
reclassify previously failing WPT cases as passing.
