#!/usr/bin/env python3
"""Generate native and managed CSS metadata from the checked-in JSON catalog."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "experiments/WebScene.NativeEngine.Probe/native/css_property_metadata.json"
DEFAULT_OUTPUT = ROOT / "experiments/WebScene.NativeEngine.Probe/native/generated/webscene_css_property_metadata.inc"
DEFAULT_NATIVE_SUPPORTED_OUTPUT = (
    ROOT / "experiments/WebScene.NativeEngine.Probe/native/generated/webscene_css_supported_properties.inc")
DEFAULT_NATIVE_IDS_OUTPUT = (
    ROOT / "experiments/WebScene.NativeEngine.Probe/native/generated/webscene_css_property_ids.inc")
DEFAULT_NATIVE_IDENTITY_OUTPUT = (
    ROOT / "experiments/WebScene.NativeEngine.Probe/native/generated/webscene_css_property_identity.inc")
DEFAULT_MANAGED_OUTPUT = ROOT / "src/WebScene.Css/CssPropertyMetadata.Generated.cs"
ALLOWED_MASKS = {
    "inline_top", "inline_right", "inline_bottom", "inline_left",
    "inline_margin", "inline_padding", "inline_border", "inline_gap", "inline_overflow",
}
GRAMMAR_FAMILIES = (
    "keyword", "componentList", "length", "lengthList4", "lengthList2", "color", "complex")


class Catalog:
    def __init__(
        self,
        properties: list[dict[str, object]],
        managed_known_properties: list[str],
        supported_property_extras: list[str],
        native_property_ids: list[dict[str, object]],
        native_storage_only_properties: list[str],
        native_grammar_families: dict[str, list[str]],
        native_inherited_properties: list[str],
        native_maskless_property_ids: list[str],
    ) -> None:
        self.properties = properties
        self.managed_known_properties = managed_known_properties
        self.supported_property_extras = supported_property_extras
        self.native_property_ids = native_property_ids
        self.native_storage_only_properties = native_storage_only_properties
        self.native_grammar_families = native_grammar_families
        self.native_inherited_properties = native_inherited_properties
        self.native_maskless_property_ids = native_maskless_property_ids


def _load_name_list(payload: dict[str, object], key: str, *, allow_empty: bool = False) -> list[str]:
    values = payload.get(key)
    if not isinstance(values, list) or (not values and not allow_empty):
        qualifier = "an array" if allow_empty else "a non-empty array"
        raise ValueError(f"{key} must be {qualifier}")

    names: list[str] = []
    seen: set[str] = set()
    for value in values:
        if not isinstance(value, str) or not value or value.lower() != value:
            raise ValueError(f"{key} name must be lower-case: {value!r}")
        if value in seen:
            raise ValueError(f"duplicate {key} name: {value}")
        seen.add(value)
        names.append(value)
    return names


def load_catalog(path: Path) -> Catalog:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError("CSS property metadata must be an object")
    if payload.get("version") != 1:
        raise ValueError("css property metadata version must be 1")
    managed_known_properties = _load_name_list(payload, "managedKnownProperties")
    supported_property_extras = _load_name_list(
        payload, "supportedPropertyExtras", allow_empty=True)
    native_storage_only_properties = _load_name_list(
        payload, "nativeStorageOnlyProperties", allow_empty=True)
    native_inherited_properties = _load_name_list(
        payload, "nativeInheritedProperties", allow_empty=True)
    native_property_ids = payload.get("nativePropertyIds")
    if not isinstance(native_property_ids, list) or len(native_property_ids) < 3:
        raise ValueError("nativePropertyIds must contain unknown, custom, and typed properties")
    native_ids: set[str] = set()
    native_names: set[str] = set()
    for index, entry in enumerate(native_property_ids):
        if not isinstance(entry, dict):
            raise ValueError("each native property identity must be an object")
        property_id = entry.get("id")
        if not isinstance(property_id, str) or re.fullmatch(r"[a-z][a-z0-9_]*", property_id) is None:
            raise ValueError(f"invalid native property id: {property_id!r}")
        if property_id in native_ids:
            raise ValueError(f"duplicate native property id: {property_id}")
        native_ids.add(property_id)
        name = entry.get("name")
        aliases = entry.get("aliases", [])
        if property_id in ("unknown", "custom"):
            if name is not None or aliases:
                raise ValueError(f"special native property id {property_id} cannot have names")
        elif not isinstance(name, str) or not name or name.lower() != name:
            raise ValueError(f"native property {property_id} must have a lower-case name")
        if not isinstance(aliases, list) or not all(isinstance(alias, str) for alias in aliases):
            raise ValueError(f"aliases for native property {property_id} must be an array of strings")
        for native_name in ([name] if name is not None else []) + aliases:
            if not native_name or native_name.lower() != native_name:
                raise ValueError(f"native property name must be lower-case: {native_name!r}")
            if native_name in native_names:
                raise ValueError(f"duplicate native property name or alias: {native_name}")
            native_names.add(native_name)
        if index == 0 and property_id != "unknown":
            raise ValueError("the first native property id must be unknown")
        if index == 1 and property_id != "custom":
            raise ValueError("the second native property id must be custom")
    grammar_payload = payload.get("nativeGrammarFamilies")
    if not isinstance(grammar_payload, dict) or set(grammar_payload) != set(GRAMMAR_FAMILIES):
        raise ValueError(
            "nativeGrammarFamilies must define exactly: " + ", ".join(GRAMMAR_FAMILIES))
    native_grammar_families: dict[str, list[str]] = {}
    classified_ids: set[str] = set()
    for family in GRAMMAR_FAMILIES:
        values = grammar_payload[family]
        if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
            raise ValueError(f"native grammar family {family} must be an array of property ids")
        for property_id in values:
            if property_id not in native_ids:
                raise ValueError(f"native grammar family {family} references unknown id: {property_id}")
            if property_id in ("unknown", "custom"):
                raise ValueError(f"special native property id cannot have a grammar family: {property_id}")
            if property_id in classified_ids:
                raise ValueError(f"duplicate native grammar classification: {property_id}")
            classified_ids.add(property_id)
        native_grammar_families[family] = values
    missing_grammar = native_ids - {"unknown", "custom"} - classified_ids
    if missing_grammar:
        raise ValueError(
            "native property ids lack grammar classification: "
            + ", ".join(sorted(missing_grammar)))
    maskless_values = payload.get("nativeMasklessPropertyIds")
    if not isinstance(maskless_values, list) or not all(
            isinstance(value, str) for value in maskless_values):
        raise ValueError("nativeMasklessPropertyIds must be an array of property ids")
    native_maskless_property_ids: list[str] = []
    maskless_ids: set[str] = set()
    for property_id in maskless_values:
        if property_id not in native_ids:
            raise ValueError(f"native maskless properties reference unknown id: {property_id}")
        if property_id in ("unknown", "custom"):
            raise ValueError(f"special native property id cannot be maskless: {property_id}")
        if property_id in maskless_ids:
            raise ValueError(f"duplicate native maskless property id: {property_id}")
        maskless_ids.add(property_id)
        native_maskless_property_ids.append(property_id)
    entries = payload.get("properties")
    if not isinstance(entries, list) or not entries:
        raise ValueError("properties must be a non-empty array")
    names: set[str] = set()
    known_names = {entry.get("name") for entry in entries if isinstance(entry, dict)}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("each property must be an object")
        name = entry.get("name")
        if not isinstance(name, str) or not name or name.lower() != name:
            raise ValueError(f"property name must be lower-case: {name!r}")
        if name in names:
            raise ValueError(f"duplicate property: {name}")
        names.add(name)
        masks = entry.get("mask")
        if not isinstance(masks, list) or not masks or not all(mask in ALLOWED_MASKS for mask in masks):
            raise ValueError(f"invalid mask list for {name}: {masks!r}")
        alias = entry.get("alias")
        expansion = entry.get("expansion")
        apply_expansion = entry.get("applyExpansion", False)
        if not isinstance(apply_expansion, bool):
            raise ValueError(f"applyExpansion for {name} must be a boolean")
        if alias is not None and expansion is not None:
            raise ValueError(f"{name} cannot define both alias and expansion")
        targets = [alias] if alias is not None else expansion or []
        if alias is not None and not isinstance(alias, str):
            raise ValueError(f"alias for {name} must be a string")
        if expansion is not None and (not isinstance(expansion, list) or len(expansion) not in (2, 4)):
            raise ValueError(f"expansion for {name} must have two or four longhands")
        if apply_expansion and expansion is None:
            raise ValueError(f"{name} applies expansion without defining an expansion")
        for target in targets:
            if target not in known_names:
                raise ValueError(f"{name} references unknown effective property {target!r}")
    supported = set(managed_known_properties + supported_property_extras) | names
    storage_only = set(native_storage_only_properties)
    unknown_storage = storage_only - supported
    if unknown_storage:
        raise ValueError(
            "native storage-only names are not exposed by CSSOM: "
            + ", ".join(sorted(unknown_storage)))
    overlaps = storage_only & native_names
    if overlaps:
        raise ValueError(
            "native CSSOM names cannot be both typed and storage-only: "
            + ", ".join(sorted(overlaps)))
    missing_classification = supported - native_names - storage_only
    if missing_classification:
        raise ValueError(
            "native CSSOM names lack typed or storage-only classification: "
            + ", ".join(sorted(missing_classification)))
    native_primary_names = {
        entry["name"] for entry in native_property_ids if "name" in entry
    }
    missing_primary_exposure = native_primary_names - supported
    if missing_primary_exposure:
        raise ValueError(
            "native typed canonical properties are not CSSOM-supported: "
            + ", ".join(sorted(missing_primary_exposure)))
    unknown_inherited = set(native_inherited_properties) - supported
    if unknown_inherited:
        raise ValueError(
            "native inherited properties are not CSSOM-supported: "
            + ", ".join(sorted(unknown_inherited)))

    return Catalog(
        properties=sorted(entries, key=lambda entry: entry["name"]),
        managed_known_properties=managed_known_properties,
        supported_property_extras=supported_property_extras,
        native_property_ids=native_property_ids,
        native_storage_only_properties=native_storage_only_properties,
        native_grammar_families=native_grammar_families,
        native_inherited_properties=native_inherited_properties,
        native_maskless_property_ids=native_maskless_property_ids,
    )


def generate_native(catalog: Catalog) -> str:
    lines = [
        "// Generated by tools/generate_css_property_metadata.py. Do not edit.",
        "inline constexpr std::array effective_property_metadata_catalog{",
    ]
    for entry in catalog.properties:
        targets = ([entry["alias"]] if "alias" in entry else entry.get("expansion", []))
        padded = list(targets) + [""] * (4 - len(targets))
        mask = " | ".join(entry["mask"])
        quoted = ", ".join(f'"{target}"' for target in padded)
        apply_expansion = "true" if entry.get("applyExpansion", False) else "false"
        lines.append(
            f'    effective_property_metadata{{"{entry["name"]}", {mask}, '
            f'{{{quoted}}}, {len(targets)}U, {apply_expansion}}},'
        )
    lines.extend(["};", ""])
    return "\n".join(lines)


def supported_names(catalog: Catalog) -> list[str]:
    return sorted(set(
        catalog.managed_known_properties
        + catalog.supported_property_extras
        + [entry["name"] for entry in catalog.properties]
    ))


def css_idl_name(name: str) -> str:
    if name == "float":
        return "cssFloat"
    parts = name.split("-")
    return parts[0] + "".join(part[:1].upper() + part[1:] for part in parts[1:])


def generate_native_supported(catalog: Catalog) -> str:
    names = supported_names(catalog)
    property_accessor_count = sum(
        1 + (css_idl_name(name) != name) for name in names)
    lines = [
        "// Generated by tools/generate_css_property_metadata.py. Do not edit.",
        "struct cssom_supported_property_metadata final {",
        "    std::string_view css_name;",
        "    std::string_view idl_name;",
        "};",
        "",
        "inline constexpr std::array cssom_supported_property_catalog{",
    ]
    for name in names:
        lines.append(
            f'    cssom_supported_property_metadata{{"{name}", "{css_idl_name(name)}"}},')
    lines.extend([
        "};",
        "",
        "inline constexpr auto cssom_style_template_property_accessor_count = "
        f"{property_accessor_count}U;",
        "",
    ])
    return "\n".join(lines)


def native_property_names(entry: dict[str, object]) -> list[str]:
    name = entry.get("name")
    return ([] if name is None else [name]) + list(entry.get("aliases", []))


def generate_native_ids(catalog: Catalog) -> str:
    lines = ["// Generated by tools/generate_css_property_metadata.py. Do not edit."]
    for index, entry in enumerate(catalog.native_property_ids):
        assignment = " = 0" if index == 0 else ""
        lines.append(f'{entry["id"]}{assignment},')
    lines.append("")
    return "\n".join(lines)


def generate_native_identity(catalog: Catalog) -> str:
    typed_rows = [
        (name, entry["id"])
        for entry in catalog.native_property_ids
        for name in native_property_names(entry)
    ]
    lines = [
        "// Generated by tools/generate_css_property_metadata.py. Do not edit.",
        "struct native_typed_property_identity final {",
        "    std::string_view name;",
        "    css_property_id id;",
        "};",
        "",
        "inline constexpr std::array native_typed_property_identity_catalog{",
    ]
    for name, property_id in typed_rows:
        lines.append(
            f'    native_typed_property_identity{{"{name}", css_property_id::{property_id}}},')
    lines.extend([
        "};",
        "",
        "inline constexpr std::array<std::string_view, "
        f"{len(catalog.native_storage_only_properties)}> native_storage_only_property_catalog{{",
    ])
    for name in catalog.native_storage_only_properties:
        lines.append(f'    "{name}",')
    lines.extend([
        "};",
        "",
        "inline constexpr std::array native_modeled_property_mask_catalog{",
    ])
    maskless_ids = set(catalog.native_maskless_property_ids)
    for entry in catalog.native_property_ids:
        modeled = entry["id"] not in maskless_ids and entry["id"] not in ("unknown", "custom")
        lines.append("    " + ("true," if modeled else "false,"))
    lines.extend([
        "};",
        "",
        "inline constexpr bool generated_property_has_modeled_mask(",
        "    css_property_id property) noexcept",
        "{",
        "    const auto index = static_cast<size_t>(property);",
        "    return index < native_modeled_property_mask_catalog.size()",
        "        && native_modeled_property_mask_catalog[index];",
        "}",
        "",
        "inline constexpr std::array<std::string_view, "
        f"{len(catalog.native_inherited_properties)}> native_inherited_property_catalog{{",
    ])
    for name in sorted(catalog.native_inherited_properties):
        lines.append(f'    "{name}",')
    lines.extend([
        "};",
        "",
        "inline bool generated_property_inherits_by_default(std::string_view name) noexcept",
        "{",
        "    return std::binary_search(",
        "        native_inherited_property_catalog.begin(),",
        "        native_inherited_property_catalog.end(), name);",
        "}",
        "",
        "enum class native_property_grammar : uint8_t {",
        "    special, keyword, component_list, length, length_list_4, length_list_2, color, complex",
        "};",
        "",
        "inline constexpr std::array native_property_grammar_catalog{",
    ])
    grammar_by_id = {
        property_id: family
        for family, property_ids in catalog.native_grammar_families.items()
        for property_id in property_ids
    }
    grammar_token = {
        "componentList": "component_list",
        "lengthList4": "length_list_4",
        "lengthList2": "length_list_2",
    }
    for entry in catalog.native_property_ids:
        family = grammar_by_id.get(entry["id"], "special")
        lines.append(
            "    native_property_grammar::"
            + grammar_token.get(family, family) + ",")
    lines.extend([
        "};",
        "",
        "inline constexpr native_property_grammar generated_property_grammar(",
        "    css_property_id property) noexcept",
        "{",
        "    const auto index = static_cast<size_t>(property);",
        "    return index < native_property_grammar_catalog.size()",
        "        ? native_property_grammar_catalog[index]",
        "        : native_property_grammar::special;",
        "}",
        "",
        "inline css_property_id generated_property_id_lowercase(std::string_view name) noexcept",
        "{",
    ])
    for name, property_id in typed_rows:
        lines.append(f'    if (name == "{name}") return css_property_id::{property_id};')
    lines.extend([
        "    return css_property_id::unknown;",
        "}",
        "",
    ])
    return "\n".join(lines)


def _append_csharp_array(lines: list[str], name: str, values: list[str]) -> None:
    lines.extend([
        f"    internal static readonly string[] {name} =",
        "    [",
    ])
    for value in values:
        lines.append(f'        "{value}",')
    lines.extend(["    ];", ""])


def generate_managed(catalog: Catalog) -> str:
    lines = [
        "// Generated by tools/generate_css_property_metadata.py. Do not edit.",
        "namespace WebScene.Css;",
        "",
        "internal static class CssGeneratedPropertyMetadata",
        "{",
    ]
    _append_csharp_array(lines, "KnownNames", catalog.managed_known_properties)
    _append_csharp_array(lines, "SupportedNames", supported_names(catalog))
    lines.extend([
        "    internal static bool TryGetKnownId(string name, out int id)",
        "    {",
        "        id = name switch",
        "        {",
    ])
    for index, name in enumerate(catalog.managed_known_properties):
        lines.append(f'            "{name}" => {index},')
    lines.extend([
        "            _ => -1,",
        "        };",
        "        return id >= 0;",
        "    }",
        "}",
        "",
    ])
    return "\n".join(lines)


# Retain the original helper name for callers that only generate the native table.
def generate(catalog: Catalog) -> str:
    return generate_native(catalog)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--native-supported-output", type=Path, default=DEFAULT_NATIVE_SUPPORTED_OUTPUT)
    parser.add_argument("--native-ids-output", type=Path, default=DEFAULT_NATIVE_IDS_OUTPUT)
    parser.add_argument(
        "--native-identity-output", type=Path, default=DEFAULT_NATIVE_IDENTITY_OUTPUT)
    parser.add_argument("--managed-output", type=Path, default=DEFAULT_MANAGED_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    catalog = load_catalog(args.input)
    rendered_native = generate_native(catalog)
    rendered_native_supported = generate_native_supported(catalog)
    rendered_native_ids = generate_native_ids(catalog)
    rendered_native_identity = generate_native_identity(catalog)
    rendered_managed = generate_managed(catalog)
    if args.check:
        stale: list[Path] = []
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered_native:
            stale.append(args.output)
        if (not args.native_supported_output.exists()
                or args.native_supported_output.read_text(encoding="utf-8")
                != rendered_native_supported):
            stale.append(args.native_supported_output)
        if (not args.native_ids_output.exists()
                or args.native_ids_output.read_text(encoding="utf-8") != rendered_native_ids):
            stale.append(args.native_ids_output)
        if (not args.native_identity_output.exists()
                or args.native_identity_output.read_text(encoding="utf-8")
                != rendered_native_identity):
            stale.append(args.native_identity_output)
        if (not args.managed_output.exists()
                or args.managed_output.read_text(encoding="utf-8") != rendered_managed):
            stale.append(args.managed_output)
        if stale:
            joined = ", ".join(str(path) for path in stale)
            raise SystemExit(f"stale generated CSS property metadata: {joined}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered_native, encoding="utf-8")
    args.native_supported_output.parent.mkdir(parents=True, exist_ok=True)
    args.native_supported_output.write_text(rendered_native_supported, encoding="utf-8")
    args.native_ids_output.parent.mkdir(parents=True, exist_ok=True)
    args.native_ids_output.write_text(rendered_native_ids, encoding="utf-8")
    args.native_identity_output.parent.mkdir(parents=True, exist_ok=True)
    args.native_identity_output.write_text(rendered_native_identity, encoding="utf-8")
    args.managed_output.parent.mkdir(parents=True, exist_ok=True)
    args.managed_output.write_text(rendered_managed, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
