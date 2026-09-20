#!/usr/bin/env python3
"""Fail closed when an installed Linux Runtime SDK exceeds package budgets."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


DEFAULT_LOGICAL_BYTES = 1_200_000_000
DEFAULT_ALLOCATED_BYTES = 1_350_000_000
DEFAULT_ENTRY_COUNT = 20_000
LARGE_FILE_BYTES = 1_048_576
REQUIRED = (
    "lib/libwebscene_native_engine.a",
    "lib/libwebscene_media.a",
    "lib/libixwebsocket.a",
    "lib/libmbedcrypto.a",
    "lib/libv8_monolith.a",
    "lib/libwebgpu_dawn.so",
    "lib/libEGL.so",
    "lib/libGLESv2.so",
    "share/webscene/runtime/icudtl.dat",
    "share/webscene/runtime/webscene_bootstrap_snapshot.bin",
    "share/webscene/runtime/webscene_bootstrap_snapshot.meta",
    "share/licenses/WebScene/V8-LICENSE",
    "share/licenses/WebScene/MbedTLS-LICENSE",
    "share/licenses/WebScene/IXWebSocket-LICENSE",
)
FORBIDDEN_SUFFIXES = (".o", ".obj", ".map", ".pdb", ".dSYM")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def audit(root: Path, logical_budget: int, allocated_budget: int,
          entry_budget: int) -> dict[str, object]:
    missing = [name for name in REQUIRED if not (root / name).is_file()]
    if missing:
        raise RuntimeError("missing Linux Runtime SDK entries: " + ", ".join(missing))

    entries = sorted(path for path in root.rglob("*") if not path.is_dir())
    forbidden = [str(path.relative_to(root)) for path in entries
                 if path.name.endswith(FORBIDDEN_SUFFIXES)]
    if forbidden:
        raise RuntimeError("developer artifacts entered Runtime SDK: " + ", ".join(forbidden))

    regular = [path for path in entries if path.is_file() and not path.is_symlink()]
    logical = sum(path.stat().st_size for path in regular)
    allocated = sum(path.stat().st_blocks * 512 for path in regular)
    if len(entries) > entry_budget:
        raise RuntimeError(f"entry budget exceeded: {len(entries)} > {entry_budget}")
    if logical > logical_budget:
        raise RuntimeError(f"logical byte budget exceeded: {logical} > {logical_budget}")
    if allocated > allocated_budget:
        raise RuntimeError(
            f"allocated byte budget exceeded: {allocated} > {allocated_budget}"
        )

    by_digest: dict[str, list[str]] = {}
    for path in regular:
        if path.stat().st_size < LARGE_FILE_BYTES:
            continue
        by_digest.setdefault(digest(path), []).append(str(path.relative_to(root)))
    duplicates = [paths for paths in by_digest.values() if len(paths) > 1]
    if duplicates:
        raise RuntimeError(
            "duplicate large payloads entered Runtime SDK: " + json.dumps(duplicates)
        )

    return {
        "schema": "webscene-linux-runtime-package-v1",
        "entryCount": len(entries),
        "logicalBytes": logical,
        "allocatedBytes": allocated,
        "budgets": {
            "entryCount": entry_budget,
            "logicalBytes": logical_budget,
            "allocatedBytes": allocated_budget,
        },
        "largePayloadCount": len(by_digest),
        "duplicateLargePayloads": [],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("--logical-bytes", type=int, default=DEFAULT_LOGICAL_BYTES)
    parser.add_argument("--allocated-bytes", type=int, default=DEFAULT_ALLOCATED_BYTES)
    parser.add_argument("--entries", type=int, default=DEFAULT_ENTRY_COUNT)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.sdk.resolve()
    if not root.is_dir():
        parser.error(f"SDK root is not a directory: {root}")
    result = audit(root, args.logical_bytes, args.allocated_bytes, args.entries)
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    print(encoded, end="")


if __name__ == "__main__":
    main()
