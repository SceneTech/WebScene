import hashlib
import importlib.util
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
    @staticmethod
    def _payload(properties, known=None, extras=None):
        return {
            "version": 1,
            "nativePropertyIds": [
                {"id": "unknown"},
                {"id": "custom"},
                {"id": "left", "name": "left"},
            ],
            "nativeStorageOnlyProperties": [],
            "managedKnownProperties": known or ["left"],
            "supportedPropertyExtras": extras or [],
            "properties": properties,
        }

    def test_checked_in_outputs_match_catalog(self):
        catalog = GENERATOR.load_catalog(GENERATOR.DEFAULT_INPUT)
        self.assertEqual(
            GENERATOR.generate_native(catalog),
            GENERATOR.DEFAULT_OUTPUT.read_text(encoding="utf-8"),
        )
        self.assertEqual(
            GENERATOR.generate_native_supported(catalog),
            GENERATOR.DEFAULT_NATIVE_SUPPORTED_OUTPUT.read_text(encoding="utf-8"),
        )
        self.assertEqual(
            GENERATOR.generate_native_ids(catalog),
            GENERATOR.DEFAULT_NATIVE_IDS_OUTPUT.read_text(encoding="utf-8"),
        )
        self.assertEqual(
            GENERATOR.generate_native_identity(catalog),
            GENERATOR.DEFAULT_NATIVE_IDENTITY_OUTPUT.read_text(encoding="utf-8"),
        )
        self.assertEqual(
            GENERATOR.generate_managed(catalog),
            GENERATOR.DEFAULT_MANAGED_OUTPUT.read_text(encoding="utf-8"),
        )

    def test_catalog_preserves_known_ids_and_unifies_supported_names(self):
        catalog = GENERATOR.load_catalog(GENERATOR.DEFAULT_INPUT)
        self.assertEqual(105, len(catalog.managed_known_properties))
        self.assertEqual("align-content", catalog.managed_known_properties[0])
        self.assertEqual("grid-template-areas", catalog.managed_known_properties[-1])
        self.assertEqual(
            "fc4d77c515998b6df1f119ff7e09b0b82cc812744c34f1c44c6fd142067b1fb2",
            hashlib.sha256("\n".join(catalog.managed_known_properties).encode()).hexdigest(),
        )
        self.assertEqual(214, len(GENERATOR.supported_names(catalog)))
        self.assertEqual(146, len(catalog.native_property_ids))
        self.assertEqual(54, len(catalog.native_storage_only_properties))
        self.assertEqual(
            "b17b1cd0520ae5e36fd875ed8226b1818508bbe41be9a32540ef58a1b31da036",
            hashlib.sha256("\n".join(
                entry["id"] for entry in catalog.native_property_ids).encode()).hexdigest(),
        )
        native_pairs = [
            f'{name}:{entry["id"]}'
            for entry in catalog.native_property_ids
            for name in GENERATOR.native_property_names(entry)
        ]
        self.assertEqual(201, len(native_pairs))
        self.assertEqual(
            "10bc777d9724b11b8872131e36b8b6eb4b5ae53becdea23f3f1f3e71302bd7f3",
            hashlib.sha256("\n".join(native_pairs).encode()).hexdigest(),
        )
        self.assertEqual(
            "8904af7da9f26749568f951c6303bc9f11511897e3dfae88c15c47da37440c94",
            hashlib.sha256("\n".join(
                catalog.native_storage_only_properties).encode()).hexdigest(),
        )
        self.assertEqual("gridGap", GENERATOR.css_idl_name("grid-gap"))
        self.assertEqual("MozTransform", GENERATOR.css_idl_name("-moz-transform"))
        self.assertEqual("cssFloat", GENERATOR.css_idl_name("float"))

        rendered = GENERATOR.generate_managed(catalog)
        for name in ("inset-block-end", "border-block-width", "padding-block-start"):
            self.assertIn(f'        "{name}",', rendered)

        typed_names = {
            name
            for entry in catalog.native_property_ids
            for name in GENERATOR.native_property_names(entry)
        }
        supported = set(GENERATOR.supported_names(catalog))
        storage_only = set(catalog.native_storage_only_properties)
        self.assertEqual(supported, (typed_names & supported) | storage_only)
        self.assertFalse(typed_names & storage_only)

    def test_rejects_alias_to_unknown_property(self):
        payload = self._payload([
                {"name": "left", "mask": ["inline_left"]},
                {"name": "logical-left", "mask": ["inline_left"], "alias": "missing"},
        ])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unknown effective property"):
                GENERATOR.load_catalog(path)

    def test_rejects_duplicate_property(self):
        payload = self._payload([
                {"name": "left", "mask": ["inline_left"]},
                {"name": "left", "mask": ["inline_left"]},
        ])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate property"):
                GENERATOR.load_catalog(path)

    def test_rejects_duplicate_managed_names(self):
        payload = self._payload(
            [{"name": "left", "mask": ["inline_left"]}],
            known=["left", "left"],
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate managedKnownProperties"):
                GENERATOR.load_catalog(path)

    def test_rejects_duplicate_supported_extras(self):
        payload = self._payload(
            [{"name": "left", "mask": ["inline_left"]}],
            extras=["float", "float"],
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate supportedPropertyExtras"):
                GENERATOR.load_catalog(path)

    def test_rejects_duplicate_native_alias(self):
        payload = self._payload([{"name": "left", "mask": ["inline_left"]}])
        payload["nativePropertyIds"].append(
            {"id": "right", "name": "right", "aliases": ["left"]})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate native property name or alias"):
                GENERATOR.load_catalog(path)

    def test_rejects_unclassified_native_cssom_name(self):
        payload = self._payload(
            [{"name": "left", "mask": ["inline_left"]}],
            extras=["zoom"],
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "lack typed or storage-only classification"):
                GENERATOR.load_catalog(path)

    def test_rejects_typed_storage_only_overlap(self):
        payload = self._payload([{"name": "left", "mask": ["inline_left"]}])
        payload["nativeStorageOnlyProperties"] = ["left"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "both typed and storage-only"):
                GENERATOR.load_catalog(path)


if __name__ == "__main__":
    unittest.main()
