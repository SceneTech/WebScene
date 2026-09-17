#!/usr/bin/env python3
"""Validate and generate the native effective CSS property metadata table."""

from __future__ import annotations

import argparse
import difflib
import json
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = (
    ROOT
    / "experiments/WebScene.NativeEngine.Probe/native/css_property_metadata.json"
)
DEFAULT_OUTPUT = (
    ROOT
    / "experiments/WebScene.NativeEngine.Probe/native/generated"
    / "webscene_css_property_metadata.inc"
)

_PROPERTY_NAME = re.compile(r"^[a-z][a-z0-9-]*$")
_ENTRY_KEYS = {"name", "mask", "alias", "expansion", "applyExpansion"}
_MASKS = {
    "inline_width",
    "inline_height",
    "inline_left",
    "inline_top",
    "inline_right",
    "inline_bottom",
    "inline_display",
    "inline_position",
    "inline_flex_direction",
    "inline_flex_grow",
    "inline_background",
    "inline_overflow",
    "inline_color",
    "inline_font_size",
    "inline_font_family",
    "inline_font_weight",
    "inline_line_height",
    "inline_text_align",
    "inline_visibility",
    "inline_pointer_events",
    "inline_padding",
    "inline_margin",
    "inline_align_items",
    "inline_opacity",
    "inline_flex_wrap",
    "inline_flex_shrink",
    "inline_align_self",
    "inline_min_width",
    "inline_justify_content",
    "inline_box_sizing",
    "inline_border_radius",
    "inline_transform",
    "inline_white_space",
    "inline_min_height",
    "inline_max_width",
    "inline_max_height",
    "inline_gap",
    "inline_box_shadow",
    "inline_transform_origin",
    "inline_svg_fill",
    "inline_svg_stroke",
    "inline_cursor",
    "inline_letter_spacing",
    "inline_word_spacing",
    "inline_z_index",
    "inline_float",
    "inline_border",
    "inline_flex_basis",
    "inline_grid",
    "inline_transition_property",
    "inline_transition_duration",
    "inline_transition_delay",
    "inline_transition_timing",
    "inline_transition",
    "inline_table_border_model",
    "inline_font_smoothing",
    "inline_background_image",
    "inline_contain",
    "inline_svg_text_anchor",
    "inline_scrollbar_width",
    "inline_scrollbar_color",
    "inline_svg_stroke_width",
    "inline_align_content",
    "inline_accessibility_colors",
    "inline_containment_features",
}


def _object_without_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _require_property_name(value: Any, field: str) -> str:
    if not isinstance(value, str) or not _PROPERTY_NAME.fullmatch(value):
        raise ValueError(f"{field} must be a lower-case CSS property name")
    return value


def load_catalog(path: Path = DEFAULT_INPUT) -> list[dict[str, Any]]:
    try:
        payload = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=_object_without_duplicate_keys,
        )
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid JSON: {error}") from error

    if not isinstance(payload, dict) or set(payload) != {"version", "properties"}:
        raise ValueError("catalog must contain exactly version and properties")
    if type(payload["version"]) is not int or payload["version"] != 1:
        raise ValueError("unsupported catalog version; expected 1")
    if not isinstance(payload["properties"], list) or not payload["properties"]:
        raise ValueError("properties must be a non-empty array")

    entries: list[dict[str, Any]] = []
    names: set[str] = set()
    for index, raw_entry in enumerate(payload["properties"]):
        prefix = f"properties[{index}]"
        if not isinstance(raw_entry, dict):
            raise ValueError(f"{prefix} must be an object")
        unknown = set(raw_entry) - _ENTRY_KEYS
        if unknown:
            raise ValueError(f"{prefix} has unknown fields: {', '.join(sorted(unknown))}")
        if "name" not in raw_entry or "mask" not in raw_entry:
            raise ValueError(f"{prefix} requires name and mask")

        name = _require_property_name(raw_entry["name"], f"{prefix}.name")
        if name in names:
            raise ValueError(f"duplicate property: {name}")
        names.add(name)

        masks = raw_entry["mask"]
        if not isinstance(masks, list) or not masks:
            raise ValueError(f"{prefix}.mask must be a non-empty array")
        if any(not isinstance(mask, str) or mask not in _MASKS for mask in masks):
            raise ValueError(f"{prefix}.mask contains an invalid mask symbol")
        if len(set(masks)) != len(masks):
            raise ValueError(f"{prefix}.mask contains a duplicate mask symbol")

        has_alias = "alias" in raw_entry
        has_expansion = "expansion" in raw_entry
        if has_alias and has_expansion:
            raise ValueError(f"{prefix} cannot contain both alias and expansion")

        targets: list[str] = []
        if has_alias:
            targets = [_require_property_name(raw_entry["alias"], f"{prefix}.alias")]
        elif has_expansion:
            expansion = raw_entry["expansion"]
            if not isinstance(expansion, list) or not 1 <= len(expansion) <= 4:
                raise ValueError(f"{prefix}.expansion must contain one to four properties")
            targets = [
                _require_property_name(value, f"{prefix}.expansion")
                for value in expansion
            ]
            if len(set(targets)) != len(targets):
                raise ValueError(f"{prefix}.expansion contains a duplicate property")

        apply_expansion = raw_entry.get("applyExpansion", False)
        if not isinstance(apply_expansion, bool):
            raise ValueError(f"{prefix}.applyExpansion must be a boolean")
        if apply_expansion and not has_expansion:
            raise ValueError(f"{prefix}.applyExpansion requires expansion")

        entries.append(
            {
                "name": name,
                "mask": list(masks),
                "targets": targets,
                "applyExpansion": apply_expansion,
            }
        )

    for entry in entries:
        for target in entry["targets"]:
            if target not in names:
                raise ValueError(
                    f"unknown effective property {target!r} referenced by {entry['name']!r}"
                )
            if target == entry["name"]:
                raise ValueError(f"property {entry['name']!r} cannot target itself")

    return sorted(entries, key=lambda entry: entry["name"])


def generate(entries: list[dict[str, Any]]) -> str:
    lines = [
        "// Generated by tools/generate_css_property_metadata.py. Do not edit.",
        "inline constexpr std::array effective_property_metadata_catalog{",
    ]
    for entry in entries:
        targets = list(entry["targets"])
        padded_targets = targets + [""] * (4 - len(targets))
        target_values = ", ".join(f'"{target}"' for target in padded_targets)
        masks = " | ".join(entry["mask"])
        apply_expansion = "true" if entry["applyExpansion"] else "false"
        lines.append(
            f'    effective_property_metadata{{"{entry["name"]}", {masks}, '
            f"{{{target_values}}}, {len(targets)}U, {apply_expansion}}},"
        )
    lines.append("};")
    return "\n".join(lines) + "\n"


def _check(output: Path, generated: str) -> bool:
    try:
        existing = output.read_text(encoding="utf-8")
    except FileNotFoundError:
        print(f"generated output is missing: {output}", file=sys.stderr)
        return False
    if existing == generated:
        return True
    print(f"generated output is stale: {output}", file=sys.stderr)
    sys.stderr.writelines(
        difflib.unified_diff(
            existing.splitlines(keepends=True),
            generated.splitlines(keepends=True),
            fromfile=str(output),
            tofile="generated",
        )
    )
    return False


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args(argv)

    try:
        generated = generate(load_catalog(arguments.input))
    except (OSError, ValueError) as error:
        print(f"CSS property metadata generation failed: {error}", file=sys.stderr)
        return 1

    if arguments.check:
        return 0 if _check(arguments.output, generated) else 1

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(generated, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
