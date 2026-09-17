#!/usr/bin/env python3
"""Discard an incomplete setup-dotnet installation before it is reused."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess


def remove_installation(root: Path) -> None:
    if root.is_symlink():
        root.unlink()
    elif root.exists():
        shutil.rmtree(root)


def prepare_installation(root: Path) -> bool:
    """Return True when an existing healthy installation can be reused."""
    if root.name.lower() != "dotnet":
        raise ValueError(f"Refusing to manage unexpected install path '{root}'.")
    if not root.exists() and not root.is_symlink():
        return False

    executable = root / ("dotnet.exe" if os.name == "nt" else "dotnet")
    try:
        result = subprocess.run(
            [str(executable), "--info"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=30,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        result = None

    if result is not None and result.returncode == 0:
        print(f"Reusing healthy .NET installation at '{root}'.")
        return True

    print(
        f"::warning title=Repairing .NET installation::Removing incomplete "
        f"setup-dotnet state at '{root}'."
    )
    remove_installation(root)
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("install_directory", type=Path)
    args = parser.parse_args()
    prepare_installation(args.install_directory)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
