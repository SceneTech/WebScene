#!/usr/bin/env python3
"""Verify the architecture, dependency, symbol-version, and export contract of a Linux DSO."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess


ALLOWED_NEEDED = {
    "libc.so.6", "libdl.so.2", "libgcc_s.so.1", "libm.so.6",
    "libpthread.so.0", "librt.so.1", "libstdc++.so.6", "libutil.so.1",
}
EXPECTED_MACHINES = {
    "linux-x64": "Advanced Micro Devices X86-64",
    "linux-arm64": "AArch64",
}
EXPECTED_INTERPRETERS = {
    # Runtime payloads are shared libraries, not PIE executables. A PT_INTERP
    # segment would make the payload directly executable and is never valid.
    "linux-x64": "",
    "linux-arm64": "",
}


def version_tuple(value: str) -> tuple[int, ...]:
    return tuple(int(part) for part in value.split("."))


def collect_versions(text: str, namespace: str) -> set[str]:
    return set(re.findall(rf"\b{re.escape(namespace)}_([0-9]+(?:\.[0-9]+)+)\b", text))


def verify_text(
    text: str,
    rid: str,
    max_glibc: str,
    max_glibcxx: str,
    max_cxxabi: str,
    required_exports: set[str] | None = None,
) -> dict[str, object]:
    issues: list[str] = []
    machine_match = re.search(r"^\s*Machine:\s*(.+?)\s*$", text, re.MULTILINE)
    machine = machine_match.group(1) if machine_match else ""
    if machine != EXPECTED_MACHINES[rid]:
        issues.append(f"ELF machine is {machine!r}; expected {EXPECTED_MACHINES[rid]!r}")

    interpreter_match = re.search(r"Requesting program interpreter:\s*([^\]]+)\]", text)
    interpreter = interpreter_match.group(1) if interpreter_match else ""
    if interpreter != EXPECTED_INTERPRETERS[rid]:
        issues.append(
            f"ELF interpreter is {interpreter!r}; expected {EXPECTED_INTERPRETERS[rid]!r}"
        )

    needed = set(re.findall(r"\(NEEDED\).*?\[(.+?)\]", text))
    unexpected_needed = sorted(needed - ALLOWED_NEEDED)
    if unexpected_needed:
        issues.append("unexpected DT_NEEDED libraries: " + ", ".join(unexpected_needed))
    if re.search(r"\((?:RPATH|RUNPATH)\)", text):
        issues.append("RPATH/RUNPATH is not permitted")
    if "/crossrootfs/" in text or "/workspace/" in text:
        issues.append("build or sysroot path leaked into ELF metadata")

    ceilings = {"GLIBC": max_glibc, "GLIBCXX": max_glibcxx, "CXXABI": max_cxxabi}
    observed: dict[str, list[str]] = {}
    for namespace, ceiling in ceilings.items():
        versions = sorted(collect_versions(text, namespace), key=version_tuple)
        observed[namespace] = versions
        too_new = [value for value in versions if version_tuple(value) > version_tuple(ceiling)]
        if too_new:
            issues.append(f"{namespace} requires {too_new[-1]}; maximum is {ceiling}")

    exports = set(re.findall(r"\bGLOBAL\s+DEFAULT\s+\d+\s+(webscene_[A-Za-z0-9_]+)\b", text))
    missing_exports = sorted((required_exports or {"webscene_engine_get_abi_version"}) - exports)
    if missing_exports:
        issues.append("missing required exports: " + ", ".join(missing_exports))

    return {
        "schemaVersion": 1,
        "status": "pass" if not issues else "fail",
        "runtimeIdentifier": rid,
        "machine": machine,
        "interpreter": interpreter,
        "needed": sorted(needed),
        "symbolVersions": observed,
        "limits": ceilings,
        "issues": issues,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=pathlib.Path)
    parser.add_argument("--rid", choices=sorted(EXPECTED_MACHINES), required=True)
    parser.add_argument("--max-glibc", default="2.27")
    parser.add_argument("--max-glibcxx", default="3.4.24")
    parser.add_argument("--max-cxxabi", default="1.3.11")
    parser.add_argument(
        "--exports-file",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1]
        / "experiments/WebScene.NativeEngine.Probe/native/webscene_native_engine.exports",
    )
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if not args.library.is_file():
        parser.error(f"library does not exist: {args.library}")
    if not args.exports_file.is_file():
        parser.error(f"exports file does not exist: {args.exports_file}")
    required_exports = {
        line.strip().removeprefix("_")
        for line in args.exports_file.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    completed = subprocess.run(
        ["readelf", "-h", "-l", "-d", "--version-info", "--dyn-syms", str(args.library)],
        check=False, capture_output=True, text=True,
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr.strip() or "readelf failed")
    report = verify_text(
        completed.stdout,
        args.rid,
        args.max_glibc,
        args.max_glibcxx,
        args.max_cxxabi,
        required_exports,
    )
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
