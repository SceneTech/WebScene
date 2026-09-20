#!/usr/bin/env python3
"""Source contract for the opt-in installed Linux Runtime SDK."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PRODUCER = (
    ROOT / "src/WebScene.Sdk/cmake/WebSceneLinuxSDK.cmake"
).read_text()
CONFIG = (
    ROOT / "src/WebScene.Sdk/cmake/WebSceneLinuxConfig.cmake"
).read_text()
GUIDE = (ROOT / "docs/guides/linux-native-headless-sdk.md").read_text()
PACKAGE_AUDIT = (
    ROOT / "eng/sdk/verify-linux-runtime-package.py"
).read_text()


class LinuxRuntimeSdkSourceContract(unittest.TestCase):
    def test_runtime_is_explicit_and_default_native_profile_is_preserved(self):
        self.assertIn(
            'option(WEBSCENE_SDK_RUNTIME "Include the optional JavaScript Runtime component" OFF)',
            PRODUCER,
        )
        self.assertIn("if(WEBSCENE_SDK_RUNTIME)", PRODUCER)
        self.assertIn("WebScene_SDK_RUNTIME ${WEBSCENE_SDK_RUNTIME}", PRODUCER)

    def test_installs_complete_runtime_link_and_data_closure(self):
        for value in (
            "webscene_native_engine",
            "webscene_media",
            "ixwebsocket",
            "mbedcrypto",
            "libv8_monolith.a",
            "icudtl.dat",
            "webscene_bootstrap_snapshot.bin",
            "webscene_bootstrap_snapshot.meta",
            "libEGL${CMAKE_SHARED_LIBRARY_SUFFIX}",
            "libGLESv2${CMAKE_SHARED_LIBRARY_SUFFIX}",
        ):
            self.assertIn(value, PRODUCER)

    def test_consumer_fails_closed_and_exports_ordered_runtime_closure(self):
        self.assertIn("if(WebScene_SDK_RUNTIME)", CONFIG)
        self.assertIn("Incomplete Linux Runtime SDK", CONFIG)
        self.assertIn("_ws_linux_static(Runtime libwebscene_native_engine.a)", CONFIG)
        runtime_link = CONFIG.index(
            "WebScene::Runtime PROPERTY INTERFACE_LINK_LIBRARIES"
        )
        for dependency in (
            "WebScene::_Media",
            "WebScene::_WebSocket",
            "WebScene::_Parser",
            "WebScene::_V8",
            "WebScene::_Crypto",
            "WebScene::WebGPU",
            "WebScene::_AngleEGL",
            "WebScene::_AngleGLESv2",
            "OpenSSL::SSL",
            "ZLIB::ZLIB",
        ):
            self.assertIn(dependency, CONFIG[runtime_link:])

    def test_guide_keeps_code_oss_on_unchanged_runtime_path(self):
        self.assertIn("Code OSS", GUIDE)
        self.assertIn("Do not port", GUIDE)
        self.assertIn("WEBSCENE_SDK_RUNTIME=ON", GUIDE)

    def test_package_gate_budgets_disk_and_duplicate_payloads(self):
        for contract in (
            "DEFAULT_LOGICAL_BYTES",
            "DEFAULT_ALLOCATED_BYTES",
            "DEFAULT_ENTRY_COUNT",
            "FORBIDDEN_SUFFIXES",
            "duplicate large payloads",
            "st_blocks * 512",
            "webscene-linux-runtime-package-v1",
        ):
            self.assertIn(contract, PACKAGE_AUDIT)
        self.assertIn("verify-linux-runtime-package.py", PRODUCER)


if __name__ == "__main__":
    unittest.main()
