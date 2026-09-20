# Windows Runtime installed-SDK layout (#816)

The Windows native Runtime package now supports two relocatable CMake layouts.
The existing NuGet layout keeps its CMake surface under `build/native` and its
runtime under `runtimes/<rid>/native`. An SDK assembler can copy the verified
header, import library, CMake files, DLL, runtime manifest, data files, and
licenses into canonical `include`, `lib`, `lib/cmake/WebScene`, `bin`, and
`share/licenses` directories without rewriting the package config.

Configuration scans only those two declared runtime directories. A candidate is
accepted only when both `webscene_native_engine.dll` and
`webscene-native-runtime.json` are present. A half-copied candidate fails
immediately, and two complete candidates fail as ambiguous. The chosen layout is
published as `WebScene_RUNTIME_LAYOUT`; `WebScene_RUNTIME_DIRECTORY` names the
directory that an application must deploy.

RID, target architecture, native ABI version, artifact names, DLL SHA-256,
import-library SHA-256, and public-header SHA-256 retain the same checks after
selection. The imported target therefore cannot silently bind a stale flattened
DLL or combine assets from the two layouts.

The focused source and release-package contracts cover both layout markers,
exactly-one selection, partial-layout rejection, identity/hash checks, and the
published runtime directory. They are authored but intentionally unexecuted
under the active fast implementation direction. Windows package production,
relocation, installed AppScene consumption, unchanged Code OSS, performance,
memory, lifecycle, and VM evidence remain in #809 and AppScene #322.
