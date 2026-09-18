#!/usr/bin/env python3

from pathlib import Path
import os
import sys
import tempfile
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from prepare_runner_disk import ReclaimResult, main, reclaim_stale_entries  # noqa: E402


class PrepareRunnerDiskTests(unittest.TestCase):
    def test_removes_only_old_owned_direct_children(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old = root / "old-job"
            recent = root / "current-job"
            old.mkdir()
            recent.mkdir()
            (old / "payload").write_bytes(b"x" * 4096)
            now = time.time()
            os.utime(old, (now - 3600, now - 3600))

            result = reclaim_stale_entries(root, stale_seconds=1800, now=now)

            self.assertFalse(old.exists())
            self.assertTrue(recent.exists())
            self.assertEqual(result.removed, 1)
            self.assertEqual(result.skipped_recent, 1)

    def test_protects_current_command_parent_and_dotnet(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            commands = root / "_runner_file_commands"
            dotnet = root / "dotnet"
            commands.mkdir()
            dotnet.mkdir()
            command_file = commands / "env"
            command_file.write_text("", encoding="utf-8")
            now = time.time()
            os.utime(commands, (now - 3600, now - 3600))
            os.utime(dotnet, (now - 3600, now - 3600))

            result = reclaim_stale_entries(
                root,
                protected_paths=[command_file],
                stale_seconds=1800,
                now=now,
            )

            self.assertTrue(commands.exists())
            self.assertTrue(dotnet.exists())
            self.assertEqual(result.skipped_protected, 2)

    def test_unlinks_stale_symlink_without_following_it(self) -> None:
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as outside:
            root = Path(directory)
            target = Path(outside) / "keep"
            target.write_text("keep", encoding="utf-8")
            link = root / "old-link"
            link.symlink_to(target)
            now = time.time()
            os.utime(link, (now - 3600, now - 3600), follow_symlinks=False)

            reclaim_stale_entries(root, stale_seconds=1800, now=now)

            self.assertFalse(link.exists())
            self.assertEqual(target.read_text(encoding="utf-8"), "keep")

    def test_skips_entries_owned_by_another_uid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entry = root / "old-job"
            entry.mkdir()
            now = time.time()
            os.utime(entry, (now - 3600, now - 3600))

            result = reclaim_stale_entries(
                root,
                stale_seconds=1800,
                now=now,
                owner_uid=os.getuid() + 1,
            )

            self.assertTrue(entry.exists())
            self.assertEqual(result.skipped_owned, 1)

    def test_rejects_filesystem_root_and_symlink_root(self) -> None:
        with self.assertRaises(ValueError):
            reclaim_stale_entries(Path(Path.cwd().anchor))
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as target:
            link = Path(directory) / "temp-link"
            link.symlink_to(target)
            with self.assertRaises(ValueError):
                reclaim_stale_entries(link)

    def test_entry_and_time_bounds_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for index in range(3):
                (root / str(index)).mkdir()
            with self.assertRaises(RuntimeError):
                reclaim_stale_entries(root, maximum_entries=2)
            with mock.patch("prepare_runner_disk.time.monotonic", side_effect=[0.0, 1.0]):
                with self.assertRaises(RuntimeError):
                    reclaim_stale_entries(root, maximum_seconds=0.5)

    def test_ten_thousand_entry_scan_is_bounded(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for index in range(10_000):
                (root / f"current-{index}").touch()
            started = time.monotonic()
            result = reclaim_stale_entries(root, maximum_entries=10_000)
            elapsed = time.monotonic() - started
            self.assertEqual(result.inspected, 10_000)
            self.assertEqual(result.removed, 0)
            self.assertLess(elapsed, 5.0)

    def test_cli_fails_when_cleanup_cannot_reach_minimum_free_space(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = ReclaimResult(0, 0, 0, 0, 0, 0, 1, 2)
            arguments = [
                "prepare_runner_disk.py",
                directory,
                "--minimum-free-bytes",
                "3",
            ]
            with mock.patch.dict(os.environ, {"RUNNER_TEMP": directory}, clear=False):
                with mock.patch("sys.argv", arguments):
                    with mock.patch("prepare_runner_disk.reclaim_stale_entries", return_value=result):
                        self.assertEqual(main(), 2)

    def test_cli_removes_incomplete_dotnet_before_measuring_capacity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            dotnet = Path(directory) / "dotnet"
            (dotnet / "sdk" / "partial").mkdir(parents=True)
            arguments = [
                "prepare_runner_disk.py",
                directory,
                "--minimum-free-bytes",
                "1",
            ]
            with mock.patch.dict(os.environ, {"RUNNER_TEMP": directory}, clear=False):
                with mock.patch("sys.argv", arguments):
                    self.assertEqual(main(), 0)
            self.assertFalse(dotnet.exists())

    def test_privileged_cleanup_is_limited_to_named_direct_children(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            allowed = root / "appscene-headless-old"
            denied = root / "unrelated-old"
            allowed.mkdir()
            denied.mkdir()
            now = time.time()
            os.utime(allowed, (now - 3600, now - 3600))
            os.utime(denied, (now - 3600, now - 3600))

            original = __import__("prepare_runner_disk")._remove_entry

            def remove(path: Path, *, privileged: bool) -> None:
                if path.name == allowed.name:
                    self.assertTrue(privileged)
                    path.rmdir()
                    return
                if path.name == denied.name:
                    self.assertFalse(privileged)
                    raise PermissionError
                original(path, privileged=privileged)

            with mock.patch("prepare_runner_disk._remove_entry", side_effect=remove):
                result = reclaim_stale_entries(
                    root,
                    stale_seconds=1800,
                    now=now,
                    privileged_prefixes=("appscene-headless-",),
                )

            self.assertFalse(allowed.exists())
            self.assertTrue(denied.exists())
            self.assertEqual(result.removed, 1)
            self.assertEqual(result.skipped_permission, 1)


if __name__ == "__main__":
    unittest.main()
