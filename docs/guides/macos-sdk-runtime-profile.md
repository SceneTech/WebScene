# macOS SDK compiler and runtime profile

The macOS ARM64 SDK uses one qualified C++ closure:

- Homebrew LLVM 22.1.8, identified by the compiler binary and configuration
  SHA-256 values in `WebSceneMacOSProfile.cmake`;
- the pinned Homebrew libc++ and Clang resource header trees recorded in that
  profile;
- C++20, architecture `arm64`, and deployment target `26.0`;
- macOS's `/usr/lib/libc++.1.dylib` and `/usr/lib/libSystem.B.dylib` at runtime.

The SDK does not stage or load Homebrew libc++, libc++abi, or libunwind dylibs.
Producer objects, prebuilt dependencies, and installed consumers must use the
same system runtime closure. `WebSceneToolchain.cmake` rejects a compiler binary
outside the qualified profile before CMake enables C++, and
`WebSceneConfig.cmake` repeats the compiler, architecture, and deployment checks
for every installed consumer.

## Side-by-side compiler installations

Pass `-DWEBSCENE_LLVM_ROOT=/absolute/path/to/qualified/llvm` when the pinned
compiler is outside Homebrew's default `opt/llvm` location. The toolchain forwards
this root into CMake's nested `try_compile` projects as well as the main build.
Otherwise compiler ABI detection can incorrectly reselect an unrelated default
Homebrew compiler even after the main configuration verified the intended binary.
This option does not relax the binary, configuration or header-tree checks. A
newer rebuild with the same `clang --version` string is not sufficient evidence
that it belongs to the qualified profile.

## Installed qualification

Run the installed tool against an SDK produced with the intended source commit:

```sh
python3 "$SDK/share/webscene/tools/qualify_macos_profile.py" \
  --sdk "$SDK" \
  --llvm-root /opt/homebrew/opt/llvm \
  --producer-revision "$(git rev-parse HEAD)" \
  --payload-provenance exact-clean-producer \
  --output artifacts/macos-runtime-profile.json
```

The qualifier verifies the compiler binary, compiler configuration, libc++
headers, and Clang resource headers. It copies the SDK to a new read-only path,
then creates a consumer source and build outside the producer checkout. That
consumer imports `WebScene::NativeWeb` and `WebScene::WebGPU`, compiles, links,
executes with an unrelated working directory, and exercises a native document.
The resulting JSON records every audited Mach-O dependency and rpath and confirms
that dyld loaded Dawn from the relocated SDK. Absolute producer and Homebrew
runtime paths fail the gate.

The `payload-provenance` field must describe the payload under test. A package
contract overlay on an older verified binary SDK is valid evidence for the
installed-profile behavior, but it is not evidence that the current source
completed a clean producer build or V8 snapshot generation. Release evidence
must use `exact-clean-producer` only after those separate gates pass.
