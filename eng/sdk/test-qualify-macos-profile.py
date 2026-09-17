#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import shutil
import subprocess
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "src/WebScene.Sdk/tools/qualify_macos_profile.py"
SPEC = importlib.util.spec_from_file_location("qualify_macos_profile", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class MacOSProfileContractTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cmake"), "CMake is required")
    def test_custom_macos_compiler_root_is_forwarded_to_try_compile(self):
        # Exercise the actual macOS branch portably. This tests root propagation,
        # not binary qualification (covered separately by the locked profile).
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copyfile(ROOT / "src/WebScene.Sdk/cmake/WebSceneToolchain.cmake",
                            root / "WebSceneToolchain.cmake")
            (root / "WebSceneMacOSProfile.cmake").write_text("""
function(webscene_validate_macos_compiler compiler)
  if(NOT compiler STREQUAL "${WEBSCENE_LLVM_ROOT}/bin/clang++")
    message(FATAL_ERROR "wrong compiler")
  endif()
endfunction()
""")
            (root / "check.cmake").write_text("""
cmake_minimum_required(VERSION 3.28)
set(CMAKE_HOST_SYSTEM_NAME Darwin)
set(WEBSCENE_LLVM_ROOT "custom-pinned-llvm")
set(CMAKE_OSX_SYSROOT "test-sdk")
include("${CMAKE_CURRENT_LIST_DIR}/WebSceneToolchain.cmake")
if(NOT "WEBSCENE_LLVM_ROOT" IN_LIST CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
  message(FATAL_ERROR "custom compiler root would be lost by try_compile")
endif()
foreach(compiler CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_OBJCXX_COMPILER)
  if(NOT "${${compiler}}" MATCHES "^custom-pinned-llvm/bin/clang")
    message(FATAL_ERROR "${compiler} ignored the selected root")
  endif()
endforeach()
""")
            result = subprocess.run(["cmake", "-P", str(root / "check.cmake")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repository_profile_is_complete_and_locked(self):
        profile = MODULE.read_profile(
            ROOT / "src/WebScene.Sdk/cmake/WebSceneMacOSProfile.cmake")
        self.assertEqual(set(profile), MODULE.PROFILE_KEYS)
        self.assertEqual(profile["WebScene_MACOS_COMPILER_VERSION"], "22.1.8")
        self.assertEqual(profile["WebScene_MACOS_CXX_RUNTIME"], "/usr/lib/libc++.1.dylib")
        self.assertEqual(profile["WebScene_MACOS_DEPLOYMENT_TARGET"], "26.0")
        for key in (
            "WebScene_MACOS_COMPILER_SHA256",
            "WebScene_MACOS_COMPILER_CONFIG_SHA256",
            "WebScene_MACOS_LIBCXX_HEADERS_SHA256",
            "WebScene_MACOS_CLANG_HEADERS_SHA256",
        ):
            self.assertRegex(profile[key], r"^[0-9a-f]{64}$")

    def test_tree_digest_binds_relative_paths_and_content(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "a").mkdir()
            (root / "a/header.h").write_text("first")
            first, count = MODULE.tree_digest(root)
            self.assertEqual(count, 1)
            (root / "a/header.h").write_text("second")
            second, _ = MODULE.tree_digest(root)
            self.assertNotEqual(first, second)
            (root / "a/header.h").rename(root / "renamed.h")
            renamed, _ = MODULE.tree_digest(root)
            self.assertNotEqual(second, renamed)

    def test_otool_parsers_preserve_loader_contracts(self):
        load_output = """consumer:
\t@rpath/libwebgpu_dawn.dylib (compatibility version 0.0.0, current version 0.0.0)
\t/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 2100.43.0)
"""
        rpath_output = """Load command 1
          cmd LC_RPATH
      cmdsize 48
         path @loader_path/../lib (offset 12)
"""
        with mock.patch.object(MODULE, "run", side_effect=[load_output, rpath_output]):
            self.assertEqual(MODULE.dependencies(Path("consumer")), [
                "@rpath/libwebgpu_dawn.dylib", "/usr/lib/libc++.1.dylib"])
            self.assertEqual(MODULE.rpaths(Path("consumer")), ["@loader_path/../lib"])


if __name__ == "__main__":
    unittest.main()
