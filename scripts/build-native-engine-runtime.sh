#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rid=
output_dir="$repo_root/artifacts/native-engine-runtime"
package_version=
v8_root=
v8_output_root=
v8_workspace=
v8_revision=15.3.10
html_parser=html5ever
css_parser=cssparser
selector_parser=servo
dom_bindings=generated
v8_snapshot=bootstrap
thin_lto=false
upstream_v8=false
disable_wasm=false
partition_alloc=false
cmake_build_type=Release
target_triple=
rust_target_triple=
sysroot=
builder_identity=
glibc_baseline=
depot_tools_commit=ca054941f756b50e1a3d83727270d879bec1f331
defer_target_execution=false
finalize_only=false

usage() {
  echo "Usage: $0 --rid osx-arm64|osx-x64|linux-arm64|linux-x64 [--output DIR] [--package-version VERSION] [--v8-root DIR] [--v8-output-root DIR] [--v8-workspace DIR] [--v8-revision REVISION] [--target-triple TRIPLE] [--rust-target-triple TRIPLE] [--sysroot DIR] [--builder-identity ID] [--glibc-baseline VERSION] [--depot-tools-commit SHA] [--defer-target-execution|--finalize-only] [--html-parser legacy|html5ever] [--css-parser legacy|cssparser] [--selector-parser legacy|servo] [--dom-bindings legacy|generated] [--v8-snapshot none|bootstrap] [--cmake-build-type Release|RelWithDebInfo] [--upstream-v8] [--thin-lto] [--disable-wasm] [--partition-alloc]" >&2
}

while (($# > 0)); do
  case "$1" in
    --rid) rid="${2:-}"; shift 2 ;;
    --output) output_dir="${2:-}"; shift 2 ;;
    --package-version) package_version="${2:-}"; shift 2 ;;
    --v8-root) v8_root="${2:-}"; shift 2 ;;
    --v8-output-root) v8_output_root="${2:-}"; shift 2 ;;
    --v8-workspace) v8_workspace="${2:-}"; shift 2 ;;
    --v8-revision) v8_revision="${2:-}"; shift 2 ;;
    --target-triple) target_triple="${2:-}"; shift 2 ;;
    --rust-target-triple) rust_target_triple="${2:-}"; shift 2 ;;
    --sysroot) sysroot="${2:-}"; shift 2 ;;
    --builder-identity) builder_identity="${2:-}"; shift 2 ;;
    --glibc-baseline) glibc_baseline="${2:-}"; shift 2 ;;
    --depot-tools-commit) depot_tools_commit="${2:-}"; shift 2 ;;
    --defer-target-execution) defer_target_execution=true; shift ;;
    --finalize-only) finalize_only=true; shift ;;
    --html-parser) html_parser="${2:-}"; shift 2 ;;
    --css-parser) css_parser="${2:-}"; shift 2 ;;
    --selector-parser) selector_parser="${2:-}"; shift 2 ;;
    --dom-bindings) dom_bindings="${2:-}"; shift 2 ;;
    --v8-snapshot) v8_snapshot="${2:-}"; shift 2 ;;
    --cmake-build-type) cmake_build_type="${2:-}"; shift 2 ;;
    --upstream-v8) upstream_v8=true; shift ;;
    --thin-lto) thin_lto=true; shift ;;
    --disable-wasm) disable_wasm=true; shift ;;
    --partition-alloc) partition_alloc=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

case "$rid" in
  osx-arm64) expected_kernel=Darwin; expected_machine=arm64; cpu=arm64; native_name=libwebscene_native_engine.dylib ;;
  osx-x64) expected_kernel=Darwin; expected_machine=x86_64; cpu=x64; native_name=libwebscene_native_engine.dylib ;;
  linux-arm64) expected_kernel=Linux; expected_machine=aarch64; cpu=arm64; native_name=libwebscene_native_engine.so ;;
  linux-x64) expected_kernel=Linux; expected_machine=x86_64; cpu=x64; native_name=libwebscene_native_engine.so ;;
  *) usage; exit 1 ;;
esac

if [[ "$html_parser" != legacy && "$html_parser" != html5ever ]]; then
  echo "Unsupported HTML parser '$html_parser'; expected legacy or html5ever." >&2
  exit 1
fi
if [[ "$css_parser" != legacy && "$css_parser" != cssparser ]]; then
  echo "Unsupported CSS parser '$css_parser'; expected legacy or cssparser." >&2
  exit 1
fi
if [[ "$selector_parser" != legacy && "$selector_parser" != servo ]]; then
  echo "Unsupported selector parser '$selector_parser'; expected legacy or servo." >&2
  exit 1
fi
if [[ "$dom_bindings" != legacy && "$dom_bindings" != generated ]]; then
  echo "Unsupported DOM bindings '$dom_bindings'; expected legacy or generated." >&2
  exit 1
fi
if [[ "$v8_snapshot" != none && "$v8_snapshot" != bootstrap ]]; then
  echo "Unsupported V8 snapshot '$v8_snapshot'; expected none or bootstrap." >&2
  exit 1
fi
if [[ "$cmake_build_type" != Release && "$cmake_build_type" != RelWithDebInfo ]]; then
  echo "Unsupported CMake build type '$cmake_build_type'; expected Release or RelWithDebInfo." >&2
  exit 1
fi
if [[ ( "$css_parser" == cssparser || "$selector_parser" == servo )
    && "$html_parser" != html5ever ]]; then
  echo "Servo CSS components require --html-parser html5ever." >&2
  exit 1
fi

v8_configuration=Release
build_variant="-$html_parser-$css_parser-$selector_parser-$dom_bindings-$v8_snapshot"
if [[ "$cmake_build_type" == RelWithDebInfo ]]; then
  build_variant+=-symbols
