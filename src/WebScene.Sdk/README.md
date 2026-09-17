# WebScene.Sdk

`WebScene.Sdk` is the portable product layer for packaged React/TypeScript components.
It provides the versioned Component Profile 1 manifest, offline asset validation,
shared immutable asset caching, isolated component instance state, compatibility
diagnostics, and a JSON-only asynchronous host bridge.

Host capabilities are explicit (`host.commands`, `host.settings`,
`host.notifications`, `host.network`, `host.clipboard`, and `host.files`). A component
must declare a capability and the application must install a handler before a request
can run. The bridge does not expose arbitrary CLR objects and does not claim to be a
security sandbox for untrusted code.

See `tooling/webscene` for TypeScript declarations and bundler plugins. Avalonia and
Uno Skia desktop applications host these packages with `WebScene.Sdk.Avalonia` or
`WebScene.Sdk.Uno` and their native `WebSceneComponentHost` controls. Application
templates remain intentionally absent.

The native macOS SDK's compiler, libc++ header, and system runtime closure is
defined by `cmake/WebSceneMacOSProfile.cmake`. The installed relocation and
consumer gate is documented in
[`docs/guides/macos-sdk-runtime-profile.md`](../../docs/guides/macos-sdk-runtime-profile.md).
