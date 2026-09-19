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

    def test_registry_is_compatible_with_dawns_no_exception_build(self):
        source = (ROOT / "eng/graphics/dawn_native_device.cpp").read_text()
        self.assertIn("new(std::nothrow) live_devices::entry", source)
        self.assertNotIn("catch(", source.replace(" ", ""))
        self.assertNotIn("std::unordered_map", source)

    def test_v1_and_v2_are_staged_hashed_and_exported_together(self):
        header = (ROOT / "experiments/WebScene.NativeEngine.Probe/native/graphics/"
                  "webscene/dawn_native_device.h").read_text()
        symbols = (ROOT / "eng/graphics/DawnSymbolBoundary.cmake").read_text()
        exports = (ROOT / "eng/graphics/dawn_exports.py").read_text()
        builder = (ROOT / "eng/graphics/build.py").read_text()
        verifier = (ROOT / "eng/graphics/verify-sdk.py").read_text()
        cache = (ROOT / ".github/actions/graphics-sdk/action.yml").read_text()
        for version in ("V1", "V2"):
            name = f"websceneDawnQueryVulkanDevice{version}"
            self.assertIn(name, header)
            self.assertIn(name, symbols)
            self.assertIn(name, exports)
        for name in ("dawn_native_device.h", "dawn_native_device.cpp",
                     "dawn_native_device_internal.h"):
            self.assertIn(name, builder)
            self.assertIn(name, verifier)
            self.assertIn(name, cache)


if __name__ == "__main__":
    unittest.main()