fi
thin_lto_cmake=OFF
partition_alloc_cmake=OFF
v8_webassembly=true
if [[ "$thin_lto" == true ]]; then
  v8_configuration=ReleaseThinLto
  build_variant+=-thinlto-llvm
  thin_lto_cmake=ON
fi
if [[ "$disable_wasm" == true ]]; then
  v8_configuration+=NoWasm
  build_variant+=-no-wasm
  v8_webassembly=false
fi
if [[ "$partition_alloc" == true ]]; then
  v8_configuration+=PartitionAlloc
  build_variant+=-partitionalloc
  partition_alloc_cmake=ON
fi
build_variant+=-inspector

if [[ -z "$package_version" ]]; then
  package_version="$(
    dotnet msbuild "$repo_root/src/WebScene.Core/WebScene.Core.csproj" \
      -getProperty:PackageVersion -nologo |
      tail -n 1 | tr -d '\r'
  )"
fi
if [[ -z "$package_version" ]]; then
  echo "Unable to resolve the native runtime package version." >&2
  exit 1
fi

macos_arm64_to_x64=false
host_kernel="$(uname -s)"
host_machine="$(uname -m)"
if [[ "$rid" == osx-x64 && "$host_kernel" == Darwin && "$host_machine" == arm64 ]]; then
  macos_arm64_to_x64=true
fi
if [[ "$expected_kernel" == Darwin \
    && ( "$host_kernel" != "$expected_kernel" \
      || ( "$host_machine" != "$expected_machine" && "$macos_arm64_to_x64" != true ) ) ]]; then
  echo "RID '$rid' must be built natively on $expected_kernel/$expected_machine; current host is $host_kernel/$host_machine." >&2
  exit 1
fi
if [[ "$expected_kernel" == Darwin && -z "$rust_target_triple" ]]; then
  if [[ "$cpu" == x64 ]]; then
    rust_target_triple=x86_64-apple-darwin
  else
    rust_target_triple=aarch64-apple-darwin
  fi
fi
if [[ "$expected_kernel" == Darwin ]]; then
  rust_version=1.90.0
  rust_mac_arm64_sha256=9772d20d5cd736079a0ee84d00e6697cf2084f0fc4621b011e24e6f2d08d2d7f
  rust_mac_x64_std_sha256=dd731e6f9f30cb9b2928b92b084d2f12a3abf06a481ecbd8c3553c3e6f742139
  rust_prefix="${RUNNER_TOOL_CACHE:-${RUNNER_TEMP:-$repo_root/artifacts/toolchains}}/webscene-rust-$rust_version"
  rust_complete="$rust_prefix/.webscene-complete"
  if [[ ! -f "$rust_complete" ]]; then
    rust_download_dir="$(mktemp -d "${RUNNER_TEMP:-/tmp}/webscene-rust.XXXXXX")"
    (
      cd "$rust_download_dir"
      host_archive="rust-$rust_version-aarch64-apple-darwin.tar.xz"
      x64_std_archive="rust-std-$rust_version-x86_64-apple-darwin.tar.xz"
      curl --fail --silent --show-error --location \
        --retry 5 --retry-delay 2 --retry-all-errors --connect-timeout 20 \
        --remote-name "https://static.rust-lang.org/dist/$host_archive"
      echo "$rust_mac_arm64_sha256  $host_archive" | shasum -a 256 -c -
      tar -xf "$host_archive"
      "${host_archive%.tar.xz}/install.sh" --prefix="$rust_prefix" --without=rust-docs
      curl --fail --silent --show-error --location \
        --retry 5 --retry-delay 2 --retry-all-errors --connect-timeout 20 \
        --remote-name "https://static.rust-lang.org/dist/$x64_std_archive"
      echo "$rust_mac_x64_std_sha256  $x64_std_archive" | shasum -a 256 -c -
      tar -xf "$x64_std_archive"
      "${x64_std_archive%.tar.xz}/install.sh" --prefix="$rust_prefix"
      : > "$rust_complete"
    )
  fi
  export PATH="$rust_prefix/bin:$PATH"
  if [[ "$(rustc --version)" != "rustc $rust_version "* ]]; then
    echo "Pinned macOS Rust toolchain validation failed: $(rustc --version)" >&2
    exit 1
  fi
fi
if [[ "$expected_kernel" == Linux ]]; then
  case "$rid:$target_triple" in
    linux-x64:x86_64-linux-gnu|linux-arm64:aarch64-linux-gnu) ;;
    *) echo "RID '$rid' requires its locked Linux target triple, not '$target_triple'." >&2; exit 1 ;;
  esac
  case "$rid:$rust_target_triple" in
    linux-x64:x86_64-unknown-linux-gnu|linux-arm64:aarch64-unknown-linux-gnu) ;;
    *) echo "RID '$rid' requires its locked Rust target triple, not '$rust_target_triple'." >&2; exit 1 ;;
  esac
  if [[ "$finalize_only" == false && ! -d "$sysroot" ]]; then
    echo "Linux cross-build sysroot is missing: $sysroot" >&2
    exit 1
  fi
  if [[ -z "$builder_identity" || -z "$glibc_baseline" ]]; then
    echo "Linux release builds require --builder-identity and --glibc-baseline." >&2
    exit 1
  fi
fi

if [[ "$finalize_only" == true && -z "$v8_root" ]]; then
  v8_workspace="${v8_workspace:-$repo_root/artifacts/native-engine-v8/$rid}"
  v8_root="$v8_workspace/v8"
fi

