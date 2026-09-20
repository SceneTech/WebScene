#!/usr/bin/env python3
"""Source contract for the two fail-closed Windows Runtime package layouts."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / (
    "packaging/WebScene.NativeEngine.Runtime/cmake/"
    "WebSceneWindowsConfig.cmake"
)


class WindowsSdkRelocationSourceTests(unittest.TestCase):
    def test_selects_one_complete_runtime_layout(self):
        source = CONFIG.read_text(encoding="utf-8")
        for contract in (
            '"${_webscene_prefix}/bin"',
            '"${_webscene_package_root}/runtimes/${WebScene_PACKAGE_RID}/native"',
            "_webscene_complete_runtime_directories",
            "_webscene_complete_runtime_directory_count EQUAL 1",
            "the DLL and manifest must move together",
        ):
            self.assertIn(contract, source)

    def test_preserves_identity_hashes_and_publishes_selected_layout(self):
        source = CONFIG.read_text(encoding="utf-8")
        for contract in (
            "runtimeIdentifier",
            "architecture",
            "abiVersion",
            "importLibrarySha256",
            "cHeaderSha256",
            "file(SHA256",
            'WebScene_RUNTIME_LAYOUT "installed-sdk"',
            'WebScene_RUNTIME_LAYOUT "nuget"',
            'WebScene_RUNTIME_DIRECTORY "${_webscene_runtime_directory}"',
        ):
            self.assertIn(contract, source)


if __name__ == "__main__":
    unittest.main()
