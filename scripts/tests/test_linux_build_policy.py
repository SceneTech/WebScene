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

    def test_release_matrix_contains_both_glibc_rids(self) -> None:
        for rid in ("linux-x64", "linux-arm64"):
            self.assertIn(f"rid: {rid}", self.workflow)
            self.assertIn(f"--expected-rid {rid}", self.workflow)
            self.assertIn(f"--native-rid {rid}", self.workflow)
        self.assertIn("github.ref_type != 'tag'", self.workflow)

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
        self.assertIn('openssl_library_dir="$sysroot/usr/lib/$target_triple"', self.build_script)
        self.assertIn('-DOPENSSL_CRYPTO_LIBRARY="$openssl_library_dir/libcrypto.so"', self.build_script)
        self.assertIn('-DOPENSSL_SSL_LIBRARY="$openssl_library_dir/libssl.so"', self.build_script)


if __name__ == "__main__":
    unittest.main()
