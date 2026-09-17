#!/usr/bin/env python3
"""Extract WebScene's immutable JavaScript bootstrap programs for V8 snapshotting."""

from __future__ import annotations

import argparse
import pathlib
import re


MARKERS = (
    "void install_websocket_globals",
    "void install_editor_web_platform_globals",
    "void install_fetch_globals",
    "static constexpr std::string_view intersection_observer_bootstrap_source",
)

# MSVC rejects string literals larger than 16,380 bytes (C2026). Keep enough
# headroom for source-encoding differences while retaining byte-for-byte joins.
MAX_RAW_LITERAL_BYTES = 15_000


def extract_parts(source: str, marker: str) -> list[str]:
    """Return the ordered raw literals that make up one bootstrap program."""
    start = source.find(marker)
    if start < 0:
        raise RuntimeError(f"bootstrap marker not found: {marker}")
    scope = source[start:]
    compile_start = scope.find("auto script = v8::Script::Compile")
    if compile_start >= 0:
        scope = scope[:compile_start]
    matches = re.findall(r'R"JS\((.*?)\)JS"', scope, re.DOTALL)
    if not matches:
        raise RuntimeError(f"raw JavaScript literal not found after: {marker}")
    oversized = [len(part.encode("utf-8")) for part in matches
                 if len(part.encode("utf-8")) > MAX_RAW_LITERAL_BYTES]
    if oversized:
        raise RuntimeError(
            f"raw JavaScript literal after {marker} exceeds "
            f"{MAX_RAW_LITERAL_BYTES} bytes: {oversized}"
        )
    return matches


def extract(source: str, marker: str) -> str:
    return "".join(extract_parts(source, marker)).strip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    source = args.input.read_text(encoding="utf-8")
    programs = [extract(source, marker) for marker in MARKERS]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "// Generated from webscene_v8_runtime.cpp; do not edit.\n"
        + "\n".join(programs),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
