#!/usr/bin/env bash
set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
v8_root="$repo_root/artifacts/native-engine-v8/linux-x64/v8"
html_parser=html5ever
css_parser=cssparser
selector_parser=servo
dom_bindings=generated
v8_snapshot=bootstrap
cmake_build_type=Release
thin_lto=false
disable_wasm=false
partition_alloc=false
graphics_sdk=
arguments=("$@")
for ((index = 0; index < ${#arguments[@]}; ++index)); do
  argument="${arguments[index]}"
  case "$argument" in
    --v8-root) v8_root="${arguments[++index]:-}" ;;
    --html-parser) html_parser="${arguments[++index]:-}" ;;
    --css-parser) css_parser="${arguments[++index]:-}" ;;
    --selector-parser) selector_parser="${arguments[++index]:-}" ;;
    --dom-bindings) dom_bindings="${arguments[++index]:-}" ;;
    --v8-snapshot) v8_snapshot="${arguments[++index]:-}" ;;
    --cmake-build-type) cmake_build_type="${arguments[++index]:-}" ;;
    --graphics-sdk) graphics_sdk="${arguments[++index]:-}" ;;
    --thin-lto) thin_lto=true ;;
    --disable-wasm) disable_wasm=true ;;
    --partition-alloc) partition_alloc=true ;;
  esac
done
build_variant="-$html_parser-$css_parser-$selector_parser-$dom_bindings-$v8_snapshot"
v8_configuration=Release
if [[ -n "$graphics_sdk" ]]; then
  build_variant+=-graphics
fi
if [[ "$cmake_build_type" == RelWithDebInfo ]]; then
  build_variant+=-symbols
fi
if [[ "$thin_lto" == true ]]; then
  build_variant+=-thinlto-llvm
  v8_configuration=ReleaseThinLto
fi
if [[ "$disable_wasm" == true ]]; then
  build_variant+=-no-wasm
  v8_configuration+=NoWasm
fi
if [[ "$partition_alloc" == true ]]; then
  build_variant+=-partitionalloc
  v8_configuration+=PartitionAlloc
fi
build_variant+=-inspector
build_dir="$repo_root/artifacts/native-engine-runtime-build/linux-x64$build_variant"

set +e
"$repo_root/scripts/build-native-engine-runtime.sh" "$@"
package_status=$?

native_test_status=0
icu_data="$v8_root/out/x64/$v8_configuration/icudtl.dat"
if [[ -d "$build_dir" ]]; then
  if [[ -f "$icu_data" ]]; then
    cmake -E copy_if_different "$icu_data" "$build_dir/icudtl.dat"
  fi
  ctest --test-dir "$build_dir" -C Release --output-on-failure
  native_test_status=$?

  if ((native_test_status != 0)) && [[ -x "$build_dir/webscene_native_engine_tests" ]]; then
    gdb \
      --batch \
      -ex "set pagination off" \
      -ex run \
      -ex "thread apply all bt" \
      --args "$build_dir/webscene_native_engine_tests" || true
  fi
fi
set -e

if ((package_status != 0)); then
  exit "$package_status"
fi
exit "$native_test_status"
