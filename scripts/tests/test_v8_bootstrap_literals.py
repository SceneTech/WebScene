#!/usr/bin/env python3
"""Regression coverage for portable V8 bootstrap source literals."""

from __future__ import annotations

import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
EXTRACTOR_PATH = ROOT / "tools" / "v8-snapshot" / "extract_bootstraps.py"
RUNTIME_PATH = (
    ROOT
    / "experiments"
    / "WebScene.NativeEngine.Probe"
    / "native"
    / "webscene_v8_runtime.cpp"
)

SPEC = importlib.util.spec_from_file_location("extract_bootstraps", EXTRACTOR_PATH)
assert SPEC is not None and SPEC.loader is not None
EXTRACTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACTOR)


class V8BootstrapLiteralTests(unittest.TestCase):
    def test_fetch_bootstrap_is_portable_and_byte_equivalent(self) -> None:
        source = RUNTIME_PATH.read_text(encoding="utf-8")
        parts = EXTRACTOR.extract_parts(source, "void install_fetch_globals")

        self.assertGreater(len(parts), 1)
        self.assertTrue(
            all(
                len(part.encode("utf-8")) <= EXTRACTOR.MAX_RAW_LITERAL_BYTES
                for part in parts
            )
        )
        self.assertEqual(
            "".join(parts).strip() + "\n",
            EXTRACTOR.extract(source, "void install_fetch_globals"),
        )


if __name__ == "__main__":
    unittest.main()
