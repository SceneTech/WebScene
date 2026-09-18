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

## Final macOS bundle audit

The installed `share/webscene/tools/audit_macos_bundle.py` tool recursively checks
every Mach-O file in a completed application bundle. It verifies each architecture
slice, its minimum macOS version, install name, rpaths, and the complete bundled or
allowlisted-system dependency closure. The scan rejects escaping, dangling, and
case-mismatched paths, unsafe symlinks, concurrent bundle mutation, and bundles over
the configured entry or evidence limits.

`package_macos.py` runs the audit after it has copied, stripped, and individually
signed native files, writes `Contents/Resources/webscene-macho-audit.json`, and then
signs and verifies the outer bundle. The evidence format is installed at
`share/webscene/schemas/webscene-macho-audit.schema.json`. Consumers can also run the
audit directly:

```sh
python3 share/webscene/tools/audit_macos_bundle.py \
  --bundle Example.app \
  --executable Contents/MacOS/Example \
  --architecture arm64 \
  --maximum-deployment-target 15.0 \
  --evidence Example.macho-audit.json
```

Repeat `--architecture` for a universal bundle. The configured architecture set must
match every Mach-O file exactly.
