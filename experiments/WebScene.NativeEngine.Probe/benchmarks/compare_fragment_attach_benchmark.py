#!/usr/bin/env python3
"""Run an ABBA comparison of atomic DocumentFragment attachment recascades."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(binary: Path, samples: int, warmups: int) -> dict[str, float | str]:
    completed = subprocess.run(
        [str(binary), str(samples), str(warmups), "fragment-attach"],
        cwd=binary.parent, check=True, capture_output=True, text=True, timeout=180)
    fields: dict[str, float | str] = {}
    for field in completed.stdout.strip().split():
        if "=" not in field:
            continue
        name, value = field.split("=", 1)
        try:
            fields[name] = float(value)
        except ValueError:
            fields[name] = value
    if fields.get("mode") != "fragment-attach" or "p50_ms" not in fields:
        raise RuntimeError(f"unexpected benchmark output: {completed.stdout!r}")
    return fields


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--blocks", type=int, default=6)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--minimum-improvement-percent", type=float, default=10)
    args = parser.parse_args()
    binaries = {"control": args.control.resolve(), "candidate": args.candidate.resolve()}
    runs = []
    for block in range(args.blocks):
        for order, variant in enumerate(("control", "candidate", "candidate", "control")):
            runs.append({"block": block + 1, "order": order + 1, "variant": variant,
                         **run(binaries[variant], args.samples, args.warmups)})
    medians = {
        variant: statistics.median(float(run["p50_ms"])
                                   for run in runs if run["variant"] == variant)
        for variant in binaries
    }
    improvement = (1 - medians["candidate"] / medians["control"]) * 100
    result = {
        "schemaVersion": 1,
        "fixture": {"panels": 24, "childrenPerPanel": 256,
                    "structuralSelectors": [":empty", ":has", ":nth-child", ":last-child"]},
        "binaries": {name: {"path": str(path), "sha256": digest(path)}
                     for name, path in binaries.items()},
        "medianP50Ms": medians,
        "improvementPercent": round(improvement, 3),
        "minimumImprovementPercent": args.minimum_improvement_percent,
        "runs": runs,
        "passed": improvement >= args.minimum_improvement_percent,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in
                      ("medianP50Ms", "improvementPercent", "passed")}, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
