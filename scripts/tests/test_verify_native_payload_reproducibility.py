from __future__ import annotations

import importlib.util
import pathlib
import tempfile
import unittest
import zipfile


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "verify-native-payload-reproducibility.py"
SPEC = importlib.util.spec_from_file_location("verify_native_payload_reproducibility", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class NativePayloadReproducibilityTests(unittest.TestCase):
    def package(self, root: pathlib.Path, name: str, payload: bytes) -> pathlib.Path:
        package = root / name
        with zipfile.ZipFile(package, "w") as archive:
            archive.writestr("runtimes/linux-x64/native/libwebscene_native_engine.so", payload)
            archive.writestr("metadata.txt", name)
        return package

    def test_ignores_package_container_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            first = self.package(root, "first.nupkg", b"same")
            second = self.package(root, "second.nupkg", b"same")
            self.assertEqual(MODULE.payload_hashes(first, "linux-x64"), MODULE.payload_hashes(second, "linux-x64"))

    def test_detects_payload_change(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            first = self.package(root, "first.nupkg", b"first")
            second = self.package(root, "second.nupkg", b"second")
            self.assertNotEqual(MODULE.payload_hashes(first, "linux-x64"), MODULE.payload_hashes(second, "linux-x64"))


if __name__ == "__main__":
    unittest.main()
