# Installed Linux Runtime SDK (#810)

The Linux SDK keeps its existing Native-only default. Opting into
`WEBSCENE_SDK_RUNTIME=ON` now selects the static full-V8 engine and installs a
relocatable `WebScene::Runtime` closure: media, WebSocket, HTML parser, V8,
Mbed TLS crypto, Dawn, ANGLE, OpenSSL/Zlib system dependencies, ICU data, and
the bootstrap snapshot and metadata. The installed config checks every
runtime data file before exporting the target and still reports Runtime as a
missing component when the producer option is off.

The Runtime contains no application module and adds no Electron, Chromium,
CEF, WebView, browser process, or Code OSS source change. Runtime selection is
a configure/install concern and adds zero per-frame CSS cascade, layout,
visual-tree, scene, or scheduling work. Demand-driven behavior remains owned
by the engine and AppScene platform host gates.

`scripts/test_linux_runtime_sdk_source.py` records the option/default,
installed closure, fail-closed component, ordered link dependency, and Code
OSS no-port documentation contracts. The focused installed-consumer,
relocation, ABI/export, package logical/allocated size, entry-count,
duplicate, dependency, cache cleanup, unchanged Code OSS, VM, memory,
lifecycle, idle CPU/frame, and performance gates are **authored, not
executed** under the current fast-merge direction.
