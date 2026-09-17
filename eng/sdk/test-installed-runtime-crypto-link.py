#!/usr/bin/env python3
"""Link a tiny installed-package Runtime consumer through its crypto closure."""

from __future__ import annotations

import argparse
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
REQUIRED_SYMBOLS = (
    "mbedtls_gcm_crypt_and_tag",
    "mbedtls_aes_crypt_cbc",
    "mbedtls_md_hmac",
)


def run(*arguments: str, cwd: Path | None = None) -> str:
    result = subprocess.run(
        arguments,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode:
        output = result.stdout[-16_384:]
        raise RuntimeError(f"{' '.join(arguments)} failed ({result.returncode}):\n{output}")
    return result.stdout


def write(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value)


def compile_object(compiler: str, source: Path, output: Path) -> None:
    run(compiler, "-c", str(source), "-o", str(output))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "src/WebScene.Sdk/cmake/WebSceneConfig.cmake",
    )
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument(
        "--sdk",
        type=Path,
        help="Use real installed libraries and headers while testing the source package config",
    )
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("the installed macOS package link contract requires macOS")
    config = args.config.resolve()
    if not config.is_file():
        parser.error(f"missing package config: {config}")
    arch = platform.machine()
    if arch not in ("arm64", "aarch64"):
        parser.error(f"the installed SDK contract requires ARM64, found {arch}")

    with tempfile.TemporaryDirectory(prefix="webscene-runtime-crypto-link-") as temporary:
        root = Path(temporary)
        prefix = root / "sdk"
        cmake_dir = prefix / "lib/cmake/WebScene"
        include = prefix / "include"
        libraries = prefix / "lib"
        source = root / "sources"
        consumer = root / "consumer"
        build = root / "build"
        for directory in (cmake_dir, source, consumer):
            directory.mkdir(parents=True, exist_ok=True)
        if args.sdk:
            installed = args.sdk.resolve()
            installed_config = installed / "lib/cmake/WebScene"
            if not (installed / "include").is_dir() or not installed_config.is_dir():
                parser.error(f"incomplete installed SDK: {installed}")
            include.symlink_to(installed / "include", target_is_directory=True)
            for item in (installed / "lib").iterdir():
                if item.is_file():
                    (libraries / item.name).symlink_to(item)
            shutil.copytree(installed_config, cmake_dir, dirs_exist_ok=True)
        else:
            (include / "graphics").mkdir(parents=True)
            write(
                cmake_dir / "WebSceneMacOSProfile.cmake",
                "function(webscene_require_macos_profile)\nendfunction()\n",
            )
            write(cmake_dir / "WebSceneApplication.cmake", "")
            write(cmake_dir / "WebScenePrecompiledJavaScript.cmake", "")
            write(
                include / "webscene_native_engine.h",
                "#pragma once\n"
                "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
                "void* webscene_engine_create(unsigned);\n"
                "void webscene_engine_destroy(void*);\n"
                "#ifdef __cplusplus\n}\n#endif\n",
            )
            write(
                source / "runtime.c",
                "extern void mbedtls_gcm_crypt_and_tag(void);\n"
                "extern void mbedtls_aes_crypt_cbc(void);\n"
                "extern void mbedtls_md_hmac(void);\n"
                "void* webscene_engine_create(unsigned count) {\n"
                "  mbedtls_gcm_crypt_and_tag();\n"
                "  mbedtls_aes_crypt_cbc();\n"
                "  mbedtls_md_hmac();\n"
                "  return count ? (void*)2 : (void*)1;\n"
                "}\n"
                "void webscene_engine_destroy(void* engine) { (void)engine; }\n",
            )
            write(
                source / "crypto.c",
                "void mbedtls_gcm_crypt_and_tag(void) {}\n"
                "void mbedtls_aes_crypt_cbc(void) {}\n"
                "void mbedtls_md_hmac(void) {}\n",
            )
            write(source / "empty.c", "void webscene_empty_archive_member(void) {}\n")
            runtime_object = root / "runtime.o"
            crypto_object = root / "crypto.o"
            empty_object = root / "empty.o"
            compile_object(args.compiler, source / "runtime.c", runtime_object)
            compile_object(args.compiler, source / "crypto.c", crypto_object)
            compile_object(args.compiler, source / "empty.c", empty_object)
            archiver = shutil.which("ar")
            if not archiver:
                raise RuntimeError("ar is required")
            run(archiver, "rcs", str(libraries / "libwebscene_native_engine.a"), str(runtime_object))
            run(archiver, "rcs", str(libraries / "libmbedcrypto.a"), str(crypto_object))
            for name in (
                "libwebscene_core.a",
                "libwebscene_media.a",
                "libixwebsocket.a",
                "libwebscene_html_parser.a",
                "libwebscene_css_selector_parser.a",
                "libv8_monolith.a",
            ):
                run(archiver, "rcs", str(libraries / name), str(empty_object))
            run(
                args.compiler,
                "-dynamiclib",
                str(source / "empty.c"),
                "-o",
                str(libraries / "libwebgpu_dawn.dylib"),
            )
        shutil.copy2(config, cmake_dir / "WebSceneConfig.cmake")

        undefined = run("nm", "-u", str(libraries / "libwebscene_native_engine.a"))
        missing = [symbol for symbol in REQUIRED_SYMBOLS if symbol not in undefined]
        if missing:
            raise RuntimeError(f"runtime fixture does not require {missing}")

        write(
            consumer / "CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.28)\n"
            "project(WebSceneInstalledRuntimeCryptoLink LANGUAGES CXX)\n"
            "find_package(WebScene REQUIRED CONFIG)\n"
            "add_executable(installed_runtime_crypto main.cpp)\n"
            "target_link_libraries(installed_runtime_crypto PRIVATE WebScene::Runtime)\n",
        )
        write(
            consumer / "main.cpp",
            "#include <webscene_native_engine.h>\n"
            "int main() {\n"
            "  auto* engine = webscene_engine_create(0);\n"
            "  webscene_engine_destroy(engine);\n"
            "  return engine ? 0 : 1;\n"
            "}\n",
        )
        run(
            "cmake",
            "-S",
            str(consumer),
            "-B",
            str(build),
            "-G",
            args.generator,
            f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DCMAKE_CXX_COMPILER={args.compiler}",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0",
        )
        link_output = run("cmake", "--build", str(build), "--verbose")
        link_evidence = link_output + (build / "build.ninja").read_text(errors="replace")
        runtime_index = link_evidence.rfind("libwebscene_native_engine.a")
        crypto_index = link_evidence.rfind("libmbedcrypto.a")
        if runtime_index < 0 or crypto_index < runtime_index:
            raise RuntimeError("generated link closure does not place libmbedcrypto.a after Runtime")
        print(
            "Installed WebScene::Runtime consumer linked AES-GCM, AES-CBC, and HMAC "
            "through libmbedcrypto.a"
        )


if __name__ == "__main__":
    main()
