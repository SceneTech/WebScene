#!/usr/bin/env python3
"""Qualify the installed macOS SDK compiler and system-libc++ closure."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import tempfile


PROFILE_KEYS = {
    "WebScene_MACOS_PROFILE_SCHEMA",
    "WebScene_MACOS_PROFILE_NAME",
    "WebScene_MACOS_COMPILER_ID",
    "WebScene_MACOS_COMPILER_VERSION",
    "WebScene_MACOS_COMPILER_SHA256",
    "WebScene_MACOS_COMPILER_CONFIG_SHA256",
    "WebScene_MACOS_LIBCXX_HEADERS_SHA256",
    "WebScene_MACOS_CLANG_HEADERS_SHA256",
    "WebScene_MACOS_ARCHITECTURE",
    "WebScene_MACOS_DEPLOYMENT_TARGET",
    "WebScene_MACOS_CXX_STANDARD",
    "WebScene_MACOS_CXX_LIBRARY",
    "WebScene_MACOS_CXX_RUNTIME",
    "WebScene_MACOS_SYSTEM_RUNTIME",
}


def run(*command: object, cwd: Path | None = None, env: dict[str, str] | None = None) -> str:
    return subprocess.check_output(
        [str(item) for item in command], cwd=cwd, env=env, text=True,
        stderr=subprocess.STDOUT).strip()


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def tree_digest(root: Path) -> tuple[str, int]:
    value = hashlib.sha256()
    count = 0
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        value.update(path.relative_to(root).as_posix().encode())
        value.update(b"\0")
        value.update(bytes.fromhex(digest(path)))
        value.update(b"\0")
        count += 1
    return value.hexdigest(), count


def read_profile(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    pattern = re.compile(r'^set\((WebScene_MACOS_[A-Z0-9_]+)\s+"?([^"\)]+)"?\)$')
    for line in path.read_text().splitlines():
        match = pattern.match(line.strip())
        if match and match.group(1) in PROFILE_KEYS:
            result[match.group(1)] = match.group(2)
    missing = PROFILE_KEYS - result.keys()
    if missing:
        raise RuntimeError(f"Incomplete macOS profile {path}: {sorted(missing)}")
    return result


def dependencies(path: Path) -> list[str]:
    output = run("otool", "-L", path)
    return [line.strip().split(" (", 1)[0] for line in output.splitlines()[1:]]


def rpaths(path: Path) -> list[str]:
    output = run("otool", "-l", path)
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", output)


def make_read_only(root: Path) -> None:
    for path in [*root.rglob("*"), root]:
        mode = path.lstat().st_mode
        if not path.is_symlink():
            path.chmod(mode & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH))


def write_consumer(root: Path) -> None:
    (root / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 3.28)
project(WebSceneMacOSProfileConsumer LANGUAGES CXX)
find_package(WebScene CONFIG REQUIRED)
add_executable(webscene_macos_profile_consumer main.cpp)
target_link_libraries(webscene_macos_profile_consumer PRIVATE WebScene::NativeWeb WebScene::WebGPU)
""")
    (root / "main.cpp").write_text("""#include <webscene/native_web.hpp>
#include <iostream>
int main() {
  webscene::native_web::document document;
  const auto node = document.element(document.body(), "div");
  document.attribute(node, "id", "relocated-profile");
  const bool passed = document.find("relocated-profile") == node;
  document.dispose();
  std::cout << (passed ? "macos-profile-pass" : "macos-profile-fail") << '\\n';
  return passed ? 0 : 1;
}
""")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--llvm-root", type=Path, default=Path("/opt/homebrew/opt/llvm"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--producer-revision", required=True)
    parser.add_argument("--payload-provenance", required=True)
    args = parser.parse_args()

    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise RuntimeError("The qualified profile requires macOS ARM64")
    sdk = args.sdk.resolve()
    llvm = args.llvm_root.resolve()
    profile_path = sdk / "lib/cmake/WebScene/WebSceneMacOSProfile.cmake"
    profile = read_profile(profile_path)
    compiler = llvm / "bin/clang++"
    compiler_output = run(compiler, "--version")
    if compiler_output.splitlines()[0] != f"Homebrew clang version {profile['WebScene_MACOS_COMPILER_VERSION']}":
        raise RuntimeError(f"Unexpected compiler identity: {compiler_output.splitlines()[0]}")
    if digest(compiler) != profile["WebScene_MACOS_COMPILER_SHA256"]:
        raise RuntimeError("Compiler SHA-256 does not match the installed profile")

    config_match = re.search(r"^Configuration file: (.+)$", compiler_output, re.MULTILINE)
    if not config_match:
        raise RuntimeError("Compiler did not report its Homebrew configuration file")
    compiler_config = Path(config_match.group(1))
    if digest(compiler_config) != profile["WebScene_MACOS_COMPILER_CONFIG_SHA256"]:
        raise RuntimeError("Compiler configuration SHA-256 does not match the installed profile")
    libcxx_headers = llvm / "include/c++/v1"
    clang_headers = Path(run(compiler, "-print-resource-dir")) / "include"
    libcxx_hash, libcxx_files = tree_digest(libcxx_headers)
    clang_hash, clang_files = tree_digest(clang_headers)
    if libcxx_hash != profile["WebScene_MACOS_LIBCXX_HEADERS_SHA256"]:
        raise RuntimeError("Homebrew libc++ header tree does not match the installed profile")
    if clang_hash != profile["WebScene_MACOS_CLANG_HEADERS_SHA256"]:
        raise RuntimeError("Clang resource header tree does not match the installed profile")

    with tempfile.TemporaryDirectory(prefix="webscene-macos-profile-") as temporary:
        temporary_root = Path(temporary)
        relocated_sdk = temporary_root / "relocated/read-only/sdk"
        shutil.copytree(sdk, relocated_sdk, symlinks=True)
        make_read_only(relocated_sdk)
        source = temporary_root / "consumer-source"
        source.mkdir()
        write_consumer(source)
        build = temporary_root / "consumer-build"
        run("cmake", "-S", source, "-B", build, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_PREFIX_PATH={relocated_sdk}",
            f"-DCMAKE_TOOLCHAIN_FILE={relocated_sdk}/lib/cmake/WebScene/WebSceneToolchain.cmake",
            f"-DWEBSCENE_LLVM_ROOT={llvm}")
        run("cmake", "--build", build, "--parallel", "2")
        executable = build / "webscene_macos_profile_consumer"
        unrelated_cwd = temporary_root / "unrelated-cwd"
        unrelated_cwd.mkdir()
        execution = run(executable, cwd=unrelated_cwd)
        if execution != "macos-profile-pass":
            raise RuntimeError(f"Installed consumer returned unexpected output: {execution}")

        audited = [executable]
        audited.extend(path for path in [relocated_sdk / "bin/webscene-uic",
                                         relocated_sdk / "bin/webscene-jsc"] if path.exists())
        audited.extend(sorted((relocated_sdk / "lib").glob("*.dylib")))
        binaries = []
        for binary in audited:
            linked = dependencies(binary)
            paths = rpaths(binary)
            if profile["WebScene_MACOS_CXX_RUNTIME"] not in linked:
                raise RuntimeError(f"{binary.name} does not use the system libc++ runtime")
            for item in linked:
                if item.startswith(("/opt/homebrew/", "/usr/local/", "/private/")):
                    raise RuntimeError(f"{binary.name} has a non-system absolute dependency: {item}")
            for item in paths:
                if str(sdk) in item or item.startswith(("/opt/homebrew/", "/usr/local/")):
                    raise RuntimeError(f"{binary.name} has a producer/toolchain rpath: {item}")
            binaries.append({
                "path": binary.relative_to(temporary_root).as_posix(),
                "sha256": digest(binary),
                "dependencies": linked,
                "rpaths": paths,
            })

        dyld_env = dict(os.environ, DYLD_PRINT_LIBRARIES="1")
        dyld_output = run(executable, cwd=unrelated_cwd, env=dyld_env)
        expected_dawn = str(relocated_sdk / "lib/libwebgpu_dawn.dylib")
        if expected_dawn not in dyld_output:
            raise RuntimeError("dyld did not load Dawn from the relocated SDK")

        manifest_path = relocated_sdk / "sdk-manifest.json"
        manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else None
        evidence = {
            "schemaVersion": 1,
            "result": "passed",
            "producerRevision": args.producer_revision,
            "payloadProvenance": args.payload_provenance,
            "profile": profile,
            "profileSha256": digest(profile_path),
            "compiler": {
                "path": str(compiler),
                "sha256": digest(compiler),
                "version": compiler_output,
                "configuration": str(compiler_config),
                "configurationSha256": digest(compiler_config),
                "libcxxHeaders": {"path": str(libcxx_headers), "files": libcxx_files,
                                   "sha256": libcxx_hash},
                "clangHeaders": {"path": str(clang_headers), "files": clang_files,
                                  "sha256": clang_hash},
            },
            "macOSSDK": {
                "path": run("xcrun", "--sdk", "macosx", "--show-sdk-path"),
                "version": run("xcrun", "--sdk", "macosx", "--show-sdk-version"),
                "buildVersion": run("xcrun", "--sdk", "macosx", "--show-sdk-build-version"),
            },
            "relocation": {
                "sdkWasReadOnly": True,
                "consumerSourceOutsideProducer": True,
                "executionCwdOutsideSDK": True,
                "output": execution,
                "dyldLoadedRelocatedDawn": expected_dawn,
            },
            "sdkManifestSources": manifest.get("sources") if manifest else None,
            "binaries": binaries,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"macOS compiler/runtime profile passed: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
