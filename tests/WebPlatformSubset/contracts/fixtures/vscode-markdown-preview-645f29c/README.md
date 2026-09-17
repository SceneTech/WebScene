# Code OSS on AppScene

Native Apple Silicon application hosting the source-built Code OSS web workbench
in **AppScene / WebScene**. The application bundles its own Node runtime and
upstream Code OSS server for local filesystem and extension-host services.

This repository uses AppScene's native window, WebScene's V8/DOM/layout engine,
and the native graphics presenter. It does not embed an Electron application.
The requested VS Code source is a real, pinned Git submodule at `vendor/vscode`.

## Build status and downloads

There is currently no promoted `.app` from the active heads. The committed
inputs are AppScene `38e38cc8`, WebScene `4ccadd13`, and unchanged Code OSS
`645f29cc`; WebScene `main` has advanced to `8c792eed` through the focused
selector-performance PR #326. The next demo repin waits for the three active
focused fixes so one commit records the complete train. A local producer has
completed pinned Dawn and is building V8 for the exact replacement SDK; the
stale queued remote producer was canceled. AppScene #130 owns SDK installation,
the Release build, product gates, and promotion.

The complete investigation hierarchy and four-agent dependency schedule are in
[`docs/investigation-backlog.md`](docs/investigation-backlog.md). The feature
matrix remains in [`docs/feature-acceptance.md`](docs/feature-acceptance.md).

