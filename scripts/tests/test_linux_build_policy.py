from __future__ import annotations

import hashlib
import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PACKAGING = ROOT / "packaging" / "WebScene.NativeEngine.Runtime"


class LinuxBuildPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.lock = json.loads((PACKAGING / "linux-build-lock.json").read_text())
        cls.dockerfile = (PACKAGING / "Dockerfile.linux-glibc").read_text()
        cls.workflow = (ROOT / ".github/workflows/native-runtime-packages.yml").read_text()
        cls.build_script = (ROOT / "scripts/build-native-engine-runtime.sh").read_text()
        cls.toolchain = (ROOT / "scripts/linux-glibc-toolchain.cmake").read_text()

    def test_all_container_inputs_are_digest_pinned(self) -> None:
        from_lines = re.findall(r"^FROM\s+(\S+)", self.dockerfile, re.MULTILINE)
        external = [value for value in from_lines if value not in {"x64-sysroot"}]
        self.assertTrue(external)
        self.assertTrue(all("@sha256:" in value for value in external), external)
        self.assertNotIn("apt-get", self.dockerfile)

    def test_lock_and_dockerfile_are_synchronized(self) -> None:
        expected = [self.lock["dotnetSdk"], *self.lock["sysroots"].values()]
        for item in expected:
            image = item.get("image", item.get("sourceImage"))
            self.assertIsNotNone(image)
            self.assertIn(f'{image}@{item["digest"]}', self.dockerfile)
        toolchain = self.lock["toolchain"]
        for value in (
            toolchain["rust"], toolchain["rustArchiveSha256"],
            toolchain["rustArm64StdSha256"], toolchain["depotToolsCommit"],
        ):
            self.assertIn(value, self.dockerfile)

        for patch in self.lock["patches"].values():
            patch_path = PACKAGING / patch["path"]
            self.assertTrue(patch_path.is_file(), patch_path)
            self.assertEqual(
                patch["sha256"],
                hashlib.sha256(patch_path.read_bytes()).hexdigest(),
            )

        for key in ("rustMacArm64ArchiveSha256", "rustMacX64StdSha256"):
            self.assertIn(toolchain[key], self.build_script)

    def test_release_matrix_contains_both_glibc_rids(self) -> None:
        for rid in ("linux-x64", "linux-arm64"):
            self.assertIn(f"rid: {rid}", self.workflow)
            self.assertIn(f"--expected-rid {rid}", self.workflow)
            self.assertIn(f"--native-rid {rid}", self.workflow)
        self.assertIn("github.ref_type != 'tag'", self.workflow)

    def test_linux_libcxx_cache_paths_do_not_invalidate_macos_caches(self) -> None:
        self.assertEqual(2, self.workflow.count("v8_cache_extra_paths: |"))
        self.assertEqual(3, self.workflow.count("v8_cache_extra_paths: ''"))
        self.assertEqual(2, self.workflow.count("${{ matrix.v8_cache_extra_paths }}"))

    def test_arm64_disables_memory_tagging_for_glibc_227(self) -> None:
        self.assertIn(
            "PA_BUILDFLAG_INTERNAL_HAS_MEMORY_TAGGING() (0)",
            self.build_script,
        )
        self.assertIn("V8PartitionAllocGlibc227Arm64Patch.txt", self.build_script)

    def test_cmake_try_compile_keeps_cross_target_identity(self) -> None:
        self.assertIn("CMAKE_TRY_COMPILE_PLATFORM_VARIABLES", self.toolchain)
        self.assertIn("WEBSCENE_LINUX_TARGET_TRIPLE", self.toolchain)
        self.assertIn("CMAKE_SYSROOT", self.toolchain)

    def test_linux_openssl_is_resolved_only_from_the_target_sysroot(self) -> None:
        self.assertIn('target_library_dir="$sysroot/usr/lib/$target_triple"', self.build_script)
        self.assertIn('-DOPENSSL_CRYPTO_LIBRARY="$target_library_dir/libcrypto.a"', self.build_script)
        self.assertIn('-DOPENSSL_SSL_LIBRARY="$target_library_dir/libssl.a"', self.build_script)

    def test_linux_zlib_is_resolved_only_from_the_target_sysroot(self) -> None:
        self.assertIn('-DZLIB_INCLUDE_DIR="$target_include_dir"', self.build_script)
        self.assertIn('-DZLIB_LIBRARY="$target_library_dir/libz.a"', self.build_script)
        self.assertIn('-DCMAKE_SKIP_RPATH=TRUE', self.build_script)
        self.assertIn('LD_LIBRARY_PATH=$test_library_path', self.build_script)
        self.assertIn(
            'WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../.."',
            (ROOT / "experiments/WebScene.NativeEngine.Probe/CMakeLists.txt").read_text(),
        )

    def test_toolchain_exposes_target_multiarch_search_paths(self) -> None:
        self.assertIn("CMAKE_LIBRARY_ARCHITECTURE", self.toolchain)
        self.assertIn('/usr/lib/${WEBSCENE_LINUX_TARGET_TRIPLE}', self.toolchain)
        self.assertIn('/usr/include/${WEBSCENE_LINUX_TARGET_TRIPLE}', self.toolchain)

    def test_linux_runtime_uses_v8_bundled_libcxx(self) -> None:
        self.assertIn('v8_root/third_party/libc++/src/include', self.build_script)
        self.assertIn('v8_root/third_party/libc++abi/src/include', self.build_script)
        self.assertIn('v8_root/buildtools/third_party/libc++', self.build_script)
        self.assertIn('__config_site', self.build_script)
        self.assertIn('__assertion_handler', self.build_script)
        self.assertIn(
            'artifacts/native-engine-v8/linux-*/v8/buildtools/third_party/libc++',
            self.workflow,
        )
        self.assertIn("-nostdinc++ -nostdlib++", self.build_script)
        self.assertIn("CMAKE_CXX_STANDARD_LIBRARIES", self.build_script)
        self.assertIn("libc++abi.a", self.build_script)
        self.assertIn("third_party/llvm-build/Release+Asserts", self.build_script)
        self.assertIn("_LIBCPP_HARDENING_MODE_EXTENSIVE", self.build_script)
        self.assertIn("-include new", self.build_script)
        self.assertIn('llvm_ar" rcD "$regular_archive"', self.build_script)
        self.assertIn("'!<arch>'", self.build_script)
        self.assertIn("archive_is_regular", self.workflow)
        self.assertIn("V8LibcxxMemoryResourcePatch.txt", self.build_script)
        self.assertIn("V8LibcxxMemoryResourcePatch.txt", self.workflow)
        self.assertIn("archive_has_memory_resource", self.workflow)
        self.assertIn("memory_resource\\.o", self.build_script)
        self.assertNotIn(
            'CMAKE_CXX_STANDARD_LIBRARIES=$v8_libcxx_archive;$v8_libcxxabi_archive',
            self.build_script,
        )
        self.assertIn(
            'CMAKE_CXX_STANDARD_LIBRARIES=$v8_libcxx_archive '
            '$v8_libcxxabi_archive -pthread',
            self.build_script,
        )

    def test_arm_mac_can_cross_build_intel_runtime(self) -> None:
        self.assertIn("macos_arm64_to_x64=true", self.build_script)
        self.assertIn('-DCMAKE_OSX_ARCHITECTURES="$macos_architecture"', self.build_script)
        self.assertIn("x86_64-apple-darwin", self.build_script)
        self.assertIn("rust-std-$rust_version-x86_64-apple-darwin", self.build_script)
        self.assertIn("dotnet_architecture: x64", self.workflow)
        self.assertIn("architecture: ${{ matrix.dotnet_architecture }}", self.workflow)
        self.assertIn("RUNNER_TOOL_CACHE", self.build_script)
        self.assertIn("--retry-all-errors", self.build_script)


if __name__ == "__main__":
    unittest.main()
