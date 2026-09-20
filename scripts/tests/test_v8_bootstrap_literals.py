#!/usr/bin/env python3
"""Regression coverage for portable V8 bootstrap source literals."""

from __future__ import annotations

import hashlib
import importlib.util
import pathlib
import re
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
SERVICE_WORKER_PATH = RUNTIME_PATH.with_name(
    "webscene_v8_runtime_service_workers.inc"
)
AUDIO_PLATFORM_PATH = (
    ROOT
    / "experiments"
    / "WebScene.NativeEngine.Probe"
    / "native"
    / "media"
    / "audio_platform.js.inc"
)
INDEXEDDB_PATH = (
    ROOT
    / "experiments"
    / "WebScene.NativeEngine.Probe"
    / "native"
    / "webscene_indexeddb_compatibility.h"
)

SPEC = importlib.util.spec_from_file_location("extract_bootstraps", EXTRACTOR_PATH)
assert SPEC is not None and SPEC.loader is not None
EXTRACTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACTOR)


class V8BootstrapLiteralTests(unittest.TestCase):
    def test_editor_platform_bootstrap_is_portable_and_byte_equivalent(self) -> None:
        source = RUNTIME_PATH.read_text(encoding="utf-8")
        marker = "void install_editor_web_platform_globals"
        parts = EXTRACTOR.extract_parts(source, marker)

        self.assertGreater(len(parts), 6)
        self.assertTrue(
            all(
                len(part.encode("utf-8")) <= EXTRACTOR.MAX_RAW_LITERAL_BYTES
                for part in parts
            )
        )
        self.assertEqual(
            "".join(parts).strip() + "\n",
            EXTRACTOR.extract(source, marker),
        )

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

    def test_service_worker_bootstrap_literals_are_portable(self) -> None:
        source = SERVICE_WORKER_PATH.read_text(encoding="utf-8")
        parts = re.findall(r'R"JS\((.*?)\)JS"', source, re.DOTALL)

        self.assertGreater(len(parts), 1)
        self.assertTrue(
            all(
                len(part.encode("utf-8")) <= EXTRACTOR.MAX_RAW_LITERAL_BYTES
                for part in parts
            )
        )

    def test_audio_platform_literals_are_portable_and_byte_exact(self) -> None:
        source = AUDIO_PLATFORM_PATH.read_text(encoding="utf-8")
        parts = re.findall(r'R"AUDIOJS\((.*?)\)AUDIOJS"', source, re.DOTALL)
        joined = "".join(parts)

        self.assertGreater(len(parts), 1)
        self.assertTrue(
            all(
                len(part.encode("utf-8")) <= EXTRACTOR.MAX_RAW_LITERAL_BYTES
                for part in parts
            )
        )
        self.assertEqual(
            hashlib.sha256(joined.encode("utf-8")).hexdigest(),
            "3dd3dccc775d895fad213a3622ae8a678f6d5557af842a40060f66bcc9a3ce59",
        )

    def test_indexeddb_bootstrap_is_portable_and_byte_exact(self) -> None:
        source = INDEXEDDB_PATH.read_text(encoding="utf-8")
        parts = EXTRACTOR.extract_parts(
            source, "indexeddb_compatibility_source"
        )
        joined = "".join(parts)

        self.assertGreater(len(parts), 1)
        self.assertTrue(
            all(
                len(part.encode("utf-8")) <= EXTRACTOR.MAX_RAW_LITERAL_BYTES
                for part in parts
            )
        )
        self.assertEqual(
            hashlib.sha256(joined.encode("utf-8")).hexdigest(),
            "a3c907644e9b6c9d63eddfbd6a591802e99bbd3249a3b1437788ce4eb1c73b64",
        )


if __name__ == "__main__":
    unittest.main()
