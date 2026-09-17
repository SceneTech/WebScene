#!/usr/bin/env python3

from pathlib import Path
import os
import stat
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from prepare_dotnet_install import prepare_installation  # noqa: E402


class PrepareDotnetInstallTests(unittest.TestCase):
    def test_unexpected_directory_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                prepare_installation(Path(directory) / "sdk")

    def test_missing_installation_is_left_for_setup_dotnet(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "dotnet"
            self.assertFalse(prepare_installation(root))
            self.assertFalse(root.exists())

    @unittest.skipIf(os.name == "nt", "fixture uses a POSIX executable")
    def test_healthy_installation_is_reused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "dotnet"
            root.mkdir()
            executable = root / "dotnet"
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
            marker = root / "sdk" / "8.0.425"
            marker.mkdir(parents=True)

            self.assertTrue(prepare_installation(root))
            self.assertTrue(marker.exists())

    def test_sdk_markers_without_host_are_removed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "dotnet"
            marker = root / "sdk" / "8.0.425"
            marker.mkdir(parents=True)

            self.assertFalse(prepare_installation(root))
            self.assertFalse(root.exists())

    @unittest.skipIf(os.name == "nt", "fixture uses a POSIX executable")
    def test_broken_host_and_sdk_markers_are_removed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "dotnet"
            root.mkdir()
            executable = root / "dotnet"
            executable.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
            executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
            (root / "sdk" / "10.0.401").mkdir(parents=True)

            self.assertFalse(prepare_installation(root))
            self.assertFalse(root.exists())


if __name__ == "__main__":
    unittest.main()
