from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]


class DawnNativeDeviceBridgeTests(unittest.TestCase):
    def test_v2_validates_public_inputs_before_live_registry_lookup(self):
        source = (ROOT / "eng/graphics/dawn_native_device.cpp").read_text()
        query = source.split("websceneDawnQueryVulkanDeviceV2(", 1)[1]
        self.assertLess(query.index("!token||!result"), query.index("find_device(token)"))
        self.assertLess(query.index("result->struct_size"), query.index("find_device(token)"))
        self.assertLess(query.index("result->version"), query.index("find_device(token)"))
        self.assertLess(query.index("find_device(token)"), query.index("base->GetGuard()"))

    def test_v3_query_and_acquire_validate_before_private_dereference(self):
        source = (ROOT / "eng/graphics/dawn_native_device.cpp").read_text()
        for signature in ("websceneDawnQueryVulkanDeviceV3(",
                          "websceneDawnAcquireVulkanQueueV3("):
            body = source.split(signature, 1)[1]
            self.assertLess(body.index("!"), body.index("find_device("))
            self.assertLess(body.index("struct_size"), body.index("find_device("))
            self.assertLess(body.index("version"), body.index("find_device("))
            self.assertLess(body.index("find_device("), body.index("base->GetGuard()"))

    def test_v3_holds_dawns_device_guard_and_balanced_device_references(self):
        source = (ROOT / "eng/graphics/dawn_native_device.cpp").read_text()
        for token in (
            "dawn::native::DeviceGuard guard",
            "device_->APIAddRef()",
            "device_->APIRelease()",
            "thread_local queue_access_token* current_queue_access",
            "QUEUE_ACCESS_NESTED_V3",
            "QUEUE_ACCESS_WRONG_THREAD_V3",
            "delete access",
        ):
            self.assertIn(token, source)

    def test_windows_query_validates_before_private_dereference(self):
        source = (ROOT / "eng/graphics/dawn_native_device.cpp").read_text()
        query = source.split("websceneDawnQueryD3D12DeviceV1(", 1)[1]
        self.assertLess(query.index("!token||!result"), query.index("find_device(token)"))
        self.assertLess(query.index("result->struct_size"), query.index("find_device(token)"))
        self.assertLess(query.index("result->version"), query.index("find_device(token)"))
        self.assertLess(query.index("find_device(token)"), query.index("base->GetGuard()"))
        for token in ("GetD3D12Device()", "GetD3D12CommandQueue()",
                      "GetAdapterLuid()", "WEBSCENE_DAWN_D3D12_REQUIRED_V1"):
            self.assertIn(token, query)

    def test_cpp_wrapper_is_lexical_and_sdk_targets_publish_v3(self):
        wrapper = (ROOT / "experiments/WebScene.NativeEngine.Probe/native/graphics/"
                   "dawn_linux_external_provider.h").read_text()
        for token in (
            "class dawn_linux_external_queue_access final",
            "with_dawn_linux_external_queue_access",
            "websceneDawnAcquireVulkanQueueV3",
            "websceneDawnReleaseVulkanQueueV3",
            "dawn_linux_external_queue_access&&)=delete",
        ):
            self.assertIn(token, wrapper)
        for path in (
            "src/WebScene.Sdk/cmake/LinuxSDK.cmake",
            "src/WebScene.Sdk/cmake/WebSceneLinuxSDK.cmake",
            "src/WebScene.Sdk/cmake/WebSceneLinuxConfig.cmake",
        ):
            self.assertIn(
                "WEBSCENE_DAWN_NATIVE_DEVICE_ABI_VERSION_V3=3",
                (ROOT / path).read_text(),
            )

    def test_registry_is_compatible_with_dawns_no_exception_build(self):
        source = (ROOT / "eng/graphics/dawn_native_device.cpp").read_text()
        self.assertIn("new(std::nothrow) live_devices::entry", source)
        self.assertNotIn("catch(", source.replace(" ", ""))
        self.assertNotIn("std::unordered_map", source)

    def test_v1_v2_and_v3_are_staged_hashed_and_exported_together(self):
        header = (ROOT / "experiments/WebScene.NativeEngine.Probe/native/graphics/"
                  "webscene/dawn_native_device.h").read_text()
        symbols = (ROOT / "eng/graphics/DawnSymbolBoundary.cmake").read_text()
        exports = (ROOT / "eng/graphics/dawn_exports.py").read_text()
        builder = (ROOT / "eng/graphics/build.py").read_text()
        verifier = (ROOT / "eng/graphics/verify-sdk.py").read_text()
        cache = (ROOT / ".github/actions/graphics-sdk/action.yml").read_text()
        for version in ("V1", "V2", "V3"):
            name = f"websceneDawnQueryVulkanDevice{version}"
            self.assertIn(name, header)
            self.assertIn(name, symbols)
            self.assertIn(name, exports)
        for name in ("websceneDawnAcquireVulkanQueueV3",
                     "websceneDawnReleaseVulkanQueueV3"):
            self.assertIn(name, header)
            self.assertIn(name, symbols)
            self.assertIn(name, exports)
        for name in ("dawn_native_device.h", "dawn_native_device.cpp",
                     "dawn_native_device_internal.h"):
            self.assertIn(name, builder)
            self.assertIn(name, verifier)
            self.assertIn(name, cache)

    def test_windows_bridge_is_built_staged_verified_and_exported(self):
        header = (ROOT / "experiments/WebScene.NativeEngine.Probe/native/graphics/"
                  "webscene/dawn_native_device.h").read_text()
        symbols = (ROOT / "eng/graphics/DawnSymbolBoundary.cmake").read_text()
        exports = (ROOT / "eng/graphics/dawn_exports.py").read_text()
        builder = (ROOT / "eng/graphics/build.py").read_text()
        verifier = (ROOT / "eng/graphics/verify-sdk.py").read_text()
        name = "websceneDawnQueryD3D12DeviceV1"
        self.assertIn(name, header)
        self.assertIn(name, exports)
        self.assertIn("WIN32 OR (UNIX AND NOT APPLE)", symbols)
        self.assertIn("WEBSCENE_DAWN_NATIVE_DEVICE_IMPLEMENTATION=1", symbols)
        self.assertIn('(\"linux-\", \"win-\")', builder)
        self.assertIn('(\"linux-\", \"win-\")', verifier)


if __name__ == "__main__":
    unittest.main()
