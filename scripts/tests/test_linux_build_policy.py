from __future__ import annotations

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

    def test_release_matrix_contains_both_glibc_rids(self) -> None:
        for rid in ("linux-x64", "linux-arm64"):
            self.assertIn(f"rid: {rid}", self.workflow)
            self.assertIn(f"--expected-rid {rid}", self.workflow)
            self.assertIn(f"--native-rid {rid}", self.workflow)
        self.assertIn("github.ref_type != 'tag'", self.workflow)


if __name__ == "__main__":
    unittest.main()
