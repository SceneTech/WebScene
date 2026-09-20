#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
HELPER = ROOT / "scripts" / "v8-patch-workspace.sh"


class V8PatchWorkspaceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.checkout = pathlib.Path(self.temp.name) / "checkout"
        self.checkout.mkdir()
        self.git("init", "-q")
        self.git("config", "user.name", "WebScene Test")
        self.git("config", "user.email", "test@webscene.invalid")
        (self.checkout / "runtime.txt").write_text("before\n", encoding="utf-8")
        self.git("add", "runtime.txt")
        self.git("commit", "-qm", "fixture")
        (self.checkout / "runtime.txt").write_text("after\n", encoding="utf-8")
        self.patch = pathlib.Path(self.temp.name) / "runtime.patch"
        self.patch.write_text(self.git("diff").stdout, encoding="utf-8")
        self.git("checkout", "--", "runtime.txt")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def git(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ("git", *args),
            cwd=self.checkout,
            check=True,
            text=True,
            capture_output=True,
        )

    def helper(self, command: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                "bash",
                "-c",
                f'source "$1"; {command}',
                "v8-patch-test",
                str(HELPER),
                str(self.checkout),
                str(self.patch),
            ],
            check=False,
            text=True,
            capture_output=True,
        )

    def test_apply_is_idempotent_and_restore_returns_clean_checkout(self) -> None:
        apply = 'webscene_apply_patch_once "$2" "$3"'
        self.assertEqual(0, self.helper(apply).returncode)
        self.assertEqual(0, self.helper(apply).returncode)
        self.assertEqual("after\n", (self.checkout / "runtime.txt").read_text())

        restore = 'webscene_restore_patch_if_applied "$2" "$3"'
        self.assertEqual(0, self.helper(restore).returncode)
        self.assertEqual(0, self.helper(restore).returncode)
        self.assertEqual("", self.git("status", "--short").stdout)

    def test_restore_preserves_unrelated_edits(self) -> None:
        (self.checkout / "unrelated.txt").write_text("keep me\n", encoding="utf-8")
        self.assertEqual(
            0,
            self.helper('webscene_restore_patch_if_applied "$2" "$3"').returncode,
        )
        self.assertIn("?? unrelated.txt", self.git("status", "--short").stdout)

    def test_partial_patch_is_not_discarded(self) -> None:
        (self.checkout / "runtime.txt").write_text("different\n", encoding="utf-8")
        self.assertEqual(
            0,
            self.helper('webscene_restore_patch_if_applied "$2" "$3"').returncode,
        )
        self.assertEqual("different\n", (self.checkout / "runtime.txt").read_text())


if __name__ == "__main__":
    unittest.main()
