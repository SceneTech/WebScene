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
        self.assertEqual(119, len(catalog.managed_known_properties))
        self.assertEqual("align-content", catalog.managed_known_properties[0])
        self.assertEqual("touch-action", catalog.managed_known_properties[-1])
        self.assertEqual(
            "e7e0aed213f767dbc84f70ba35f9348c1ba705dd9363eeed634928364f8f36ee",
            hashlib.sha256("\n".join(
                catalog.managed_known_properties).encode()).hexdigest(),
        )
        self.assertEqual(257, len(GENERATOR.supported_names(catalog)))
        self.assertEqual(
            "fe13b91ddf2c8ab38f18445e6efe6084a576b9d4ac5d69b49076fa68cb2046d5",
            hashlib.sha256("\n".join(
                GENERATOR.supported_names(catalog)).encode()).hexdigest(),
        )
        self.assertEqual(160, len(catalog.native_property_ids))
        self.assertEqual(62, len(catalog.native_storage_only_properties))
        native_ids = [entry["id"] for entry in catalog.native_property_ids]
        self.assertEqual(
            "c72e585041c378389948483a534bec02fd150ebadab9360773083d0d698dd824",
            hashlib.sha256("\n".join(native_ids).encode()).hexdigest(),
        )
        self.assertEqual("touch_action", native_ids[-1])
        native_pairs = [
            f'{name}:{entry["id"]}'
            for entry in catalog.native_property_ids
            for name in GENERATOR.native_property_names(entry)
        ]
        self.assertEqual(235, len(native_pairs))
        self.assertEqual(
            "80f3c32a9c29b64cfc893b0cef327512d07ffe968aad846ba801a4c64cd4090e",
            hashlib.sha256("\n".join(native_pairs).encode()).hexdigest(),
        )
        self.assertEqual("touchaction:touch_action", native_pairs[-1])
        self.assertEqual(
            "3f5f3616586fd121b580531538674b9002752a94d7c6d01610dc4c43125933c2",
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
                "keyword": 40,
                "componentList": 16,
                "length": 34,
                "lengthList4": 5,
                "lengthList2": 10,
                "color": 9,
                "complex": 44,
            },
            {family: len(ids) for family, ids in catalog.native_grammar_families.items()},
        )
        self.assertEqual(
            "f2fde1b185ddac76bb95618bcede6220beb93614aa62268a866500e92991a666",
            hashlib.sha256("\n".join(grammar_rows).encode()).hexdigest(),
        )
        self.assertEqual(24, len(catalog.native_inherited_properties))
        self.assertEqual(38, len(catalog.native_maskless_property_ids))
        self.assertEqual(
            "36e7c460403184a993e4011932282a264d6a32b8144f91ac1c77f844e0210a35",
            hashlib.sha256("\n".join(
                catalog.native_maskless_property_ids).encode()).hexdigest(),
        )
        self.assertEqual("gridGap", GENERATOR.css_idl_name("grid-gap"))
        self.assertEqual("MozTransform", GENERATOR.css_idl_name("-moz-transform"))
        self.assertEqual("cssFloat", GENERATOR.css_idl_name("float"))
        self.assertIn(
            "cssom_style_template_property_accessor_count = 470U",
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
