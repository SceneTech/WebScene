# WebScene native engine runtime

This package contains one reviewed native WebScene V8 DOM/CSS/scene engine for the RID in the
package ID. It is produced by the release pipeline, not restored as a template.

The package includes the native library, its required `icudtl.dat`, the bootstrap snapshot
and metadata sidecars, the applicable third-party notices, and a SHA-256/ABI/V8 build
manifest. Release packages use V8
pointer compression, its process-wide shared cage, and the size-optimized runtime
policy. Those settings and dense-link status are recorded in the manifest and exposed
as transitive MSBuild properties so a stale or incompatible V8 monolith cannot
silently enter a release.
The manifest also records the accepted `html5ever`, `cssparser`, Servo-selector,
generated-WebIDL, and bootstrap-snapshot selections. Schema version 2 hashes every native
and snapshot asset. Transitive build targets copy the snapshot beside the library for
both build and publish outputs and fail if any required asset is absent.
Release linkage also dead-strips unreachable native sections and restricts the
dynamic export table to WebScene's public C ABI. Developer builds retain ordinary
symbols unless `WEBSCENE_NATIVE_ENGINE_DENSE_LINK=ON` is selected explicitly.
When adding a `WEBSCENE_API` function, also update the macOS
`native/webscene_native_engine.exports` list. Architecture tests check the list
against the complete public header on every CI platform, and package consumer
tests resolve every declared function from each release binary. Export-list edits
are linker dependencies, so incremental dense builds relink after such changes.
Runtime packages compile with `WEBSCENE_NATIVE_ENGINE_CERTIFICATION=OFF`; feature
inventories, diagnostic snapshots, native profiling state, and their hot-path
counters are not shipped. Production packages do include the patched V8 Inspector
capability; the stable `webscene_engine_get_build_features` ABI reports only the
V8 Inspector bit for these package binaries.
The library locates ICU data relative to its own module, so the package remains
relocatable.
Browser-facing `WebSocket` support is implemented inside the native runtime
with the pinned IXWebSocket transport; it does not call back into a managed
network stack.
Applications must target the same `RuntimeIdentifier`; mixing runtime packages and
RIDs is rejected during the build.

Install the package matching the application's deployment RID:

```xml
<PackageReference Include="WebScene.NativeEngine.Runtime.osx-arm64" Version="VERSION" />
<!-- <PackageReference Include="WebScene.NativeEngine.Runtime.osx-x64" Version="VERSION" /> -->
<!-- <PackageReference Include="WebScene.NativeEngine.Runtime.linux-x64" Version="VERSION" /> -->
<!-- <PackageReference Include="WebScene.NativeEngine.Runtime.linux-arm64" Version="VERSION" /> -->
<!-- <PackageReference Include="WebScene.NativeEngine.Runtime.win-x64" Version="VERSION" /> -->
```

| Target platform | Runtime identifier | Package |
| --- | --- | --- |
| macOS on Apple silicon | `osx-arm64` | [`WebScene.NativeEngine.Runtime.osx-arm64`](https://www.nuget.org/packages/WebScene.NativeEngine.Runtime.osx-arm64/) |
| macOS on Intel | `osx-x64` | [`WebScene.NativeEngine.Runtime.osx-x64`](https://www.nuget.org/packages/WebScene.NativeEngine.Runtime.osx-x64/) |
| Linux x64 | `linux-x64` | [`WebScene.NativeEngine.Runtime.linux-x64`](https://www.nuget.org/packages/WebScene.NativeEngine.Runtime.linux-x64/) |
| Linux ARM64 | `linux-arm64` | [`WebScene.NativeEngine.Runtime.linux-arm64`](https://www.nuget.org/packages/WebScene.NativeEngine.Runtime.linux-arm64/) |
| Windows x64 | `win-x64` | [`WebScene.NativeEngine.Runtime.win-x64`](https://www.nuget.org/packages/WebScene.NativeEngine.Runtime.win-x64/) |

Additional RIDs listed by the package definition are reserved until their release
lanes are enabled.

## Reproducible Linux builds

Linux packages use the immutable inputs recorded in `linux-build-lock.json` and
the .NET-style x64 cross-builder in `Dockerfile.linux-glibc`. Build either glibc
RID locally with the same entry point used by CI:

```bash
scripts/build-linux-native-runtime.sh --rid linux-x64 --package-version VERSION
scripts/build-linux-native-runtime.sh --rid linux-arm64 --package-version VERSION
```

The ARM64 command creates a cross-build stage. CI transfers that stage to a
native ARM64 runner and calls the same command with `--stage finalize` to create
the V8 bootstrap snapshot, execute tests, and pack the NuGet package. Published
Linux binaries must pass `verify-linux-native-abi.py`, including the glibc 2.27,
GLIBCXX, CXXABI, dependency, architecture, RPATH, and exported-ABI gates.
