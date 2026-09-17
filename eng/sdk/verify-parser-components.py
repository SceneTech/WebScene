#!/usr/bin/env python3
"""Fail closed when the CSS parser archive retains runtime HTML code."""
import argparse
import json
from pathlib import Path
import subprocess

HTML_SYMBOLS = (
    "webscene_html_parser_abi_version",
    "webscene_html_parse_document",
    "webscene_html_parse_fragment",
)
CSS_SYMBOLS = (
    "webscene_css_stream_abi_version",
    "webscene_css_stream_stylesheet",
    "webscene_css_stream_declarations",
    "webscene_selector_parser_abi_version",
    "webscene_selector_parse",
)


def symbols(nm, archive):
    return subprocess.check_output(
        [str(nm), "-g", "-C", str(archive)], text=True, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nm", required=True, type=Path)
    parser.add_argument("--full", required=True, type=Path)
    parser.add_argument("--css", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    for path in (args.nm, args.full, args.css):
        if not path.is_file():
            parser.error(f"missing input: {path}")
    full = symbols(args.nm, args.full)
    css = symbols(args.nm, args.css)
    missing_full = [name for name in HTML_SYMBOLS + CSS_SYMBOLS if name not in full]
    missing_css = [name for name in CSS_SYMBOLS if name not in css]
    retained_html = [name for name in HTML_SYMBOLS if name in css]
    if "html5ever::" in css:
        retained_html.append("html5ever::")
    if missing_full or missing_css or retained_html:
        raise RuntimeError(json.dumps({
            "missingFull": missing_full,
            "missingCss": missing_css,
            "retainedHtml": retained_html,
        }, sort_keys=True))
    full_size = args.full.stat().st_size
    css_size = args.css.stat().st_size
    ratio = css_size / full_size
    if ratio > 0.85:
        raise RuntimeError(
            f"CSS/selector archive size ratio {ratio:.6f} exceeds 0.85 "
            f"({css_size}/{full_size})")
    report = {
        "schemaVersion": 1,
        "fullArchive": {"bytes": full_size, "html": True, "css": True, "selectors": True},
        "cssSelectorArchive": {"bytes": css_size, "html": False, "css": True, "selectors": True},
        "cssToFullSizeRatio": ratio,
        "maximumCssToFullSizeRatio": 0.85,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
