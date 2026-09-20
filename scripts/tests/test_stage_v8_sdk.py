#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "stage_v8_sdk", ROOT / "scripts/stage_v8_sdk.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class StageV8SDKTests(unittest.TestCase):
    def fixture(self, root: Path, *, patched: bool = True) -> Path:
        source = root / "v8"
        (source / "include").mkdir(parents=True)
        (source / "third_party/partition_alloc/src").mkdir(parents=True)
        (source / "third_party/icu").mkdir(parents=True)
        (source / "include/v8.h").write_text("// public\n")
        (source / "include/v8-version.h").write_text("#define V8_MAJOR_VERSION 15\n")
        bridge = "virtual void consoleAPICalled();\n" if patched else "// upstream\n"
        (source / "include/v8-inspector.h").write_text(bridge)
        (source / "LICENSE").write_text("V8 license\n")
        (source / "third_party/partition_alloc/src/partition_alloc.h").write_text(
            "// partition alloc\n"
        )
        (source / "third_party/icu/LICENSE").write_text("ICU license\n")
        subprocess.run(["git", "init", "-q", source], check=True)
        subprocess.run(["git", "-C", source, "config", "user.name", "fixture"], check=True)
        subprocess.run(
            ["git", "-C", source, "config", "user.email", "fixture@example.invalid"],
            check=True,
        )
        subprocess.run(["git", "-C", source, "add", "."], check=True)
        subprocess.run(["git", "-C", source, "commit", "-qm", "fixture"], check=True)
        return source

    def test_stages_patched_relocatable_bounded_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.fixture(root)
            output = root / "sdk"
            manifest = MODULE.stage(source, output)
            self.assertIn("virtual void consoleAPICalled", (
                output / "include/v8-inspector.h"
            ).read_text())
            self.assertTrue((output / "third_party/icu/LICENSE").is_file())
            self.assertTrue((output / "third_party/partition_alloc/src/partition_alloc.h").is_file())
            self.assertFalse(any(path.is_symlink() for path in output.rglob("*")))
            self.assertFalse(any(path.name == ".git" for path in output.rglob("*")))
            recorded = json.loads((output / "v8-sdk-stage.json").read_text())
            self.assertEqual(manifest, recorded)
            self.assertLess(manifest["logicalBytes"], MODULE.MAX_LOGICAL_BYTES)
            self.assertLess(manifest["fileCount"], MODULE.MAX_FILES)

    def test_rejects_upstream_header_and_nonempty_destination(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.fixture(root, patched=False)
            with self.assertRaisesRegex(ValueError, "console bridge"):
                MODULE.stage(source, root / "sdk")
            (source / "include/v8-inspector.h").write_text(
                "virtual void consoleAPICalled();\n"
            )
            output = root / "occupied"
            output.mkdir()
            (output / "keep").write_text("user data\n")
            with self.assertRaisesRegex(ValueError, "absent or empty"):
                MODULE.stage(source, output)
            self.assertEqual("user data\n", (output / "keep").read_text())


if __name__ == "__main__":
    unittest.main()
