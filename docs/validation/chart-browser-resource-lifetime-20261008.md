# Browser resource ownership on release/1.0.36

Target: `release/1.0.36`, base `5b1839345541d534288cdfc20c0817ad7381a08c`.
Branch: `release/1.0.36` (maintenance release 1.0.37). This is a local candidate, not a
published runtime package or Windows qualification.

## Cause and change

StackWich #1504 primarily reproduces a TradingView JavaScript leak in both
Chromium and WebScene. Separate minimal WebScene tests also demonstrated native
registries rooting otherwise unreachable callbacks and browser objects. On the
packaged 1.0.36 runtime, 128 payloads of 131,072 array elements stayed alive after
disconnected/unobserved ResizeObservers, unused MediaQueryLists, and a detached
DOM target/callback cycle. Each case retained approximately 67 MB after GC.

The DOM listener ownership portion of main's `d7e6efc1` is adapted here; unrelated
CSS/text work is excluded. DOM wrappers own listener entries through a private
map. Native dispatch handles are weak. Remove, once, abort, detached-subtree and
frame retirement clear the JS entry as well as native handles.

A ResizeObserver wrapper owns its callback and is rooted while it observes
nodes. Disconnected reachable wrappers remain reusable. Methods hold their owner,
so extracting a method cannot outlive its native state. Context/callback indexes
are weak. MediaQueryLists use weak wrapper/context indexes until a change listener
or `onchange` requires active ownership. Removing the last registration restores
weak ownership. Dead records are retired on subsequent allocation or low-memory
collection, and capacities are compacted after collection.

Dispatch snapshots retain both native state and the V8 wrapper. Removing a frame
from its own callback invalidates private native pointers, retires its resources,
and leaves in-progress dispatch safe. MQL removal uses tombstones during dispatch
so reentrant removals do not invalidate indexes or skip remaining listeners.

## Validation

`browser-resource-lifetime` covers:

* Three rounds of 128 resources per type: disconnect, last-target unobserve,
  unused media query, legacy listener removal, cleared onchange, detached cycles.
  Every payload WeakRef clears in a later job after collection.
* 128 callbacks on a reachable detached target survive GC and reattachment.
  Explicit removal, once delivery and AbortController then release every callback
  while the target remains connected.
* Active observers, modern MQL listeners and onchange survive GC without an
  application reference; subsequent resize delivers callbacks.
* Observer callback identity, disconnect/reobserve, extracted method lifetime.
* Reentrant listener removal preserves delivery order.
* A hydrated iframe removes itself from an MQL callback while 32 observer/query
  payloads are registered; all payloads are subsequently collectible.

The native regression passes with certification enabled and in the production
flavor. Adjacent `detached-dom-gc`, `media-query-list`,
`resize-observer-retina-export`, and `event-listener-options` groups pass on the
final production build. `media-query-reentrant` passed in the certification build.

The six independent .NET resource probes pass on the final production runtime:
zero live payloads, approximately 0.34 MB settled heap, including both controls.
The real TradingView fixture, using the separately patched hosted assets, passes
6,000 rolling order updates on this production build: 50 active models, zero
retained destroyed models in the chart delegates, fixed listener counts, no
outstanding interop work. Post-warmup heap mean change is 114,592 bytes, within the
256 KiB gate; all post-warmup samples span 165,580 bytes. This does not establish
process RSS, GPU memory, all browser APIs or Windows behavior.

## Local build

Built on macOS arm64 with V8 **15.3.10**, pointer compression/shared cage,
PartitionAlloc, generated DOM bindings, html5ever/cssparser/Servo, bootstrap
snapshot and Inspector. The final application probe uses certification **OFF**.
The V8 cache is read only; no prebuilt SDK was replaced.

```sh
cmake -S experiments/WebScene.NativeEngine.Probe -B artifacts/issue-1504-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_SYSROOT=/Volumes/SSD/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk \
  '-DCMAKE_EXE_LINKER_FLAGS=-framework Security -framework CoreFoundation' \
  -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8=ON \
  -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8_INSPECTOR=ON \
  -DWEBSCENE_NATIVE_ENGINE_CERTIFICATION=OFF \
  -DWEBSCENE_V8_POINTER_COMPRESSION=ON \
  -DWEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE=ON \
  -DWEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT=ON \
  -DWEBSCENE_V8_PARTITION_ALLOC=ON \
  -DWEBSCENE_NATIVE_ENGINE_DENSE_LINK=ON \
  -DWEBSCENE_V8_ROOT=/Volumes/SSD/repos/HtmlML/artifacts/native-engine-v8-15.3/osx-arm64/v8 \
  -DWEBSCENE_V8_OUTPUT_ROOT=/Volumes/SSD/repos/HtmlML/artifacts/native-engine-v8-15.3/osx-arm64/v8/out/arm64/ReleasePartitionAllocClean
cmake --build artifacts/issue-1504-build -j 6 --target webscene_native_engine_tests
cp /Volumes/SSD/repos/HtmlML/artifacts/native-engine-v8-15.3/osx-arm64/v8/out/arm64/ReleasePartitionAllocClean/icudtl.dat \
  artifacts/issue-1504-build/icudtl.dat
WEBSCENE_NATIVE_ENGINE_TEST_FILTER=browser-resource-lifetime \
  artifacts/issue-1504-build/webscene_native_engine_tests
```

The SDK path avoids an installed command-line SDK/linker mismatch. The framework
flags are required by this cached PartitionAlloc snapshot-builder configuration.
For other hosts use their matching SDK and pinned V8 build. Keep the dylib, ICU
data and matching bootstrap blob/metadata together.

Before release: run the native regression and the actual StackWich SimExchange
workload on Windows, qualify packaging/signing and repeat application close/reopen,
account/symbol changes, position/bracket editing and multiple-chart memory tests.
Native browser ownership fixes do not replace the TradingView vendor corrections.
