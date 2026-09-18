#!/usr/bin/env python3
"""Reclaim stale entries from one GitHub runner temporary directory."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import stat
import subprocess
import time

from prepare_dotnet_install import prepare_installation


DEFAULT_MAX_ENTRIES = 10_000
DEFAULT_MAX_SECONDS = 30.0
DEFAULT_STALE_SECONDS = 30 * 60


@dataclass(frozen=True)
class ReclaimResult:
    inspected: int
    removed: int
    skipped_recent: int
    skipped_owned: int
    skipped_permission: int
    skipped_protected: int
    free_before: int
    free_after: int
    truncated: bool


def _protected_children(root: Path, paths: list[Path]) -> set[Path]:
    protected: set[Path] = set()
    root_resolved = root.resolve(strict=True)
    for candidate in paths:
        try:
            relative = candidate.resolve(strict=False).relative_to(root_resolved)
        except ValueError:
            continue
        if relative.parts:
            protected.add(root / relative.parts[0])
    return protected


def _remove_entry(path: Path, *, privileged: bool) -> None:
    mode = path.lstat().st_mode
    try:
        if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
            path.unlink()
        else:
            shutil.rmtree(path)
    except PermissionError:
        if not privileged:
            raise
        subprocess.run(
            ["sudo", "rm", "-rf", "--", str(path)],
            check=True,
            stdin=subprocess.DEVNULL,
        )


def reclaim_stale_entries(
    root: Path,
    *,
    protected_paths: list[Path] | None = None,
    stale_seconds: int = DEFAULT_STALE_SECONDS,
    maximum_entries: int = DEFAULT_MAX_ENTRIES,
    maximum_seconds: float = DEFAULT_MAX_SECONDS,
    now: float | None = None,
    owner_uid: int | None = None,
    privileged_prefixes: tuple[str, ...] = (),
    allowed_prefixes: tuple[str, ...] = (),
) -> ReclaimResult:
    if stale_seconds < 0 or maximum_entries < 1 or maximum_seconds <= 0:
        raise ValueError("cleanup bounds must be positive")
    if not root.is_dir() or root.is_symlink():
        raise ValueError("runner temporary root must be an existing directory")

    root = root.resolve(strict=True)
    if root == Path(root.anchor):
        raise ValueError("refusing to manage a filesystem root")

    protected = _protected_children(root, protected_paths or [])
    protected.add(root / "dotnet")
    current_time = time.time() if now is None else now
    uid = os.getuid() if owner_uid is None else owner_uid
    deadline = time.monotonic() + maximum_seconds
    free_before = shutil.disk_usage(root).free
    inspected = removed = skipped_recent = skipped_owned = skipped_permission = skipped_protected = 0
    truncated = False

    for entry in root.iterdir():
        if allowed_prefixes and not any(entry.name.startswith(prefix) for prefix in allowed_prefixes):
            continue
        if inspected >= maximum_entries or time.monotonic() > deadline:
            truncated = True
            break
        inspected += 1
        if entry in protected:
            skipped_protected += 1
            continue

        try:
            metadata = entry.lstat()
        except FileNotFoundError:
            continue
        if metadata.st_uid != uid:
            skipped_owned += 1
            continue
        if current_time - metadata.st_mtime < stale_seconds:
            skipped_recent += 1
            continue

        privileged = any(entry.name.startswith(prefix) for prefix in privileged_prefixes)
        try:
            _remove_entry(entry, privileged=privileged)
        except FileNotFoundError:
            removed += 1
            continue
        except PermissionError:
            skipped_permission += 1
            continue
        removed += 1

    free_after = shutil.disk_usage(root).free
    return ReclaimResult(
        inspected=inspected,
        removed=removed,
        skipped_recent=skipped_recent,
        skipped_owned=skipped_owned,
        skipped_permission=skipped_permission,
        skipped_protected=skipped_protected,
        free_before=free_before,
        free_after=free_after,
        truncated=truncated,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("runner_temp", type=Path)
    parser.add_argument("--system-temp", type=Path, required=True)
    parser.add_argument("--minimum-free-bytes", type=int, required=True)
    parser.add_argument("--stale-seconds", type=int, default=DEFAULT_STALE_SECONDS)
    parser.add_argument("--system-stale-seconds", type=int, default=120)
    parser.add_argument("--maximum-entries", type=int, default=DEFAULT_MAX_ENTRIES)
    parser.add_argument("--maximum-seconds", type=float, default=DEFAULT_MAX_SECONDS)
    args = parser.parse_args()

    declared = os.environ.get("RUNNER_TEMP")
    if declared is None or Path(declared).resolve(strict=False) != args.runner_temp.resolve(strict=False):
        raise SystemExit("runner_temp must exactly match the RUNNER_TEMP environment variable")
    if args.system_temp.resolve(strict=True) != Path("/tmp").resolve(strict=True):
        raise SystemExit("system_temp must resolve exactly to /tmp")

    protected = [
        Path(value)
        for name in ("GITHUB_ENV", "GITHUB_OUTPUT", "GITHUB_PATH", "GITHUB_STEP_SUMMARY")
        if (value := os.environ.get(name))
    ]
    # setup-dotnet can leave a large partial extraction in RUNNER_TEMP after a
    # canceled or ENOSPC run. Reuse a valid installation and remove only an
    # invalid one before measuring the job's disk budget.
    prepare_installation(args.runner_temp / "dotnet")
    result = reclaim_stale_entries(
        args.runner_temp,
        protected_paths=protected,
        stale_seconds=args.stale_seconds,
        maximum_entries=args.maximum_entries,
        maximum_seconds=args.maximum_seconds,
        privileged_prefixes=("appscene-headless-",),
    )
    system_result = reclaim_stale_entries(
        args.system_temp,
        stale_seconds=args.system_stale_seconds,
        maximum_entries=args.maximum_entries,
        maximum_seconds=args.maximum_seconds,
        allowed_prefixes=("rustc", "tmp", "cmake-", "cargo-", "dotnet-"),
    )
    reclaimed = max(0, result.free_after - result.free_before)
    system_reclaimed = max(0, system_result.free_after - system_result.free_before)
    print(
        "Self-hosted runner disk preflight: "
        f"inspected={result.inspected} removed={result.removed} "
        f"recent={result.skipped_recent} owned-by-other={result.skipped_owned} "
        f"permission-skipped={result.skipped_permission} protected={result.skipped_protected} "
        f"free-before={result.free_before} "
        f"free-after={result.free_after} reclaimed={reclaimed} truncated={result.truncated}."
    )
    print(
        "System temporary disk preflight: "
        f"inspected={system_result.inspected} removed={system_result.removed} "
        f"recent={system_result.skipped_recent} owned-by-other={system_result.skipped_owned} "
        f"permission-skipped={system_result.skipped_permission} "
        f"protected={system_result.skipped_protected} free-before={system_result.free_before} "
        f"free-after={system_result.free_after} reclaimed={system_reclaimed} "
        f"truncated={system_result.truncated}."
    )
    available = min(result.free_after, system_result.free_after)
    if available < args.minimum_free_bytes:
        print(
            "::error title=Insufficient runner disk::"
            f"{available} bytes free on the most constrained checked filesystem after bounded cleanup; "
            f"{args.minimum_free_bytes} required. Host capacity cleanup is required."
        )
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
