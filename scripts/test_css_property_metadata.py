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
            "nativeGrammarFamilies": {
                "keyword": ["left"],
                "componentList": [],
                "length": [],
                "lengthList4": [],
                "lengthList2": [],
                "color": [],
                "complex": [],
            },
            "nativeInheritedProperties": [],
            "nativeMasklessPropertyIds": [],
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
        self.assertEqual(230, len(GENERATOR.supported_names(catalog)))
        self.assertEqual(
            "942b33d7fac2d56ecd448b0d5a5a3d2f34dee6f65cd471251fde7473c1494dd2",
            hashlib.sha256("\n".join(
                GENERATOR.supported_names(catalog)).encode()).hexdigest(),
        )
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
        grammar_by_id = {
            property_id: family
            for family, property_ids in catalog.native_grammar_families.items()
            for property_id in property_ids
        }
        grammar_rows = [
            f'{entry["id"]}:{grammar_by_id.get(entry["id"], "special")}'
            for entry in catalog.native_property_ids
        ]
        self.assertEqual(
            {
                "keyword": 29,
                "componentList": 15,
                "length": 34,
                "lengthList4": 5,
                "lengthList2": 10,
                "color": 9,
                "complex": 42,
            },
            {family: len(ids) for family, ids in catalog.native_grammar_families.items()},
        )
        self.assertEqual(
            "e1b6e60b2a079e9b6fb6856cb4ad10b3a51ee8d13610a63f8a6cfcb913a98d06",
            hashlib.sha256("\n".join(grammar_rows).encode()).hexdigest(),
        )
        self.assertEqual(20, len(catalog.native_inherited_properties))
        self.assertEqual(24, len(catalog.native_maskless_property_ids))
        self.assertEqual(
            "ba22ffe33da3f7535acb7993d24b1b79232089eaf50b2782d47d1d12ccc4b37c",
            hashlib.sha256("\n".join(
                catalog.native_maskless_property_ids).encode()).hexdigest(),
        )
        self.assertEqual("gridGap", GENERATOR.css_idl_name("grid-gap"))
        self.assertEqual("MozTransform", GENERATOR.css_idl_name("-moz-transform"))
        self.assertEqual("cssFloat", GENERATOR.css_idl_name("float"))
        self.assertIn(
            "cssom_style_template_property_accessor_count = 418U",
            GENERATOR.generate_native_supported(catalog),
        )

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

    def test_rejects_duplicate_native_grammar_classification(self):
        payload = self._payload([{"name": "left", "mask": ["inline_left"]}])
        payload["nativeGrammarFamilies"]["complex"] = ["left"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate native grammar classification"):
                GENERATOR.load_catalog(path)

    def test_rejects_missing_native_grammar_classification(self):
        payload = self._payload([{"name": "left", "mask": ["inline_left"]}])
        payload["nativeGrammarFamilies"]["keyword"] = []
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "lack grammar classification"):
                GENERATOR.load_catalog(path)

    def test_rejects_unknown_native_inherited_property(self):
        payload = self._payload([{"name": "left", "mask": ["inline_left"]}])
        payload["nativeInheritedProperties"] = ["unknown-property"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "not CSSOM-supported"):
                GENERATOR.load_catalog(path)

    def test_rejects_unexposed_native_canonical_property(self):
        payload = self._payload([{"name": "left", "mask": ["inline_left"]}])
        payload["nativePropertyIds"][2]["name"] = "right"
        payload["nativePropertyIds"][2]["aliases"] = ["left"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "not CSSOM-supported"):
                GENERATOR.load_catalog(path)

    def test_rejects_unknown_native_maskless_property_id(self):
        payload = self._payload([{"name": "left", "mask": ["inline_left"]}])
        payload["nativeMasklessPropertyIds"] = ["not_an_id"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "reference unknown id"):
                GENERATOR.load_catalog(path)


if __name__ == "__main__":
    unittest.main()