[macOS arm64 CI](https://github.com/SceneTech/vscode-demo/actions/workflows/macos-arm64.yml)
builds on `macos-26`. A successful complete run publishes
**code-oss-appscene-macos-arm64**, containing a ZIP, DMG, checksums and build
provenance. Download and extract the artifact, then open the DMG and drag
**Code OSS AppScene.app** to Applications, or extract the inner ZIP with Archive
Utility. The ZIP preserves executable permissions, bundle structure and symlinks.

**Requirements:** Apple Silicon and macOS 26 or newer. The minimum follows the
pinned native SDK. Packages are ad-hoc signed development builds, not Developer
ID signed or Apple notarized. macOS may require using **Open Anyway** in System
Settings → Privacy & Security after an initial launch attempt.

The complete package is uploaded only after the packaged native app has rendered
the workbench and successfully edited and undone text in the real upstream
editor. Source/helper tests and a successful server startup alone do not qualify
the native app. See [runtime compatibility](docs/compatibility.md) for the exact
acceptance contract and remaining engine limitations. The separate required
[packaged performance gate](docs/performance.md) measures startup, visible and
hidden idle cost, input/frame latency, memory, engine phases, and panel-tree
attachment against versioned budgets.

The current broader [feature acceptance matrix](docs/feature-acceptance.md)
separates the passing editor baseline from partial, pending, and blocked
Welcome, command, Explorer, file, terminal, search, Settings, and extension
scenarios. Treat that matrix as the current usability status of the local
release.

The [open integration issue plan](docs/open-issue-plan.md) inventories every
still-open issue created for this effort, distinguishes code that exists only on
the consolidation branches from code already on `main`, and records the required
dependency and merge order.

If packaging succeeds but the native editor test fails, CI also preserves
**code-oss-appscene-macos-arm64-unqualified**. It contains the ZIP/DMG under
`dist/` and the failed launch evidence under `artifacts/smoke/`. This artifact is
for reproducing and diagnosing native runtime failures; check its smoke report
before treating it as a usable editor. The workflow remains failed, and the
ordinary qualified artifact is still published only after the editor test passes.

## CI dependencies

The archive under [`vendor-sdk/parts/`](vendor-sdk/parts/) and the corresponding
`.cache/appscene-sdk` installation are retained for older diagnostic lanes. They
precede the AppScene revision currently pinned at `vendor/appscene` and cannot
qualify a new release. The release path uses an SDK installed by the native
AppScene CLI and requires its manifest to identify the exact clean AppScene and
WebScene gitlink revisions in this repository.

The source-build job uses its read-only Actions `GITHUB_TOKEN` for public
upstream extension downloads, avoiding anonymous GitHub API limits.

The part checksums, combined compressed archive checksum, SDK manifest, file checksums, platform, minimum
OS version and clean AppScene/WebScene source revisions are verified against
[`config/upstream.lock.json`](config/upstream.lock.json). The original producer
run ID, artifact ID, ZIP and gzip archive checksums are retained as provenance.
The installer assembles the verified parts automatically. The bundled archive
contains the same SDK files in XZ compression; it remains
available after the original upstream artifact expires on **12 October 2026**.
See [`vendor-sdk/README.md`](vendor-sdk/README.md) for the retained archive's
provenance. A missing or modified bundled archive fails explicitly in diagnostic
use; release builds never silently select it.

To fetch the original producer artifact explicitly, use `--download` and supply
**SCENETECH_READ_TOKEN** through your environment with **Actions: read** access
to the private `SceneTech/AppScene` repository. The ordinary `vscode-demo`
`GITHUB_TOKEN` cannot read that separate private repository. An existing verified
SDK cache is reused by all source options; use a fresh `--output` directory when
checking a different archive. `--archive` accepts the original pinned gzip
archive for offline builds.

The pinned SDK producer and its consumer/presentation suite passed. The native
host uses the SDK's exact Homebrew LLVM 22.1.8 compiler profile with the macOS
system libc++ runtime. Its C++ sources do not use modules; the host selects the
matching Apple libc++ headers to avoid mixing newer atomic-wait declarations
with the system runtime.

## Local build and release

Install the native AppScene CLI, then install the SDK archive rebuilt from the
exact AppScene and WebScene gitlinks in this repository. Install Xcode command-line
tools, CMake, Ninja, Python 3.12+, and the exact build Node version from
`vendor/vscode/.nvmrc`. No system Node installation is needed by the finished
application.

```sh
git clone --recurse-submodules https://github.com/SceneTech/vscode-demo.git
cd vscode-demo
nvm install "$(cat vendor/vscode/.nvmrc)"
nvm use "$(cat vendor/vscode/.nvmrc)"

# Install the rebuilt archive through the native CLI. Use the archive's real
# SHA-256 value; AppScene SDK versions are immutable after installation.
appscene sdk install 0.1.0-preview.3 \
  --archive /absolute/path/appscene-webscene-sdk.tar.gz \
  --sha256 REBUILT_ARCHIVE_SHA256 --offline --yes

# Check CLI support, canonical .asproj capabilities, prerequisites, complete SDK
# integrity, and exact clean source revisions without running a build.
python3 scripts/appscene-release.py release --dry-run --jobs 3

# Build Code OSS, the native host, the signed bundle and publication directory.
python3 scripts/appscene-release.py release --jobs 3 \
  --output "$PWD/dist/appscene-release"
```

`appscene build` owns the declared Code OSS `BuildStep`, CMake configuration,
native compilation, resource staging, package policy, and signing. `appscene
publish` stages the verified bundle and writes the publication inventory. The
wrapper invokes both commands and refuses SDK source-revision mismatches,
capability mismatches, dirty SDK provenance, failed `appscene doctor` checks,
and SDK paths outside the CLI's installed SDK store. See the
[AppScene release workflow](docs/appscene-release.md) for the contract and
manual diagnostic commands.

`package-macos.py` accepts only a server payload whose `build-info.json` binds
the pinned VS Code revision, current `patch-vscode.mjs`, and exact production
JavaScript hashes. It also checks the acceptance markers in the compiled
workbench before copying or signing the app. Reusing an older
`build/vscode-reh-web-darwin-arm64` after an overlay change therefore fails;
rerun `appscene build`, which owns the cached `BuildStep`. Local commit
`9c8e31c` also verifies the producer receipt after every AppScene cache lookup,
recomputes the overlay and payload identity, requires all eight contract
markers, and retains `release-freshness.json`. This implements the app-specific
part of [issue #2](https://github.com/SceneTech/vscode-demo/issues/2); the issue
remains open until the final exact Release proves the packaged bytes through
editor, worker, secret, Command Palette, Settings, visual, and lifecycle gates.

The source build installs several gigabytes of temporary npm dependencies. The
pinned compiler is managed by Homebrew; the repository keeps only its compressed
bottle for reproducibility. After packaging, list
the reproducible storage and remove it explicitly:

```sh
python3 scripts/clean-workspace.py
python3 scripts/clean-workspace.py --apply --toolchain
```

The ZIP, DMG, SDK, and smoke evidence are retained. When its ZIP checksum
matches, the cleaner removes the reconstructible unpacked app and keeps the
smaller archive. Add `--packaging-inputs` only after the latest packaged
native smoke passes; the cleaner refuses to remove those inputs after a failed
or missing smoke. A future native build restores the verified compiler bottle
after `--toolchain`; the cleaner never removes Homebrew's shared installation.

Build Node is **24.18.0**; the bundled server uses upstream's separately pinned
**24.18.1** Node runtime. The build keeps remote Node native modules separate from
the root Electron build modules, verifies Node's upstream checksum, and loads
the packaged native modules using the packaged Node executable. It builds the
production server-web target with minification, translations and bundled
extensions. It retains upstream open-source product defaults and license
notices. An external extension marketplace and proprietary Microsoft services
are not configured by this project.

The packager selects the arm64 slice from universal native dependencies in the
staged app, strips local symbols before signing, removes platform-incompatible
and development-only server files, deduplicates exact large immutable assets,
then audits and signs the complete arm64-only bundle. Dependencies without an
arm64 slice are rejected. The package gate caps the app at 775 MB logical and
825 MB allocated, 17,000 file/link entries, the ZIP at 265 MB, the DMG at 325 MB,
and the inventory manifest at 10 MB. Separate limits cover the server,
extensions, root dependencies, Node, frameworks, host, and largest file so one
component cannot consume the total budget unnoticed.
The generated manifest also records the largest files, directory contributors,
and remaining exact same-mode duplicates. CI independently reconciles those
claims against the manifest inventory and companion archives, then publishes
every budget's utilization and remaining headroom in the job summary.

## Runtime layout and diagnostics

| Path | Purpose |
| --- | --- |
| `Contents/MacOS/Code OSS AppScene` | Native C++/Objective-C++ AppScene host |
| `Contents/Frameworks/` | Relocated native graphics dependencies |
| `Contents/Resources/server/` | Code OSS server, web workbench, extensions, native modules and Node |
| `~/Library/Application Support/Code OSS AppScene/` | Server user data, installed extensions, logs and native compilation cache |

The host creates a fresh cryptographically random token, places it in a private
temporary file, starts Node with an OS-selected loopback port, and waits for an
authenticated HTML response before starting the native workbench. Process
arguments use `posix_spawn`; shell interpolation is not involved. The host owns
the child process group and stops it on normal shutdown. Resource loading only
sends the connection cookie to the application's own loopback origin.

The resource adapter supports binary responses and native resource sizing/copy
callbacks. It does not open arbitrary external network resources. Native
workbench compatibility for extension webviews, web workers, browser storage,
IME and other advanced APIs remains bounded by the pinned WebScene runtime;
see [the compatibility audit](docs/compatibility.md).

For a bundled-server diagnostic:

```sh
'dist/Code OSS AppScene.app/Contents/MacOS/Code OSS AppScene' --server-self-test
```

The native smoke runs from a separate working directory and records structured
results, stdout/stderr and available presentation evidence. Its small source
overlay is enabled only by `scene-smoke=1`; it verifies the actual editor model,
rendered lines and undo result. The build records the overlay hash and restores
the original submodule source on exit.

For a repeatable computer-use session, start the packaged executable directly:

```sh
'build/native/Code OSS AppScene.app/Contents/MacOS/Code OSS AppScene' --manual-smoke
```

This uses a transient profile, creates the same real Monaco editor used by the
automated smoke, and leaves the native window open after the editor is ready.
It does not change the Code OSS source tree or add an Electron compatibility
layer. Use the automated smoke for pass/fail qualification; `--manual-smoke` is
an interactive diagnostic for pointer, keyboard, clipboard, window and
presentation behavior.

On macOS, the repeatable Accessibility-generated pointer, keyboard, clipboard,
and normal-close qualification is:

```sh
python3 scripts/manual-input-smoke.py \
  --app 'build/native/Code OSS AppScene.app' \
  --output artifacts/manual-input
```

It requires `cliclick`, preserves the original text clipboard, records only
numeric AppScene input-routing metadata, and writes a screenshot plus a JSON
summary. The screenshot is limited to the AppScene window bounds. The fixed
markers make copy and paste byte-exact without retaining user input or clipboard
contents in the diagnostic trace.

Rendering and layout changes have a separate original-browser comparison gate:

```sh
python3 -m pip install -r requirements-visual-parity.txt
python3 scripts/visual-parity.py run \
  --scenario editor \
  --app 'dist/Code OSS AppScene.app' \
  --server build/vscode-reh-web-darwin-arm64/bin/code-server-oss \
  --output artifacts/visual-parity
```

It opens the same fixed TypeScript file and UI state at 1440 × 940 in the
original Code OSS Chromium workbench and AppScene/WebScene. It gates measured
workbench rectangles and bounded pixel differences, then retains both images,
a heatmap, overlay, blink view, logs, hashes, and an HTML report. Named
scenarios also write a fail-closed evidence audit with exact engine/scenario,
payload, screenshot and review-artifact hashes, exact clean AppScene/WebScene
SDK source revisions, explicit check denominators, and versioned run/report
disk budgets. Editor, deterministic dirty tabs, Welcome, Command Palette,
Settings, and the Problems panel have checked-in capture contracts; the
inventory keeps every other surface explicitly planned.
See
[the visual parity contract](docs/visual-parity.md) before changing rendering
code or acceptance thresholds.

Release-wide workbench behavior has a separate fail-closed inventory:

```sh
python3 -m unittest tests.test_workbench_scenario_manifest -v
python3 scripts/workbench_scenario_manifest.py \
  --output artifacts/workbench-scenario-report.json
```

It pins the unchanged Code OSS entry points and commands for editor groups,
Search, SCM, Problems/Output, tasks/terminal, Run/Debug, extensions, files,
dialogs, settings, profiles, and workbench lifecycle. Twelve scenarios contain
48 ordered actions and 96 explicit product/native/browser/visual/performance/
lifecycle/accessibility/security obligations. Blocked or missing evidence stays
in the denominator and cannot be promoted by a visual smoke alone.

The real Command Palette has a separate fail-closed lane with the same payload,
clean profile policy, theme, and viewport:

```sh
python3 scripts/visual-parity.py run \
  --scenario command-palette \
  --app 'build/release-final-next11/Code OSS AppScene.app' \
  --server 'build/release-final-next11/Code OSS AppScene.app/Contents/Resources/server/bin/code-server-oss' \
  --output artifacts/command-palette-parity
```

It verifies the exact filtered command and focused row, compares palette
geometry, and evaluates pixel thresholds inside the measured palette widget.

The Settings lane opens the real Settings editor and proves the two native
`select` paths used by the commonly-used settings list:

```sh
python3 scripts/visual-parity.py run \
  --scenario settings \
  --app 'build/release-final/Code OSS AppScene.app' \
  --server 'build/release-final/Code OSS AppScene.app/Contents/Resources/server/bin/code-server-oss' \
  --output artifacts/settings-parity
```

It requires the exact `files.autoSave` and `editor.defaultFormatter` selected
values and labels, measures both rows and controls, and evaluates pixels only
inside the Settings editor region.

## Development checks

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
node --test tests/*.test.mjs
python3 scripts/test-native-host.py
python3 scripts/test-resource-loader.py --sdk .cache/appscene-sdk
python3 scripts/clean-workspace.py
bash -n scripts/*.sh
node scripts/patch-vscode.mjs --source vendor/vscode --check
bash scripts/build-vscode.sh --dry-run
```

Update the VS Code gitlink, lock and validated build helper together. The
build rejects unexpected source revisions, dirty tracked submodule files,
changed overlay anchors, missing runtime checksums and accidental replacement
of unrelated output directories. Packaging independently rejects stale overlay
metadata, payload hashes, acceptance markers, or a server build from a
different pinned revision.

## Licenses

Code OSS is built from [microsoft/vscode](https://github.com/microsoft/vscode),
under its source license. The bundle includes its license, third-party notices,
Node's notices, and the AppScene/WebScene SDK's transitive notices. Refer to those
files for each component's terms. This repository does not use Microsoft's
proprietary Visual Studio Code distribution.
