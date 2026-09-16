"""Runtime packaging must not fall back to a developer PATH compiler."""
import importlib.util
import io
import json
import os
import stat
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).parents[1]
sys.path.insert(0, str(HERE))
spec = importlib.util.spec_from_file_location("graphics_windows_runtime", HERE / "build.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class WindowsRuntimeTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows read-only file semantics")
    def test_rebuilding_sdk_removes_readonly_generated_licenses(self):
        with tempfile.TemporaryDirectory() as temp:
            sdk = Path(temp) / "sdk"
            sdk.mkdir()
            license_file = sdk / "LICENSE"
            license_file.write_text("notice")
            license_file.chmod(stat.S_IREAD)
            builder.remove_sdk(sdk)
            self.assertFalse(sdk.exists())

    def test_wrong_compiler_is_rejected_before_any_network_or_staging(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            compiler = root / json.loads((HERE / "windows-runtime.json").read_text())["compiler"]
            compiler.parent.mkdir(parents=True)
            compiler.write_bytes(b"foreign compiler")
            with patch.object(builder.urllib.request, "urlopen") as download:
                with self.assertRaisesRegex(ValueError, "Windows runtime requires SDK"):
                    builder.stage_windows_runtime(SimpleNamespace(windows_sdk=root), root / "sdk")
                download.assert_not_called()
            self.assertFalse((root / "sdk").exists())

    def test_wrong_license_does_not_silently_accept_changed_terms(self):
        pin = json.loads((HERE / "windows-runtime.json").read_text())
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            compiler = root / pin["compiler"]
            compiler.parent.mkdir(parents=True)
            compiler.write_bytes(b"fixture")
            sdk = root / "sdk"
            (sdk / "bin").mkdir(parents=True)
            with patch.object(builder, "sha", side_effect=[pin["sha256"], "wrong-license"]), \
                    patch.object(builder.urllib.request, "urlopen", return_value=io.BytesIO(b"changed")):
                with self.assertRaisesRegex(ValueError, "license checksum mismatch"):
                    builder.stage_windows_runtime(SimpleNamespace(windows_sdk=root), sdk)
            self.assertFalse((sdk / "webscene-graphics-package.json").exists())

    def test_windows_build_sdk_requires_complete_x86_and_x64_toolchains(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pin_path = root / "windows-runtime.json"
            pin_path.write_text(json.dumps({
                "sdkVersion": "10.0.target.0",
                "x86EnvironmentSdkVersion": "10.0.x86.0",
                "angleSourceSdkVersion": "10.0.source.0",
            }))
            required = builder.windows_build_sdk_required_files("10.0.target.0", "x64")
            required += builder.windows_build_sdk_required_files("10.0.x86.0", "x86")
            for relative in required:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")

            version, x86_version, source_version = builder.resolve_windows_build_sdk(
                SimpleNamespace(windows_sdk=root), pin_path)
            self.assertEqual(version, "10.0.target.0")
            self.assertEqual(x86_version, "10.0.x86.0")
            self.assertEqual(source_version, "10.0.source.0")

            (root / required[-1]).unlink()
            with self.assertRaisesRegex(ValueError, "bin/.*/x86/rc.exe"):
                builder.resolve_windows_build_sdk(
                    SimpleNamespace(windows_sdk=root), pin_path)

    def test_angle_sdk_patch_is_bounded_and_restores_upstream_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            paths = [
                source / "build/toolchain/win/setup_toolchain.py",
                source / "build/vs_toolchain.py",
            ]
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"SDK_VERSION = '10.0.source.0'\n")
            paths[0].write_bytes(
                paths[0].read_bytes() + b"  args.append(SDK_VERSION)\n")
            originals = {path: path.read_bytes() for path in paths}

            with builder.patched_angle_windows_sdk(
                    source, "10.0.x86.0", "10.0.source.0"):
                self.assertIn(
                    b"args.append('10.0.x86.0' if cpu == 'x86' else SDK_VERSION)",
                    paths[0].read_bytes())
                self.assertNotIn(b"args.append(SDK_VERSION)", paths[0].read_bytes())
                for path in paths:
                    self.assertIn(b"SDK_VERSION = '10.0.source.0'", path.read_bytes())

            self.assertEqual({path: path.read_bytes() for path in paths}, originals)

    def test_angle_sdk_patch_rejects_unreviewed_upstream_pin(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            for relative in ("build/toolchain/win/setup_toolchain.py", "build/vs_toolchain.py"):
                path = source / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("SDK_VERSION = 'changed-upstream'\n")
            with self.assertRaisesRegex(ValueError, "pin changed unexpectedly"):
                with builder.patched_angle_windows_sdk(
                        source, "10.0.x86.0", "10.0.source.0"):
                    self.fail("unexpected patch context")

    def test_angle_sdk_patch_rejects_unreviewed_selection_code(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            setup = source / "build/toolchain/win/setup_toolchain.py"
            vs_toolchain = source / "build/vs_toolchain.py"
            setup.parent.mkdir(parents=True)
            setup.write_text("SDK_VERSION = '10.0.source.0'\nchanged_selection()\n")
            vs_toolchain.write_text("SDK_VERSION = '10.0.source.0'\n")
            with self.assertRaisesRegex(ValueError, "selection changed unexpectedly"):
                with builder.patched_angle_windows_sdk(
                        source, "10.0.x86.0", "10.0.source.0"):
                    self.fail("unexpected patch context")


if __name__ == "__main__":
    unittest.main()
