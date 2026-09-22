#!/usr/bin/env python3
"""Compare runtime payload bytes from two independently produced NuGet packages."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import zipfile


def payload_hashes(package: pathlib.Path, rid: str) -> dict[str, str]:
    prefix = f"runtimes/{rid}/native/"
    with zipfile.ZipFile(package) as archive:
        return {
            name.removeprefix(prefix): hashlib.sha256(archive.read(name)).hexdigest()
            for name in sorted(archive.namelist())
            if name.startswith(prefix) and not name.endswith("/")
        }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("first", type=pathlib.Path)
    parser.add_argument("second", type=pathlib.Path)
    parser.add_argument("--rid", choices=("linux-x64", "linux-arm64"), required=True)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    first = payload_hashes(args.first, args.rid)
    second = payload_hashes(args.second, args.rid)
    report = {
        "schemaVersion": 1,
        "status": "pass" if first == second else "fail",
        "runtimeIdentifier": args.rid,
        "first": first,
        "second": second,
    }
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
