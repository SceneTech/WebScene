#!/usr/bin/env python3
"""Stage the patched V8 headers paired with a produced monolith."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


MAX_FILES = 20_000
MAX_LOGICAL_BYTES = 128 * 1024 * 1024


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_digest(root: Path) -> str:
    entries = (
        f"{path.relative_to(root).as_posix()} {sha256(path)}\n"
        for path in sorted(root.rglob("*"))
        if path.is_file()
    )
    return hashlib.sha256("".join(entries).encode()).hexdigest()


def copy_notices(source: Path, destination: Path) -> int:
    count = 0
    for path in sorted(source.rglob("*")):
        if not path.is_file() or not path.name.upper().startswith(
            ("LICENSE", "COPYING", "NOTICE")
        ):
            continue
        target = destination / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        count += 1
    return count


def stage(v8_root: Path, output: Path) -> dict[str, object]:
    v8_root = v8_root.resolve()
    output = output.resolve()
    if output.exists():
        if not output.is_dir() or any(output.iterdir()):
            raise ValueError("V8 SDK output must be absent or empty")
        output.rmdir()

    inspector = v8_root / "include/v8-inspector.h"
    if "virtual void consoleAPICalled" not in inspector.read_text():
        raise ValueError("V8 Inspector header does not contain WebScene's console bridge")
    required = [
        v8_root / "include/v8.h",
        v8_root / "include/v8-version.h",
        v8_root / "LICENSE",
        v8_root / "third_party/partition_alloc/src",
    ]
    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise ValueError(f"V8 SDK inputs are missing: {missing}")

    temporary = output.with_name(f".{output.name}.stage-{os.getpid()}")
    if temporary.exists():
        raise ValueError(f"V8 SDK temporary output already exists: {temporary}")
    try:
        shutil.copytree(v8_root / "include", temporary / "include")
        shutil.copy2(v8_root / "LICENSE", temporary / "LICENSE")
        shutil.copytree(
            v8_root / "third_party/partition_alloc/src",
            temporary / "third_party/partition_alloc/src",
        )
        notice_count = copy_notices(
            v8_root / "third_party", temporary / "third_party"
        )
        files = [path for path in temporary.rglob("*") if path.is_file()]
        symlinks = [path for path in temporary.rglob("*") if path.is_symlink()]
        git_metadata = [path for path in temporary.rglob(".git")]
        logical_bytes = sum(path.stat().st_size for path in files)
        if symlinks or git_metadata:
            raise ValueError("Staged V8 SDK contains symlinks or Git metadata")
        if len(files) > MAX_FILES or logical_bytes > MAX_LOGICAL_BYTES:
            raise ValueError(
                f"Staged V8 SDK exceeds budget: {len(files)} files, "
                f"{logical_bytes} bytes"
            )
        revision = subprocess.check_output(
            ["git", "-C", str(v8_root), "rev-parse", "HEAD"], text=True
        ).strip()
        manifest = {
            "schemaVersion": 1,
            "v8Revision": revision,
            "fileCount": len(files),
            "logicalBytes": logical_bytes,
            "noticeFileCount": notice_count,
            "includeTreeSha256": tree_digest(temporary / "include"),
            "partitionAllocTreeSha256": tree_digest(
                temporary / "third_party/partition_alloc/src"
            ),
        }
        (temporary / "v8-sdk-stage.json").write_text(
            json.dumps(manifest, indent=2) + "\n"
        )
        temporary.replace(output)
        return manifest
    except BaseException:
        if temporary.exists():
            shutil.rmtree(temporary)
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--v8-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    print(json.dumps(stage(arguments.v8_root, arguments.output), indent=2))


if __name__ == "__main__":
    main()
