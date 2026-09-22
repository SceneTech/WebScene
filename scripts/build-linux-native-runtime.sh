#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lock_file="$repo_root/packaging/WebScene.NativeEngine.Runtime/linux-build-lock.json"
dockerfile="$repo_root/packaging/WebScene.NativeEngine.Runtime/Dockerfile.linux-glibc"
rid=
package_version=
output_dir="$repo_root/artifacts/nuget-packages"
stage=build
builder_image=

usage() {
  echo "Usage: $0 --rid linux-x64|linux-arm64 [--package-version VERSION] [--output DIR] [--stage build|finalize] [--builder-image IMAGE@sha256:DIGEST]" >&2
}

while (($# > 0)); do
  case "$1" in
    --rid) rid="${2:-}"; shift 2 ;;
    --package-version) package_version="${2:-}"; shift 2 ;;
    --output) output_dir="${2:-}"; shift 2 ;;
    --stage) stage="${2:-}"; shift 2 ;;
    --builder-image) builder_image="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

case "$rid" in
  linux-x64|linux-arm64) ;;
  *) usage; exit 1 ;;
esac
case "$stage" in
  build|finalize) ;;
  *) usage; exit 1 ;;
esac

IFS='|' read -r builder_identity target_triple rust_target_triple sysroot max_glibc max_glibcxx max_cxxabi <<< "$(python3 - "$lock_file" "$rid" <<'PY'
import json, pathlib, sys
lock = json.loads(pathlib.Path(sys.argv[1]).read_text())
target = lock["sysroots"][sys.argv[2]]
print("|".join((
    lock["builderIdentity"], target["targetTriple"], target["rustTargetTriple"], target["path"],
    lock["compatibility"]["maximumGlibc"],
    lock["compatibility"]["maximumGlibcxx"],
    lock["compatibility"]["maximumCxxabi"],
)))
PY
)"
cargo_target_key="$(printf '%s' "$rust_target_triple" | tr '[:lower:]-' '[:upper:]_')"
cargo_linker_name="CARGO_TARGET_${cargo_target_key}_LINKER"
cargo_rustflags_name="CARGO_TARGET_${cargo_target_key}_RUSTFLAGS"
source_date_epoch="$(git -C "$repo_root" show -s --format=%ct HEAD)"
git_common_dir="$(git -C "$repo_root" rev-parse --path-format=absolute --git-common-dir)"
docker_mount_args=(--volume "$repo_root:/workspace")
if [[ "$git_common_dir" != "$repo_root/.git" ]]; then
  docker_mount_args+=(--volume "$git_common_dir:$git_common_dir:ro")
fi

if [[ "$stage" == finalize ]]; then
  "$repo_root/scripts/build-native-engine-runtime.sh" \
    --rid "$rid" \
    --target-triple "$target_triple" \
    --rust-target-triple "$rust_target_triple" \
    --sysroot "$sysroot" \
    --builder-identity "$builder_identity" \
    --glibc-baseline "$max_glibc" \
    --package-version "$package_version" \
    --partition-alloc \
    --upstream-v8 \
    --output "$output_dir" \
    --finalize-only
  exit 0
fi

if [[ -z "$builder_image" ]]; then
  builder_image="webscene-linux-builder:$builder_identity"
  docker build \
    --platform linux/amd64 \
    --file "$dockerfile" \
    --tag "$builder_image" \
    "$repo_root/packaging/WebScene.NativeEngine.Runtime"
elif [[ "$builder_image" != *@sha256:* ]]; then
  echo "A prebuilt builder image must be pinned by digest: $builder_image" >&2
  exit 1
fi

common_args=(
  --rid "$rid"
  --target-triple "$target_triple"
  --rust-target-triple "$rust_target_triple"
  --sysroot "$sysroot"
  --builder-identity "$builder_identity"
  --glibc-baseline "$max_glibc"
  --package-version "$package_version"
  --partition-alloc
  --upstream-v8
  --output /workspace/artifacts/nuget-packages
)
case "$rid" in
  linux-x64) v8_cpu=x64 ;;
  linux-arm64) v8_cpu=arm64 ;;
esac
v8_root_host="$repo_root/artifacts/native-engine-v8/$rid/v8"
if [[ -f "$v8_root_host/out/$v8_cpu/ReleasePartitionAlloc/obj/libv8_monolith.a" ]]; then
  common_args+=(--v8-root "/workspace/artifacts/native-engine-v8/$rid/v8")
fi
if [[ "$stage" == build && "$rid" == linux-arm64 ]]; then
  common_args+=(--defer-target-execution)
fi

docker run --rm \
  --platform linux/amd64 \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp/webscene-home \
  --env DOTNET_CLI_HOME=/tmp/webscene-home \
  --env CARGO_HOME=/tmp/webscene-home/.cargo \
  --env NUGET_PACKAGES=/tmp/webscene-home/.nuget/packages \
  --env "SOURCE_DATE_EPOCH=$source_date_epoch" \
  --env "CARGO_BUILD_TARGET=$rust_target_triple" \
  --env "$cargo_linker_name=clang" \
  --env "$cargo_rustflags_name=-C link-arg=--target=$target_triple -C link-arg=--sysroot=$sysroot --remap-path-prefix=/workspace=." \
  "${docker_mount_args[@]}" \
  --workdir /workspace \
  "$builder_image" \
  scripts/build-native-engine-runtime.sh "${common_args[@]}"

native_path="$(find "$repo_root/artifacts/native-engine-runtime-build" -path "*/$rid*/libwebscene_native_engine.so" -print -quit)"
if [[ -z "$native_path" ]]; then
  echo "Unable to locate the $rid native library for ABI verification." >&2
  exit 1
fi
python3 "$repo_root/scripts/verify-linux-native-abi.py" "$native_path" \
  --rid "$rid" \
  --max-glibc "$max_glibc" \
  --max-glibcxx "$max_glibcxx" \
  --max-cxxabi "$max_cxxabi" \
  --output "${native_path%/*}/$rid-abi.json"
