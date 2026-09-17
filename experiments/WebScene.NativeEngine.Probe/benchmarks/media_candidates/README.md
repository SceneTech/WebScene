# Deterministic media-candidate workload

This is a public-ABI, network-free engine benchmark. It generates identical DOM
and CSS bytes in control/candidate binaries, warms four 480/800px transitions,
then prints one JSON record per measured transition. Every record validates all
target/noise computed widths, heights and geometry, and includes an ordered
geometry checksum. Compare checksums by viewport before interpreting timing.

Build the same source against each installed **production** WebScene Runtime
SDK using `CMAKE_PREFIX_PATH`, the SDK's qualified compiler, architecture,
deployment target and system SDK. This directory is a standalone CMake project;
it does not need an AppScene checkout or certification instrumentation.

```
cmake -S experiments/WebScene.NativeEngine.Probe/benchmarks/media_candidates \
  -B /absolute/path/to/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/sdk [qualified toolchain options]
cmake --build /absolute/path/to/build
/absolute/path/to/build/webscene_media_candidates 8 1024 1024 32
```

Arguments: affected elements, unrelated elements, additional changed media
rules, measured transitions, optional `dense`. Normal mode makes the extra
rules target absent classes. Dense mode makes every extra rule match the
affected elements; use zero unrelated elements to expose indexing overhead.
Counts are bounded to 4,096 and iterations to 1,000.

Run serial A/B/B/A only after local builds/tests finish. Record engine archive
and source hashes, arguments, raw records and host contention. `barrierMs`
includes the public-ABI geometry/checksum query; `dispatchMs` is the engine's
resize dispatch interval. Neither measures native-window presentation, images,
authored resize listeners, live Spotify, or Chrome. This fixture complements,
not replaces, those acceptance gates. Dense/universal selectors and document
traversal remain real work; reduced keyed selector checks are not zero work.
