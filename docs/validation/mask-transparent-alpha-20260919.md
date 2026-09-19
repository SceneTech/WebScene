# Mask transparency portability — 2026-09-19

Current-main CI run `35431128608` passed build, tooling, ABI and architecture
gates, then failed the Avalonia headless mask tests on Linux and macOS. The six
failed assertions compared `#00ffffff` with `#00000000` at pixels whose alpha
was zero.

For premultiplied fully transparent pixels, the stored RGB channels do not
affect the rendered result and are not stable across supported Skia profiles.
The mask contracts now assert alpha zero at holes while preserving exact color
assertions for every visible foreground pixel and the clipped region boundary.

Only `git diff --check` was run locally under the fast implementation policy.
The exact current-main Linux and macOS Avalonia jobs are the execution gate.
