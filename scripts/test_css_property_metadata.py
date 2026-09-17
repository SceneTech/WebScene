import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools/generate_css_property_metadata.py"
SPEC = importlib.util.spec_from_file_location("css_property_metadata_generator", MODULE_PATH)
assert SPEC and SPEC.loader
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class CssPropertyMetadataTests(unittest.TestCase):
    def test_checked_in_header_matches_catalog(self):
        entries = GENERATOR.load_catalog(GENERATOR.DEFAULT_INPUT)
        expected = GENERATOR.generate(entries)
        self.assertEqual(
            expected,
            GENERATOR.DEFAULT_OUTPUT.read_text(encoding="utf-8"),
        )

    def test_rejects_alias_to_unknown_property(self):
        payload = {
            "version": 1,
            "properties": [
                {"name": "left", "mask": ["inline_left"]},
                {"name": "logical-left", "mask": ["inline_left"], "alias": "missing"},
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unknown effective property"):
                GENERATOR.load_catalog(path)

    def test_rejects_duplicate_property(self):
        payload = {
            "version": 1,
            "properties": [
                {"name": "left", "mask": ["inline_left"]},
                {"name": "left", "mask": ["inline_left"]},
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate property"):
                GENERATOR.load_catalog(path)

    def test_rejects_invalid_schema_mask_and_expansion_arity(self):
        payloads = [
            {"version": 2, "properties": []},
            {
                "version": 1,
                "properties": [{"name": "left", "mask": ["not_a_mask"]}],
            },
            {
                "version": 1,
                "properties": [
                    {
                        "name": "margin",
                        "mask": ["inline_margin"],
                        "expansion": ["a", "b", "c", "d", "e"],
                    }
                ],
            },
        ]
        for payload in payloads:
            with self.subTest(payload=payload), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "metadata.json"
                path.write_text(json.dumps(payload), encoding="utf-8")
                with self.assertRaises(ValueError):
                    GENERATOR.load_catalog(path)

    def test_rejects_unknown_expansion_target(self):
        payload = {
            "version": 1,
            "properties": [
                {
                    "name": "margin",
                    "mask": ["inline_margin"],
                    "expansion": ["missing"],
                }
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unknown effective property"):
                GENERATOR.load_catalog(path)

    def test_check_rejects_stale_output(self):
        entries = GENERATOR.load_catalog(GENERATOR.DEFAULT_INPUT)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated.inc"
            output.write_text("stale\n", encoding="utf-8")
            diagnostics = io.StringIO()
            with contextlib.redirect_stderr(diagnostics):
                self.assertFalse(GENERATOR._check(output, GENERATOR.generate(entries)))
            self.assertIn("generated output is stale", diagnostics.getvalue())


if __name__ == "__main__":
    unittest.main()
