#!/usr/bin/env python3
"""Scan and validate the fail-closed Code OSS Web API capability ledger."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "tests/WebPlatformSubset/code-oss-web-api-catalog.json"
LEDGER_PATH = ROOT / "tests/WebPlatformSubset/code-oss-web-api-ledger.json"
SNAPSHOT_PATH = ROOT / "tests/WebPlatformSubset/code-oss-web-api-reachability.json"
REPORT_PATH = ROOT / "docs/validation/code-oss-web-api-ledger.md"
SCHEMA_PATH = ROOT / "tests/WebPlatformSubset/code-oss-web-api-ledger.schema.json"
STATES = {"supported", "partial", "blocked", "intentionally-absent", "consumer-unreachable"}
PLATFORMS = {"linux-x64", "macos-arm64", "windows-x64"}
SOURCE_SUFFIXES = {".ts", ".tsx", ".js", ".mjs", ".html"}
EXCLUDED_PARTS = {"node_modules", "out", ".git", "test", "tests", "fixtures"}
MAX_INPUT_BYTES = 131_072
MAX_REPORT_BYTES = 131_072
EVIDENCE_KINDS = {"wpt", "browser", "native", "product", "performance", "security"}
EVIDENCE_COUNT_KEYS = {"pass", "fail", "skipped", "unavailable", "total"}
API_HINTS = {
    "abort": ("AbortController", "AbortSignal"),
    "animation-frame": ("requestAnimationFrame", "cancelAnimationFrame"),
    "broadcast-channel": ("BroadcastChannel",),
    "cache-storage": ("caches.", "CacheStorage"),
    "canvas-2d": ("getContext", "CanvasRenderingContext2D"),
    "clipboard": ("clipboard", "ClipboardEvent"),
    "crypto": ("crypto.", "SubtleCrypto"),
    "dom-core": ("document.", "MutationObserver"),
    "drag-drop": ("DataTransfer", "DragEvent"),
    "fetch": ("fetch", "Request", "Response", "Headers"),
    "file-api": ("FileReader", "FileList", "Blob"),
    "file-system-access": ("FileSystem", "showOpenFilePicker", "showDirectoryPicker"),
    "focus-selection": ("activeElement", "hasFocus", "getSelection", "Selection"),
    "form-data": ("FormData",),
    "iframe": ("IFrame", "contentWindow", "contentDocument", "sandbox"),
    "indexeddb": ("indexedDB", "IDB"),
    "message-port": ("MessageChannel", "MessagePort"),
    "observers": ("Observer",),
    "performance-timing": ("performance.", "PerformanceEntry"),
    "service-worker": ("serviceWorker", "ServiceWorker"),
    "storage": ("localStorage", "sessionStorage", "navigator.storage"),
    "streams": ("ReadableStream", "WritableStream", "TransformStream"),
    "url": ("URLSearchParams", "ObjectURL"),
    "websocket": ("WebSocket",),
    "worker": ("Worker",),
}


class LedgerError(RuntimeError):
    pass


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise LedgerError(f"cannot read {path}: {exc}") from exc


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_keys(value: dict, expected: set[str], label: str) -> None:
    actual = set(value)
    if actual != expected:
        raise LedgerError(f"{label} keys differ: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")


def git_revision(path: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def validate_catalog(catalog: dict) -> list[dict]:
    require_keys(catalog, {"schemaVersion", "catalogRevision", "description", "apis"}, "catalog")
    if catalog.get("schemaVersion") != 1:
        raise LedgerError("catalog schemaVersion must be 1")
    apis = catalog.get("apis")
    if not isinstance(apis, list) or not apis:
        raise LedgerError("catalog must contain APIs")
    ids = [entry.get("id") for entry in apis]
    if ids != sorted(ids) or len(ids) != len(set(ids)):
        raise LedgerError("catalog API ids must be unique and sorted")
    for entry in apis:
        require_keys(entry, {"id", "category", "patterns"}, f"catalog {entry.get('id')}")
        if not re.fullmatch(r"[a-z0-9-]+", entry.get("id", "")):
            raise LedgerError(f"invalid API id: {entry.get('id')!r}")
        patterns = entry.get("patterns")
        if not isinstance(patterns, list) or not patterns:
            raise LedgerError(f"{entry['id']}: patterns must be non-empty")
        for pattern in patterns:
            try:
                re.compile(pattern)
            except re.error as exc:
                raise LedgerError(f"{entry['id']}: invalid pattern {pattern!r}: {exc}") from exc
    return apis


def source_files(source_root: Path):
    for top in (source_root / "src", source_root / "extensions"):
        if not top.is_dir():
            continue
        for directory, names, filenames in os.walk(top):
            names[:] = sorted(name for name in names if name not in EXCLUDED_PARTS)
            for filename in sorted(filenames):
                path = Path(directory) / filename
                if path.suffix in SOURCE_SUFFIXES:
                    yield path


def scan(catalog: dict, source_root: Path, expected_revision: str | None = None) -> dict:
    apis = validate_catalog(catalog)
    actual_revision = git_revision(source_root)
    if expected_revision and actual_revision != expected_revision:
        raise LedgerError(
            f"Code OSS revision mismatch: expected {expected_revision}, found {actual_revision}"
        )
    compiled = {
        entry["id"]: [re.compile(pattern) for pattern in entry["patterns"]]
        for entry in apis
    }
    matches = {entry["id"]: {"hitCount": 0, "files": []} for entry in apis}
    rg = shutil.which("rg")
    if rg:
        globs = []
        for suffix in sorted(SOURCE_SUFFIXES):
            globs.extend(["--glob", f"*{suffix}"])
        for part in sorted(EXCLUDED_PARTS):
            globs.extend(["--glob", f"!**/{part}/**"])
        roots = [str(path) for path in (source_root / "src", source_root / "extensions") if path.is_dir()]
        files_result = subprocess.run(
            [rg, "--no-ignore", "--files", *globs, *roots], check=True, capture_output=True, text=True
        )
        scanned = len(files_result.stdout.splitlines())
        pattern_args = [item for entry in apis for pattern in entry["patterns"] for item in ("-e", pattern)]
        result = subprocess.run(
            [rg, "--no-ignore", "--json", "--only-matching", "--no-messages", *globs, *pattern_args, *roots],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode not in (0, 1):
            raise LedgerError(f"ripgrep reachability scan failed: {result.stderr.strip()}")
        files_by_api = {entry["id"]: set() for entry in apis}
        for line in result.stdout.splitlines():
            event = json.loads(line)
            if event.get("type") != "match":
                continue
            path = Path(event["data"]["path"]["text"])
            relative = path.relative_to(source_root).as_posix()
            for submatch in event["data"]["submatches"]:
                token = submatch["match"]["text"]
                owners = [api_id for api_id, patterns in compiled.items() if any(pattern.fullmatch(token) for pattern in patterns)]
                if len(owners) != 1:
                    raise LedgerError(f"ambiguous scanner token {token!r}: {owners}")
                api_id = owners[0]
                matches[api_id]["hitCount"] += 1
                files_by_api[api_id].add(relative)
        for api_id, files in files_by_api.items():
            matches[api_id]["files"] = sorted(files)
    else:
        scanned = 0
        for path in sorted(source_files(source_root)):
            scanned += 1
            text = path.read_text(encoding="utf-8", errors="replace")
            relative = path.relative_to(source_root).as_posix()
            for api_id, patterns in compiled.items():
                if not any(hint in text for hint in API_HINTS[api_id]):
                    continue
                hit_count = sum(len(pattern.findall(text)) for pattern in patterns)
                if hit_count:
                    matches[api_id]["hitCount"] += hit_count
                    matches[api_id]["files"].append(relative)
    entries = []
    for entry in apis:
        result = matches[entry["id"]]
        files = result.pop("files")
        entries.append(
            {
                "id": entry["id"],
                "reachable": bool(files),
                "hitCount": result["hitCount"],
                "fileCount": len(files),
                "examples": files[:5],
            }
        )
    return {
        "schemaVersion": 1,
        "catalogRevision": catalog["catalogRevision"],
        "codeOssRevision": actual_revision,
        "scope": ["src", "extensions"],
        "excludedDirectoryNames": sorted(EXCLUDED_PARTS),
        "scannedFileCount": scanned,
        "apis": entries,
    }


def validate_snapshot(catalog: dict, snapshot: dict) -> dict[str, dict]:
    require_keys(
        snapshot,
        {"schemaVersion", "catalogRevision", "codeOssRevision", "scope", "excludedDirectoryNames", "scannedFileCount", "apis"},
        "reachability snapshot",
    )
    catalog_ids = [entry["id"] for entry in validate_catalog(catalog)]
    if snapshot.get("schemaVersion") != 1:
        raise LedgerError("reachability schemaVersion must be 1")
    if snapshot.get("catalogRevision") != catalog.get("catalogRevision"):
        raise LedgerError("reachability catalog revision is stale")
    if not re.fullmatch(r"[0-9a-f]{40}", snapshot.get("codeOssRevision", "")):
        raise LedgerError("reachability Code OSS revision is invalid")
    if snapshot.get("scope") != ["src", "extensions"]:
        raise LedgerError("reachability scan scope changed")
    if snapshot.get("excludedDirectoryNames") != sorted(EXCLUDED_PARTS):
        raise LedgerError("reachability exclusions changed")
    if not isinstance(snapshot.get("scannedFileCount"), int) or snapshot["scannedFileCount"] <= 0:
        raise LedgerError("reachability scan has no source files")
    entries = snapshot.get("apis")
    if not isinstance(entries, list):
        raise LedgerError("reachability APIs must be an array")
    ids = [entry.get("id") for entry in entries]
    if ids != catalog_ids:
        raise LedgerError("reachability APIs must exactly match the sorted catalog")
    for entry in entries:
        require_keys(entry, {"id", "reachable", "hitCount", "fileCount", "examples"}, f"reachability {entry.get('id')}")
        if not isinstance(entry.get("reachable"), bool):
            raise LedgerError(f"{entry['id']}: reachable must be boolean")
        for key in ("hitCount", "fileCount"):
            if not isinstance(entry.get(key), int) or entry[key] < 0:
                raise LedgerError(f"{entry['id']}: invalid {key}")
        if entry["reachable"] != (entry["fileCount"] > 0 and entry["hitCount"] > 0):
            raise LedgerError(f"{entry['id']}: reachability/count mismatch")
        examples = entry.get("examples")
        if not isinstance(examples, list) or len(examples) > 5:
            raise LedgerError(f"{entry['id']}: invalid examples")
    return {entry["id"]: entry for entry in entries}


def css_denominators(data: dict) -> dict:
    capabilities = Counter(capability.get("status") for capability in data.get("capabilities", []))
    capability_counts = {name: count for name, count in sorted(capabilities.items())}
    capability_counts["total"] = sum(capabilities.values())
    lanes = {}
    for lane in ("native", "browser"):
        counts = Counter()
        for capability in data.get("capabilities", []):
            for evidence in capability.get("evidence", []):
                if evidence.get("lane") == lane:
                    counts.update(evidence.get("counts", {}))
        lanes[lane] = {name: counts.get(name, 0) for name in ("pass", "fail", "skipped", "unavailable", "total")}
    return {"capabilities": capability_counts, **lanes}


def validate_css_slice(ledger: dict) -> dict:
    css = ledger.get("cssSlice")
    if not isinstance(css, dict):
        raise LedgerError("cssSlice is required")
    require_keys(
        css,
        {"path", "sha256", "schema", "matrixVersion", "profileSha256", "denominators"},
        "CSS slice",
    )
    path_value = css.get("path")
    digest = css.get("sha256")
    if not isinstance(path_value, str) or not isinstance(digest, str):
        raise LedgerError("cssSlice path and sha256 are required")
    path = ROOT / path_value
    if not path.is_file():
        raise LedgerError(f"CSS capability slice is missing: {path_value}")
    if sha256(path) != digest:
        raise LedgerError("CSS capability slice digest is stale")
    data = load_json(path)
    if data.get("schema") != css.get("schema"):
        raise LedgerError("CSS capability slice schema changed")
    if data.get("matrixVersion") != css.get("matrixVersion"):
        raise LedgerError("CSS capability slice matrix version changed")
    profile_digest = data.get("provenance", {}).get("profileSha256")
    if profile_digest != css.get("profileSha256"):
        raise LedgerError("CSS capability evidence profile digest changed")
    actual_denominators = css_denominators(data)
    if actual_denominators != css.get("denominators"):
        raise LedgerError(f"CSS capability denominators changed: expected {css.get('denominators')}, found {actual_denominators}")
    return data


def validate_ledger(catalog: dict, snapshot: dict, ledger: dict, verify_css: bool = True) -> None:
    require_keys(ledger, {"schemaVersion", "revisions", "cssSlice", "claims"}, "ledger")
    snapshot_by_id = validate_snapshot(catalog, snapshot)
    if ledger.get("schemaVersion") != 2:
        raise LedgerError("ledger schemaVersion must be 2")
    revisions = ledger.get("revisions", {})
    require_keys(revisions, {"codeOss", "webScene", "appScene", "wpt"}, "ledger revisions")
    for name, revision in revisions.items():
        if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
            raise LedgerError(f"invalid {name} revision")
    if revisions.get("codeOss") != snapshot.get("codeOssRevision"):
        raise LedgerError("ledger Code OSS revision is stale")
    upstream = load_json(ROOT / "tests/WebPlatformSubset/upstream-files.json")
    if revisions.get("wpt") != upstream.get("revision"):
        raise LedgerError("ledger WPT revision is stale")
    claims = ledger.get("claims")
    if not isinstance(claims, list):
        raise LedgerError("ledger claims must be an array")
    catalog_ids = [entry["id"] for entry in validate_catalog(catalog)]
    ids = [claim.get("id") for claim in claims]
    if ids != catalog_ids:
        raise LedgerError("ledger claims must exactly match the sorted catalog")
    for claim in claims:
        require_keys(claim, {"id", "reachable", "state", "platforms", "issues", "evidence", "limits", "reason"}, f"claim {claim.get('id')}")
        api_id = claim["id"]
        state = claim.get("state")
        if state not in STATES:
            raise LedgerError(f"{api_id}: invalid state {state!r}")
        if claim.get("reachable") != snapshot_by_id[api_id]["reachable"]:
            raise LedgerError(f"{api_id}: reachability is stale")
        platforms = claim.get("platforms")
        if not isinstance(platforms, list) or not platforms or len(platforms) != len(set(platforms)) or not set(platforms) <= PLATFORMS:
            raise LedgerError(f"{api_id}: invalid platform scope")
        issues = claim.get("issues")
        if not isinstance(issues, list) or not issues or not all(re.fullmatch(r"(?:WebScene|AppScene)#\d+", value) for value in issues):
            raise LedgerError(f"{api_id}: at least one valid owner issue is required")
        if len(issues) != len(set(issues)):
            raise LedgerError(f"{api_id}: duplicate owner issue")
        evidence = claim.get("evidence")
        if not isinstance(evidence, list):
            raise LedgerError(f"{api_id}: evidence must be an array")
        seen_evidence = set()
        for item in evidence:
            if not isinstance(item, dict):
                raise LedgerError(f"{api_id}: evidence entries must be objects")
            require_keys(
                item,
                {"kind", "path", "platforms", "revision", "sha256", "counts", "run"},
                f"{api_id} evidence",
            )
            if item["kind"] not in EVIDENCE_KINDS:
                raise LedgerError(f"{api_id}: invalid evidence kind")
            identity = (item["kind"], item["path"])
            if identity in seen_evidence:
                raise LedgerError(f"{api_id}: duplicate evidence {identity}")
            seen_evidence.add(identity)
            evidence_platforms = item.get("platforms")
            if not isinstance(evidence_platforms, list) or not evidence_platforms \
                    or len(evidence_platforms) != len(set(evidence_platforms)) \
                    or not set(evidence_platforms) <= set(platforms):
                raise LedgerError(f"{api_id}: evidence platform scope is invalid")
            if not re.fullmatch(r"[0-9a-f]{40}", item.get("revision", "")):
                raise LedgerError(f"{api_id}: evidence revision is invalid")
            digest = item.get("sha256")
            if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
                raise LedgerError(f"{api_id}: evidence digest is invalid")
            path = ROOT / item["path"]
            if not path.is_file():
                raise LedgerError(f"{api_id}: evidence path does not exist: {item['path']}")
            if sha256(path) != digest:
                raise LedgerError(f"{api_id}: evidence digest is stale: {item['path']}")
            counts = item.get("counts")
            if not isinstance(counts, dict) or set(counts) != EVIDENCE_COUNT_KEYS \
                    or any(not isinstance(value, int) or isinstance(value, bool) or value < 0
                           for value in counts.values()) \
                    or counts["total"] <= 0 \
                    or counts["total"] != sum(counts[name] for name in EVIDENCE_COUNT_KEYS - {"total"}):
                raise LedgerError(f"{api_id}: evidence counts are invalid")
            if not isinstance(item.get("run"), str) \
                    or not item["run"].startswith("https://github.com/SceneTech/"):
                raise LedgerError(f"{api_id}: evidence run must be a SceneTech GitHub URL")
        if state in {"supported", "partial"}:
            kinds = {item.get("kind") for item in evidence if isinstance(item, dict)}
            if not {"native", "product"}.issubset(kinds) or not ({"wpt", "browser"} & kinds):
                raise LedgerError(f"{api_id}: {state} claims require native, product, and WPT/browser evidence")
            for required_kind in ("native", "product"):
                covered = {
                    platform
                    for item in evidence if item.get("kind") == required_kind
                    for platform in item.get("platforms", [])
                    if item.get("counts", {}).get("fail", 1) == 0
                    and item.get("counts", {}).get("unavailable", 1) == 0
                    and item.get("counts", {}).get("pass", 0) > 0
                }
                if covered != set(platforms):
                    raise LedgerError(
                        f"{api_id}: {state} {required_kind} evidence does not cover every claimed platform"
                    )
        if state in {"blocked", "intentionally-absent", "consumer-unreachable"} and not claim.get("reason"):
            raise LedgerError(f"{api_id}: {state} claim requires a reason")
        limits = claim.get("limits")
        if not isinstance(limits, list) or not all(isinstance(limit, str) and limit for limit in limits):
            raise LedgerError(f"{api_id}: limits must be an array")
        if not isinstance(claim.get("reason"), str):
            raise LedgerError(f"{api_id}: reason must be a string")
    if verify_css:
        validate_css_slice(ledger)


def render_report(catalog: dict, snapshot: dict, ledger: dict) -> str:
    claims = ledger["claims"]
    states = Counter(claim["state"] for claim in claims)
    reachable = sum(1 for claim in claims if claim["reachable"])
    lines = [
        "# Code OSS Web API capability ledger",
        "",
        "Generated by `scripts/code_oss_web_api_ledger.py`; do not hand-edit this report.",
        "",
        f"- Code OSS revision: `{ledger['revisions']['codeOss']}`",
        f"- WebScene baseline: `{ledger['revisions']['webScene']}`",
        f"- AppScene baseline: `{ledger['revisions']['appScene']}`",
        f"- WPT revision: `{ledger['revisions']['wpt']}`",
        f"- Static scan: {reachable}/{len(claims)} catalog entries reachable across {snapshot['scannedFileCount']} source files",
        "- State denominator: " + ", ".join(f"{name} {states.get(name, 0)}" for name in sorted(STATES)) + f", total {len(claims)}",
        "",
        "Static matches establish source reachability only. Runtime claims require the evidence linked below.",
        "",
        "| API slice | Reachable | State | Platforms | Owner | Evidence | Limits/reason |",
        "|---|---:|---|---|---|---:|---|",
    ]
    for claim in claims:
        detail = " ".join(claim.get("limits", [])) or claim.get("reason", "")
        evidence = claim["evidence"]
        evidence_counts = Counter()
        for item in evidence:
            evidence_counts.update(item["counts"])
        evidence_summary = str(len(evidence))
        if evidence:
            evidence_summary += (
                f" ({evidence_counts['pass']} pass, {evidence_counts['fail']} fail, "
                f"{evidence_counts['skipped']} skipped, "
                f"{evidence_counts['unavailable']} unavailable, "
                f"{evidence_counts['total']} total)"
            )
        lines.append(
            f"| `{claim['id']}` | {'yes' if claim['reachable'] else 'no'} | {claim['state']} | "
            f"{', '.join(claim['platforms'])} | {', '.join(claim['issues'])} | {evidence_summary} | {detail} |"
        )
    css = ledger["cssSlice"]
    d = css["denominators"]
    lines.extend([
        "", "## CSS slice", "",
        f"CSS claims are composed from `{css['path']}` at SHA-256 `{css['sha256']}`.",
        f"Capability denominator: supported {d['capabilities'].get('supported', 0)}, partial {d['capabilities'].get('partial', 0)}, total {d['capabilities']['total']}.",
        f"Native assertions: {d['native']['pass']} pass, {d['native']['fail']} fail, {d['native']['skipped']} skipped, {d['native']['unavailable']} unavailable, {d['native']['total']} total.",
        f"Browser assertions: {d['browser']['pass']} pass, {d['browser']['fail']} fail, {d['browser']['skipped']} skipped, {d['browser']['unavailable']} unavailable, {d['browser']['total']} total.",
        "",
    ])
    return "\n".join(lines)


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=False) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, help="scan an unchanged Code OSS checkout")
    parser.add_argument("--write-snapshot", action="store_true")
    parser.add_argument("--write-report", action="store_true")
    parser.add_argument("--skip-css-slice", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        for path in (CATALOG_PATH, LEDGER_PATH, SNAPSHOT_PATH, SCHEMA_PATH):
            if path.exists() and path.stat().st_size > MAX_INPUT_BYTES:
                raise LedgerError(f"input exceeds {MAX_INPUT_BYTES} bytes: {path}")
        schema = load_json(SCHEMA_PATH)
        if schema.get("$id") != "https://webscene.dev/schemas/code-oss-web-api-ledger-v2.json":
            raise LedgerError("ledger schema identity changed")
        catalog = load_json(CATALOG_PATH)
        validate_catalog(catalog)
        snapshot = load_json(SNAPSHOT_PATH) if SNAPSHOT_PATH.exists() else None
        ledger = load_json(LEDGER_PATH) if LEDGER_PATH.exists() else None
        if args.source_root:
            expected = ledger.get("revisions", {}).get("codeOss") if ledger else None
            fresh = scan(catalog, args.source_root.resolve(), expected)
            if args.write_snapshot:
                write_json(SNAPSHOT_PATH, fresh)
                snapshot = fresh
            elif snapshot != fresh:
                raise LedgerError("committed reachability snapshot is stale; run with --write-snapshot")
        if snapshot is None:
            raise LedgerError(f"missing {SNAPSHOT_PATH}")
        if ledger is None:
            raise LedgerError(f"missing {LEDGER_PATH}")
        validate_ledger(catalog, snapshot, ledger, verify_css=not args.skip_css_slice)
        report = render_report(catalog, snapshot, ledger)
        if len(report.encode("utf-8")) > MAX_REPORT_BYTES:
            raise LedgerError(f"generated report exceeds {MAX_REPORT_BYTES} bytes")
        if args.write_report:
            REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
            REPORT_PATH.write_text(report, encoding="utf-8")
        elif not REPORT_PATH.exists() or REPORT_PATH.read_text(encoding="utf-8") != report:
            raise LedgerError("generated ledger report is stale; run with --write-report")
        print(
            f"Code OSS Web API ledger valid: {len(ledger['claims'])} claims, "
            f"{sum(entry['reachable'] for entry in snapshot['apis'])} reachable"
        )
        return 0
    except (LedgerError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