if [[ -z "$v8_root" ]]; then
  v8_workspace="${v8_workspace:-$repo_root/artifacts/native-engine-v8/$rid}"
  depot_tools="$v8_workspace/depot_tools"
  v8_root="$v8_workspace/v8"
  mkdir -p "$v8_workspace"

  if [[ ! -d "$depot_tools/.git" && -d /opt/depot_tools/.git ]]; then
    git clone --no-checkout /opt/depot_tools "$depot_tools"
    git -C "$depot_tools" checkout --detach "$depot_tools_commit"
  elif [[ ! -d "$depot_tools/.git" ]]; then
    clone_attempt=1
    while ! git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$depot_tools"; do
      if ((clone_attempt >= 3)); then
        echo "Unable to clone depot_tools after $clone_attempt attempts." >&2
        exit 1
      fi
      echo "depot_tools clone failed; retrying (attempt $((clone_attempt + 1))/3)." >&2
      rm -rf "$depot_tools"
      clone_attempt=$((clone_attempt + 1))
    done
  fi
  if [[ "$(git -C "$depot_tools" rev-parse HEAD)" != "$depot_tools_commit" ]]; then
    git -C "$depot_tools" fetch origin "$depot_tools_commit"
    git -C "$depot_tools" checkout --detach "$depot_tools_commit"
  fi
  export PATH="$depot_tools:$PATH"
  if [[ ! -f "$depot_tools/python3_bin_reldir.txt" ]]; then
    "$depot_tools/ensure_bootstrap"
  fi
  export DEPOT_TOOLS_UPDATE=0

  if [[ ! -f "$v8_workspace/.gclient" ]]; then
    (
      cd "$v8_workspace"
      gclient config https://chromium.googlesource.com/v8/v8
    )
  fi
  v8_sync_marker="$v8_workspace/.gclient-sync-$v8_revision"
  if [[ ! -f "$v8_sync_marker" ]]; then
    (
      cd "$v8_workspace"
      gclient sync --no-history -r "$v8_revision"
    )
    : > "$v8_sync_marker"
  fi

  apply_patch_once() {
    local checkout="$1"
    local patch_file="$2"
    if git -C "$checkout" apply --check "$patch_file" >/dev/null 2>&1; then
      git -C "$checkout" apply "$patch_file"
    elif ! git -C "$checkout" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
      echo "Cannot apply or recognize V8 patch '$patch_file' in '$checkout'." >&2
      exit 1
    fi
  }
  # WebScene owns the JavaScript console bindings. The inspector bridge keeps
  # the original V8 values so CDP clients receive object ids and previews.
  apply_patch_once "$v8_root" "$repo_root/third-party/v8-patches/V8InspectorConsolePatch.txt"
  if [[ "$upstream_v8" == false && "$v8_revision" != 15.3.10 ]]; then
  apply_patch_once "$v8_root" "$repo_root/third-party/v8-patches/V8Patch.txt"
    apply_patch_once "$v8_root" "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8ToolchainPatch.txt"
  apply_patch_once "$v8_root/build" "$repo_root/third-party/v8-patches/BuildPatch.txt"
  apply_patch_once "$v8_root/third_party/icu" "$repo_root/third-party/v8-patches/ICUPatch.txt"
  fi
  if [[ "$thin_lto" == true ]]; then
    apply_patch_once "$v8_root" "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8ThinLtoPatch.txt"
  fi
  if [[ "$partition_alloc" == true && "$expected_kernel" == Darwin ]]; then
    apply_patch_once \
      "$v8_root/third_party/partition_alloc/src" \
      "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8PartitionAllocMacVisibilityPatch.txt"
  fi
  if [[ "$expected_kernel" == Linux ]]; then
    apply_patch_once \
      "$v8_root/buildtools" \
      "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8LibcxxMemoryResourcePatch.txt"
    apply_patch_once "$v8_root/build" "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8BuildNoCrelPatch.txt"
    if [[ "$cpu" == arm64 ]]; then
      apply_patch_once \
        "$v8_root/third_party/partition_alloc/src" \
        "$repo_root/packaging/WebScene.NativeEngine.Runtime/patches/V8PartitionAllocGlibc227Arm64Patch.txt"
    fi
  fi

  gn_args="chrome_pgo_phase=0 fatal_linker_warnings=false is_cfi=false is_component_build=false is_debug=false symbol_level=0 target_cpu=\"$cpu\" treat_warnings_as_errors=false use_clang_modules=false use_thin_lto=$thin_lto v8_embedder_string=\"-WebScene\" v8_enable_fuzztest=false v8_enable_partition_alloc=$partition_alloc v8_enable_pointer_compression=true v8_enable_pointer_compression_shared_cage=true v8_enable_sandbox=false v8_enable_static_roots=false v8_enable_31bit_smis_on_64bit_arch=false v8_enable_temporal_support=false v8_enable_webassembly=$v8_webassembly v8_monolithic=true v8_use_external_startup_data=false v8_target_cpu=\"$cpu\""
  if [[ "$expected_kernel" == Linux ]]; then
    # V8 15.3 requires C++20 library headers that are newer than the glibc 2.27
    # target sysroot provides. Use Chromium's bundled libc++ while retaining
    # the locked old-glibc sysroot for the platform ABI.
    # Keep V8's bundled LLD for its host tools; the reviewed build patch above
    # disables only CREL emission so Jammy can consume the archive.
    gn_args+=" use_custom_libcxx=true use_lld=true use_sysroot=true target_sysroot=\"$sysroot\" use_glib=false v8_monolithic_for_shared_library=true"
  elif [[ "$expected_kernel" == Darwin ]]; then
    # WebScene's embedding targets use the libc++ supplied by the selected
    # macOS SDK. Build V8 against the same ABI; Chromium's bundled libc++ uses
    # the std::__Cr namespace and cannot be linked with Apple's system libc++.
    gn_args+=" use_custom_libcxx=false"
  fi
  if [[ "$partition_alloc" == true \
      && ( "$expected_kernel" == Linux || "$expected_kernel" == Darwin ) ]]; then
    # A dlopen-loaded runtime cannot safely replace the host process allocator:
    # objects allocated before the DSO is loaded can later be routed to the
    # replacement free(). Keep PartitionAlloc available to V8 while disabling
    # process-wide malloc symbol interposition.
    gn_args+=" use_allocator_shim=false use_partition_alloc_as_malloc=false"
  fi
  (
    cd "$v8_root"
    gn gen "out/$cpu/$v8_configuration" --args="$gn_args"
    if [[ "$expected_kernel" == Linux && "$cpu" == arm64 ]]; then
      partition_alloc_buildflags_relative="gen/third_party/partition_alloc/src/partition_alloc/buildflags.h"
      partition_alloc_buildflags="out/$cpu/$v8_configuration/$partition_alloc_buildflags_relative"
      ninja -C "out/$cpu/$v8_configuration" "$partition_alloc_buildflags_relative"
      if [[ ! -f "$partition_alloc_buildflags" ]]; then
        echo "PartitionAlloc build flags were not generated at '$partition_alloc_buildflags'." >&2
        exit 1
      fi
      # V8's embedder overrides can retain ARM MTE even when the standalone
      # PartitionAlloc default is patched. glibc 2.27 has no sys/ifunc.h, so
      # force the generated target flag off before Ninja consumes it.
      sed -i \
        's/^#define PA_BUILDFLAG_INTERNAL_HAS_MEMORY_TAGGING() (1)$/#define PA_BUILDFLAG_INTERNAL_HAS_MEMORY_TAGGING() (0)/' \
        "$partition_alloc_buildflags"
      if ! grep -Fqx '#define PA_BUILDFLAG_INTERNAL_HAS_MEMORY_TAGGING() (0)' "$partition_alloc_buildflags"; then
        echo "Unable to disable PartitionAlloc memory tagging for the glibc 2.27 ARM64 target." >&2
        exit 1
      fi
    fi
    v8_ninja_targets=(obj/libv8_monolith.a)
    if [[ "$expected_kernel" == Linux ]]; then
      # Cross builds need target-architecture C++ runtime archives in the
      # primary toolchain. V8's ARM64 monolith otherwise builds libc++ only for
      # the x64 host-tools toolchain used by mksnapshot.
      v8_ninja_targets+=(
        obj/buildtools/third_party/libc++/libc++.a
        obj/buildtools/third_party/libc++abi/libc++abi.a)
    fi
    ninja -C "out/$cpu/$v8_configuration" "${v8_ninja_targets[@]}"
    if [[ "$expected_kernel" == Linux ]]; then
      # Chromium emits thin archives here. They only contain paths to the
      # adjacent object files, so restoring just the archives from the V8 SDK
      # cache makes the final WebScene link fail. Repack every member into a
      # regular deterministic archive before the cache is populated.
      llvm_ar="$v8_root/third_party/llvm-build/Release+Asserts/bin/llvm-ar"
      for archive in \
          "out/$cpu/$v8_configuration/obj/buildtools/third_party/libc++/libc++.a" \
          "out/$cpu/$v8_configuration/obj/buildtools/third_party/libc++abi/libc++abi.a"; do
        archive_dir="$(dirname "$archive")"
        archive_name="$(basename "$archive")"
        regular_archive="$archive_name.regular.$$"
        (
          cd "$archive_dir"
          mapfile -t archive_members < <("$llvm_ar" t "$archive_name")
          if (( ${#archive_members[@]} == 0 )); then
            echo "V8 C++ runtime archive has no members: $archive" >&2
            exit 1
          fi
          rm -f "$regular_archive"
          "$llvm_ar" rcD "$regular_archive" "${archive_members[@]}"
          if [[ "$(head -c 7 "$regular_archive")" != '!<arch>' ]]; then
            echo "Failed to materialize regular V8 C++ runtime archive: $archive" >&2
            exit 1
          fi
          mv "$regular_archive" "$archive_name"
        )
      done
    fi
  )
  v8_output_root="$v8_root/out/$cpu/$v8_configuration"
fi

v8_root="$(cd "$v8_root" && pwd)"
v8_output_root="${v8_output_root:-$v8_root/out/$cpu/$v8_configuration}"
if [[ ! -d "$v8_output_root" ]]; then
  echo "V8 output directory is missing: $v8_output_root" >&2
  exit 1
fi
v8_output_root="$(cd "$v8_output_root" && pwd)"
v8_monolith="$v8_output_root/obj/libv8_monolith.a"
icu_data="$v8_output_root/icudtl.dat"
v8_args="$v8_output_root/args.gn"
v8_license="$v8_root/LICENSE"
icu_license="$v8_root/third_party/icu/LICENSE"
for required in "$v8_root/include/v8.h" "$v8_root/include/v8-version.h" \
    "$v8_monolith" "$icu_data" "$v8_args" "$v8_license" "$icu_license"; do
  if [[ ! -f "$required" ]]; then
    echo "Required native runtime input is missing: $required" >&2
    exit 1
  fi
done
if ! grep -q 'virtual void consoleAPICalled' "$v8_root/include/v8-inspector.h"; then
  echo "The V8 SDK at '$v8_root' does not contain WebScene's inspector console bridge." >&2
  echo "Rebuild it with third-party/v8-patches/V8InspectorConsolePatch.txt." >&2
  exit 1
fi
IFS=. read -r expected_v8_major expected_v8_minor expected_v8_build _ <<< "$v8_revision"
v8_version_header="$v8_root/include/v8-version.h"
if ! grep -Eq "^#define V8_MAJOR_VERSION +$expected_v8_major$" "$v8_version_header" \
    || ! grep -Eq "^#define V8_MINOR_VERSION +$expected_v8_minor$" "$v8_version_header" \
    || ! grep -Eq "^#define V8_BUILD_NUMBER +$expected_v8_build$" "$v8_version_header"; then
  echo "The V8 headers at '$v8_root' do not match requested revision $v8_revision." >&2
  exit 1
fi
if ! grep -Eq '^v8_enable_pointer_compression *= *true$' "$v8_args" \
    || ! grep -Eq '^v8_enable_pointer_compression_shared_cage *= *true$' "$v8_args"; then
  echo "The V8 SDK at '$v8_root' is not the required pointer-compressed shared-cage build." >&2
  exit 1
fi
if ! grep -Eq "^use_thin_lto *= *$thin_lto$" "$v8_args"; then
  echo "The V8 SDK at '$v8_output_root' does not match requested ThinLTO=$thin_lto." >&2
  exit 1
fi
if ! grep -Eq "^v8_enable_partition_alloc *= *$partition_alloc$" "$v8_args"; then
  echo "The V8 SDK at '$v8_output_root' does not match requested PartitionAlloc=$partition_alloc." >&2
  exit 1
fi
if grep -Eq '^v8_enable_webassembly *=' "$v8_args" \
    && ! grep -Eq "^v8_enable_webassembly *= *$v8_webassembly$" "$v8_args"; then
  echo "The V8 SDK at '$v8_output_root' does not match requested WebAssembly=$v8_webassembly." >&2
  exit 1
fi
if [[ "$v8_webassembly" == false ]] \
    && ! grep -Eq '^v8_enable_webassembly *= *false$' "$v8_args"; then
  echo "The V8 SDK at '$v8_output_root' does not explicitly disable WebAssembly." >&2
  exit 1
fi
if [[ "$expected_kernel" == Linux ]] \
    && ! grep -Eq '^use_lld *= *true$' "$v8_args"; then
  echo "The V8 SDK at '$v8_root' was not built with the required patched LLD configuration." >&2
  exit 1
fi
if [[ "$expected_kernel" == Linux ]] \
    && { ! grep -Eq '^use_sysroot *= *true$' "$v8_args" \
      || ! grep -Fq "target_sysroot = \"$sysroot\"" "$v8_args"; }; then
  echo "The V8 SDK at '$v8_root' was not built against the locked target sysroot." >&2
  exit 1
fi
if [[ "$expected_kernel" == Linux ]] \
    && ! grep -Eq '^v8_monolithic_for_shared_library *= *true$' "$v8_args"; then
  echo "The V8 SDK at '$v8_root' is not safe to link into a shared library." >&2
  exit 1
fi
if [[ "$expected_kernel" == Linux ]] \
    && ! grep -Eq '^use_custom_libcxx *= *true$' "$v8_args"; then
  echo "The V8 SDK at '$v8_root' was not built with Chromium's required Linux libc++." >&2
  exit 1
fi
if [[ "$expected_kernel" == Darwin ]] \
    && ! grep -Eq '^use_custom_libcxx *= *false$' "$v8_args"; then
  echo "The V8 SDK at '$v8_root' was not built with the macOS system libc++." >&2
  exit 1
fi
if [[ "$partition_alloc" == true \
    && ( "$expected_kernel" == Linux || "$expected_kernel" == Darwin ) ]] \
    && { ! grep -Eq '^use_allocator_shim *= *false$' "$v8_args" \
      || ! grep -Eq '^use_partition_alloc_as_malloc *= *false$' "$v8_args"; }; then
  echo "The V8 SDK at '$v8_root' enables unsafe process-wide allocator interposition." >&2
  exit 1
fi

build_dir="$repo_root/artifacts/native-engine-runtime-build/$rid$build_variant"
cmake_args=(
  -S "$repo_root/experiments/WebScene.NativeEngine.Probe"
  -B "$build_dir"
  -DCMAKE_BUILD_TYPE="$cmake_build_type"
  -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8=ON
  -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8_INSPECTOR=ON
  -DWEBSCENE_V8_POINTER_COMPRESSION=ON
  -DWEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE=ON
  -DWEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT=ON
  -DWEBSCENE_V8_PARTITION_ALLOC="$partition_alloc_cmake"
  -DWEBSCENE_NATIVE_ENGINE_DENSE_LINK=ON
  -DWEBSCENE_NATIVE_ENGINE_THIN_LTO="$thin_lto_cmake"
  -DWEBSCENE_NATIVE_ENGINE_CERTIFICATION=OFF
  -DWEBSCENE_NATIVE_ENGINE_HTML_PARSER="$html_parser"
  -DWEBSCENE_NATIVE_ENGINE_CSS_PARSER="$css_parser"
  -DWEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER="$selector_parser"
  -DWEBSCENE_NATIVE_ENGINE_DOM_BINDINGS="$dom_bindings"
  -DWEBSCENE_NATIVE_ENGINE_V8_SNAPSHOT="$v8_snapshot"
  -DWEBSCENE_V8_ROOT="$v8_root"
  -DWEBSCENE_V8_OUTPUT_ROOT="$v8_output_root"
  -DWEBSCENE_NATIVE_ENGINE_DEFER_TARGET_EXECUTION="$defer_target_execution"
)
macos_deployment_target=14.0
if [[ "$expected_kernel" == Darwin ]]; then
  macos_architecture=arm64
  if [[ "$cpu" == x64 ]]; then
    macos_architecture=x86_64
  fi
  cmake_args+=(
    -DCMAKE_OSX_ARCHITECTURES="$macos_architecture"
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$macos_deployment_target"
    -DWEBSCENE_RUST_TARGET_TRIPLE="$rust_target_triple"
  )
fi
if [[ "$thin_lto" == true ]]; then
  v8_llvm_bin="$v8_root/third_party/llvm-build/Release+Asserts/bin"
  for llvm_tool in clang clang++ llvm-ar lld; do
    if [[ ! -x "$v8_llvm_bin/$llvm_tool" ]]; then
      echo "ThinLTO requires V8's LLVM tool '$v8_llvm_bin/$llvm_tool'." >&2
      exit 1
    fi
  done
  v8_llvm_ranlib="$v8_llvm_bin/llvm-ranlib"
  if [[ ! -x "$v8_llvm_ranlib" ]]; then
    # Chromium omits the redundant multi-call symlink in some toolchain
    # bundles. llvm-ar selects ranlib mode from argv[0]. Keep the link beside
    # clang so CMake's nested IPO capability checks can discover it.
    ln -s "$v8_llvm_bin/llvm-ar" "$v8_llvm_ranlib"
  fi
  # ThinLTO bitcode is versioned. Compile and link the embedding library with
  # the exact Chromium LLVM toolchain that produced the V8 archive.
  cmake_args+=(
    -DCMAKE_C_COMPILER="$v8_llvm_bin/clang"
    -DCMAKE_CXX_COMPILER="$v8_llvm_bin/clang++"
    -DCMAKE_AR="$v8_llvm_bin/llvm-ar"
    -DCMAKE_RANLIB="$v8_llvm_ranlib"
    -DCMAKE_C_COMPILER_AR="$v8_llvm_bin/llvm-ar"
    -DCMAKE_C_COMPILER_RANLIB="$v8_llvm_ranlib"
    -DCMAKE_CXX_COMPILER_AR="$v8_llvm_bin/llvm-ar"
    -DCMAKE_CXX_COMPILER_RANLIB="$v8_llvm_ranlib"
    "-DCMAKE_C_FLAGS=-fuse-ld=lld -Wno-unused-command-line-argument"
    "-DCMAKE_CXX_FLAGS=-fuse-ld=lld -Wno-unused-command-line-argument"
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
    -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld
    -DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld
  )
elif [[ "$expected_kernel" == Linux && "$finalize_only" == false ]]; then
  # Compile the embedding library with the exact Chromium LLVM and libc++
  # revision used for V8. New libc++ headers can require compiler features and
  # configuration defines absent from the builder image's host toolchain.
  target_library_dir="$sysroot/usr/lib/$target_triple"
  target_include_dir="$sysroot/usr/include"
  v8_libcxx_config_include="$v8_root/buildtools/third_party/libc++"
  v8_libcxx_include="$v8_root/third_party/libc++/src/include"
  v8_libcxxabi_include="$v8_root/third_party/libc++abi/src/include"
  v8_libcxx_archive="$v8_output_root/obj/buildtools/third_party/libc++/libc++.a"
  v8_libcxxabi_archive="$v8_output_root/obj/buildtools/third_party/libc++abi/libc++abi.a"
  v8_llvm_root="$v8_root/third_party/llvm-build/Release+Asserts"
  v8_llvm_bin="$v8_llvm_root/bin"
  for target_dependency in \
      "$target_include_dir/openssl/ssl.h" \
      "$target_library_dir/libcrypto.a" \
      "$target_library_dir/libssl.a" \
      "$target_include_dir/zlib.h" \
      "$target_library_dir/libz.a" \
      "$v8_libcxx_config_include/__config_site" \
      "$v8_libcxx_config_include/__assertion_handler" \
      "$v8_libcxx_include/source_location" \
      "$v8_libcxxabi_include/cxxabi.h" \
      "$v8_libcxx_archive" \
      "$v8_libcxxabi_archive" \
      "$v8_llvm_bin/clang" \
      "$v8_llvm_bin/clang++" \
      "$v8_llvm_bin/llvm-ar" \
      "$v8_llvm_bin/ld.lld"; do
    if [[ ! -e "$target_dependency" ]]; then
      echo "Linux sysroot is missing required native dependency '$target_dependency'." >&2
      exit 1
    fi
  done
  for runtime_archive in "$v8_libcxx_archive" "$v8_libcxxabi_archive"; do
    if [[ "$(head -c 7 "$runtime_archive")" != '!<arch>' ]]; then
      echo "Linux V8 C++ runtime dependency is not a self-contained regular archive: '$runtime_archive'." >&2
      exit 1
    fi
  done
  if ! "$v8_llvm_bin/llvm-ar" t "$v8_libcxx_archive" \
      | grep -Eq '(^|/)memory_resource\.o$'; then
    echo "Linux V8 libc++ archive does not provide std::pmr support: '$v8_libcxx_archive'." >&2
    exit 1
  fi
  v8_llvm_ranlib="$v8_llvm_bin/llvm-ranlib"
  if [[ ! -x "$v8_llvm_ranlib" ]]; then
    ln -s "$v8_llvm_bin/llvm-ar" "$v8_llvm_ranlib"
  fi
  cmake_args+=(
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/scripts/linux-glibc-toolchain.cmake"
    -DCMAKE_SYSROOT="$sysroot"
    -DCMAKE_C_COMPILER="$v8_llvm_bin/clang"
    -DCMAKE_CXX_COMPILER="$v8_llvm_bin/clang++"
    -DCMAKE_AR="$v8_llvm_bin/llvm-ar"
    -DCMAKE_RANLIB="$v8_llvm_ranlib"
    -DCMAKE_C_COMPILER_AR="$v8_llvm_bin/llvm-ar"
    -DCMAKE_C_COMPILER_RANLIB="$v8_llvm_ranlib"
    -DCMAKE_CXX_COMPILER_AR="$v8_llvm_bin/llvm-ar"
    -DCMAKE_CXX_COMPILER_RANLIB="$v8_llvm_ranlib"
    -DCMAKE_LINKER="$v8_llvm_bin/ld.lld"
    -DWEBSCENE_LINUX_TARGET_TRIPLE="$target_triple"
    -DWEBSCENE_RUST_TARGET_TRIPLE="$rust_target_triple"
    -DOPENSSL_ROOT_DIR="$sysroot/usr"
    -DOPENSSL_INCLUDE_DIR="$target_include_dir"
    -DOPENSSL_CRYPTO_LIBRARY="$target_library_dir/libcrypto.a"
    -DOPENSSL_SSL_LIBRARY="$target_library_dir/libssl.a"
    -DZLIB_INCLUDE_DIR="$target_include_dir"
    -DZLIB_LIBRARY="$target_library_dir/libz.a"
    -DCMAKE_SKIP_RPATH=TRUE
    "-DCMAKE_C_FLAGS=-ffile-prefix-map=$repo_root=. -fdebug-prefix-map=$repo_root=."
    "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$repo_root=. -fdebug-prefix-map=$repo_root=. -nostdinc++ -nostdlib++ -I$v8_libcxx_config_include -isystem$v8_libcxx_include -isystem$v8_libcxxabi_include -include new -D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS -D_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS -D_LIBCPP_INSTRUMENTED_WITH_ASAN=0 -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"
    "-DCMAKE_CXX_STANDARD_LIBRARIES=$v8_libcxx_archive $v8_libcxxabi_archive -pthread"
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
    "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -Wl,--build-id=sha1"
  )
fi
if [[ "$finalize_only" == false ]]; then
  cmake "${cmake_args[@]}"
  cmake --build "$build_dir" --config "$cmake_build_type" --parallel
  cmake -E copy_if_different "$icu_data" "$build_dir/icudtl.dat"
fi

if [[ "$finalize_only" == true ]]; then
  snapshot_builder="$build_dir/webscene_v8_snapshot_builder"
  if [[ ! -x "$snapshot_builder" ]]; then
    echo "Cross-build output is missing its target snapshot builder: $snapshot_builder" >&2
    exit 1
  fi
  "$snapshot_builder" \
    "$icu_data" \
    "$build_dir/webscene_v8_bootstrap.js" \
    "$build_dir/webscene_bootstrap_snapshot.bin" \
    "$build_dir/webscene_bootstrap_snapshot.meta"
fi
if [[ "$defer_target_execution" == false || "$finalize_only" == true ]]; then
  if [[ "$expected_kernel" == Linux ]]; then
    # Production DSOs intentionally contain no RPATH. Give native test
    # executables an explicit, process-local route to the just-built DSO.
    test_library_path="$build_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    cmake -E env "LD_LIBRARY_PATH=$test_library_path" \
      ctest --test-dir "$build_dir" -C "$cmake_build_type" --output-on-failure
  else
    ctest --test-dir "$build_dir" -C "$cmake_build_type" --output-on-failure
  fi
fi

native_path="$build_dir/$native_name"
if [[ ! -f "$native_path" ]]; then
  echo "Native engine build did not produce '$native_path'." >&2
  exit 1
fi
if [[ "$defer_target_execution" == true && "$finalize_only" == false ]]; then
  echo "Cross-build staged for native finalization: $build_dir"
  exit 0
fi
if [[ "$expected_kernel" == Darwin ]]; then
  actual_macos_deployment_target="$(
    xcrun vtool -show-build "$native_path" |
      awk '$1 == "minos" { print $2; exit }'
  )"
  if [[ "$actual_macos_deployment_target" != "$macos_deployment_target" ]]; then
    echo "Native engine deployment target is '$actual_macos_deployment_target'; expected '$macos_deployment_target'." >&2
    exit 1
  fi
fi
if [[ "$expected_kernel" == Darwin && "$cmake_build_type" == RelWithDebInfo ]]; then
  native_dsym_path="$native_path.dSYM"
  cmake -E remove_directory "$native_dsym_path"
  dsymutil "$native_path" -o "$native_dsym_path"
  if [[ ! -d "$native_dsym_path" ]]; then
    echo "Native engine build did not produce '$native_dsym_path'." >&2
    exit 1
  fi
fi
snapshot_path="$build_dir/webscene_bootstrap_snapshot.bin"
snapshot_metadata_path="$build_dir/webscene_bootstrap_snapshot.meta"
if [[ "$v8_snapshot" == bootstrap \
    && ( ! -f "$snapshot_path" || ! -f "$snapshot_metadata_path" ) ]]; then
  echo "Native engine build did not produce its bootstrap snapshot sidecars." >&2
  exit 1
fi
ixwebsocket_license="$build_dir/_deps/webscene_ixwebsocket-src/LICENSE.txt"
if [[ ! -f "$ixwebsocket_license" ]]; then
  echo "IXWebSocket license was not found at '$ixwebsocket_license'." >&2
  exit 1
fi
if [[ "$expected_kernel" == Linux ]] \
    && readelf -SW "$native_path" 2>&1 | grep -Eq '\.crel(\.|$)'; then
  echo "Native engine output contains unsupported CREL relocation sections: $native_path" >&2
  exit 1
fi

mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
pack_args=(
  "$repo_root/packaging/WebScene.NativeEngine.Runtime/WebScene.NativeEngine.Runtime.csproj"
  -c Release
  -o "$output_dir"
  "-p:WebSceneNativeEngineRid=$rid"
  "-p:WebSceneNativeEnginePath=$native_path"
  "-p:WebSceneNativeEngineIcuDataPath=$icu_data"
  "-p:WebSceneNativeEngineV8LicensePath=$v8_license"
  "-p:WebSceneNativeEngineIcuLicensePath=$icu_license"
  "-p:WebSceneNativeEngineIXWebSocketLicensePath=$ixwebsocket_license"
  "-p:WebSceneNativeEngineV8PointerCompression=true"
  "-p:WebSceneNativeEngineV8SharedCage=true"
  "-p:WebSceneNativeEngineV8OptimizeForSizeDefault=true"
  "-p:WebSceneNativeEngineV8PartitionAlloc=$partition_alloc"
  "-p:WebSceneNativeEngineV8Inspector=true"
  "-p:WebSceneNativeEngineDenseLink=true"
  "-p:WebSceneNativeEngineThinLto=$thin_lto"
  "-p:WebSceneNativeEngineV8Revision=$v8_revision"
  "-p:WebSceneNativeEngineHtmlParser=$html_parser"
  "-p:WebSceneNativeEngineCssParser=$css_parser"
  "-p:WebSceneNativeEngineSelectorParser=$selector_parser"
  "-p:WebSceneNativeEngineDomBindings=$dom_bindings"
  "-p:WebSceneNativeEngineV8Snapshot=$v8_snapshot"
  "-p:WebSceneNativeEngineConfiguration=$cmake_build_type"
  "-p:WebSceneNativeEngineBuilderIdentity=$builder_identity"
  "-p:WebSceneNativeEngineTargetTriple=$target_triple"
  "-p:WebSceneNativeEngineGlibcBaseline=$glibc_baseline"
)
if [[ "$v8_snapshot" == bootstrap ]]; then
  pack_args+=(
    "-p:WebSceneNativeEngineSnapshotPath=$snapshot_path"
    "-p:WebSceneNativeEngineSnapshotMetadataPath=$snapshot_metadata_path")
fi
if [[ "$html_parser" == html5ever ]]; then
  pack_args+=(
    "-p:WebSceneNativeEngineHtmlParserNoticesPath=$repo_root/experiments/WebScene.NativeEngine.Probe/native/html_parser/THIRD-PARTY-NOTICES.md")
fi
pack_args+=("-p:PackageVersion=$package_version")
dotnet pack "${pack_args[@]}"

package_path="$output_dir/WebScene.NativeEngine.Runtime.$rid.$package_version.nupkg"
if [[ ! -f "$package_path" ]]; then
  echo "The RID package was not produced in '$output_dir'." >&2
  exit 1
fi
package_smoke_dir="$build_dir/package-smoke"
cmake -E remove_directory "$package_smoke_dir"
cmake -E make_directory "$package_smoke_dir"
(cd "$package_smoke_dir" && cmake -E tar xf "$package_path")
package_native_path="$package_smoke_dir/runtimes/$rid/native/$native_name"

WEBSCENE_VARIABLE_FONT_INSTANCING=1 dotnet run \
  --project "$repo_root/tests/WebPlatformSubset/runner/WebScene.WebPlatformSubset.Runner.csproj" \
  -c Release -f net10.0 -- \
  --selection required \
  --native-library "$package_native_path" \
  --native-cache-directory "$build_dir/code-cache" \
  --output "$build_dir/wpt-results"

WEBSCENE_TEST_NATIVE_LIBRARY="$package_native_path" \
  WEBSCENE_VARIABLE_FONT_INSTANCING=1 \
  dotnet test "$repo_root/tests/WebScene.Backend.Avalonia.Tests/WebScene.Backend.Avalonia.Tests.csproj" \
    -c Release -f net10.0 \
    --filter 'FullyQualifiedName~NativeWebFontCacheTests|FullyQualifiedName~VariableWebFontTests|FullyQualifiedName~SvgPictureRenderingTests'

WEBSCENE_NATIVE_ENGINE_PATH="$package_native_path" \
  dotnet run \
    --project "$repo_root/benchmarks/WebScene.NativeEngine.Benchmarks/WebScene.NativeEngine.Benchmarks.csproj" \
    -c Release -- \
    probe native-interop-race --batches 100 --width 32

consumer_smoke_root="$repo_root/artifacts/native-engine-consumer-smoke"
mkdir -p "$consumer_smoke_root"
consumer_root="$(mktemp -d "$consumer_smoke_root/consumer.XXXXXX")"
consumer_dir="$consumer_root/consumer"
consumer_nuget_config="$consumer_root/nuget.config"
cmake -E copy_if_different \
  "$repo_root/packaging/WebScene.NativeEngine.Runtime/ConsumerSmoke.Directory.Packages.props" \
  "$consumer_root/Directory.Packages.props"
consumer_framework=net10.0
if dotnet --list-sdks | grep -Eq '^8\.'; then
  consumer_framework=net8.0
fi
dotnet new nugetconfig --force --output "$consumer_root"
dotnet nuget add source "$output_dir" \
  --name local-release \
  --configfile "$consumer_nuget_config"
dotnet new console --framework "$consumer_framework" --no-restore --output "$consumer_dir"
NUGET_PACKAGES="$consumer_root/packages" dotnet add "$consumer_dir/consumer.csproj" package \
  "WebScene.NativeEngine.Runtime.$rid" \
  --version "$package_version" \
  --no-restore
NUGET_PACKAGES="$consumer_root/packages" dotnet restore \
  "$consumer_dir/consumer.csproj" -r "$rid" \
  --configfile "$consumer_nuget_config"
NUGET_PACKAGES="$consumer_root/packages" dotnet build \
  "$consumer_dir/consumer.csproj" -c Release -r "$rid" --no-restore
copied_assets=("$native_name" icudtl.dat webscene-native-runtime.json)
if [[ "$v8_snapshot" == bootstrap ]]; then
  copied_assets+=(webscene_bootstrap_snapshot.bin webscene_bootstrap_snapshot.meta)
fi
for copied_asset in "${copied_assets[@]}"; do
  copied_path="$consumer_dir/bin/Release/$consumer_framework/$rid/$copied_asset"
  if [[ ! -f "$copied_path" ]]; then
    echo "The runtime package did not copy '$copied_asset' to consumer output." >&2
    exit 1
  fi
done

echo "Native runtime: $native_path"
if [[ -n "${native_dsym_path:-}" ]]; then
  echo "Native symbols: $native_dsym_path"
fi
echo "RID package: $package_path"
