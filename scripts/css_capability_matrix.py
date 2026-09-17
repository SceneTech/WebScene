#!/usr/bin/env python3
"""Validate and render the versioned WebScene CSS capability matrix."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


SCHEMA_NAME = "webscene-css-capability-matrix-v1"
AREAS = (
    "parsing", "selectors", "cascade", "cssom", "values", "layout",
    "paint", "responsive", "animation",
)
STATUSES = ("supported", "partial", "unsupported", "out-of-scope")
LANES = ("browser", "native")
COUNT_KEYS = ("pass", "fail", "skipped", "unavailable", "total")
HARD_INPUT_LIMIT = 2 * 1024 * 1024


class MatrixError(RuntimeError):
    """A fail-closed matrix validation error."""


def repository_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise MatrixError(f"duplicate JSON key: {key}")
        value[key] = item
    return value


def load_json(path: Path, *, max_bytes: int = HARD_INPUT_LIMIT) -> dict[str, Any]:
    try:
        size = path.stat().st_size
    except FileNotFoundError as error:
        raise MatrixError(f"missing JSON input: {path}") from error
    if size > max_bytes:
        raise MatrixError(f"{path}: {size} bytes exceeds input bound {max_bytes}")
    try:
        value = json.loads(path.read_text(), object_pairs_hook=_reject_duplicate_keys)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise MatrixError(f"invalid JSON in {path}: {error}") from error
    if not isinstance(value, dict):
        raise MatrixError(f"{path}: root must be an object")
    return value


def require_exact_keys(value: dict[str, Any], required: set[str], where: str) -> None:
    missing = required - value.keys()
    extra = value.keys() - required
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing {sorted(missing)}")
        if extra:
            details.append(f"unexpected {sorted(extra)}")
        raise MatrixError(f"{where}: {', '.join(details)}")


def require_string(value: Any, where: str, pattern: str | None = None) -> str:
    if not isinstance(value, str) or not value:
        raise MatrixError(f"{where}: expected a non-empty string")
    if pattern and re.fullmatch(pattern, value) is None:
        raise MatrixError(f"{where}: value does not match {pattern}")
    return value


def repository_path(root: Path, raw: Any, where: str) -> Path:
    relative = Path(require_string(raw, where))
    if relative.is_absolute() or ".." in relative.parts:
        raise MatrixError(f"{where}: path must remain inside the repository")
    resolved = (root / relative).resolve()
    if root.resolve() not in resolved.parents:
        raise MatrixError(f"{where}: path escapes the repository")
    return resolved


def normalized_sha256(path: Path) -> str:
    text = path.read_text().replace("\r\n", "\n").replace("\r", "\n")
    return hashlib.sha256(text.encode()).hexdigest()


def validate_schema_contract(schema: dict[str, Any]) -> None:
    if schema.get("$id") != "https://scenetech.dev/schemas/webscene-css-capability-matrix-v1.schema.json":
        raise MatrixError("schema: unexpected or missing stable $id")
    if schema.get("properties", {}).get("schema", {}).get("const") != SCHEMA_NAME:
        raise MatrixError("schema: root schema discriminator drifted from the validator")
    capability = schema.get("$defs", {}).get("capability", {}).get("properties", {})
    if tuple(capability.get("area", {}).get("enum", [])) != AREAS:
        raise MatrixError("schema: area enum drifted from the validator")
    if tuple(capability.get("status", {}).get("enum", [])) != STATUSES:
        raise MatrixError("schema: status enum drifted from the validator")
    evidence = schema.get("$defs", {}).get("evidence", {}).get("properties", {})
    if tuple(evidence.get("lane", {}).get("enum", [])) != LANES:
        raise MatrixError("schema: evidence lane enum drifted from the validator")


def parse_wpt_revision(path: Path) -> str:
    match = re.search(r"^Revision:\s*([0-9a-f]{40})\s*$", path.read_text(), re.MULTILINE)
    if not match:
        raise MatrixError(f"{path}: missing exact 40-character WPT revision")
    return match.group(1)


def require_git_revision(root: Path, revision: str, where: str) -> None:
    require_string(revision, where, r"[0-9a-f]{40}")
    completed = subprocess.run(
        ["git", "cat-file", "-e", f"{revision}^{{commit}}"],
        cwd=root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if completed.returncode == 0:
        return
    shallow = subprocess.run(
        ["git", "rev-parse", "--is-shallow-repository"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    if shallow.returncode == 0 and shallow.stdout.strip() == "true":
        return
    raise MatrixError(f"{where}: revision {revision} is unavailable in this complete checkout")


def result_counts(result: dict[str, Any], where: str) -> dict[str, int]:
    subtests = result.get("subtests")
    if not isinstance(subtests, list) or not subtests:
        raise MatrixError(f"{where}: result must expose at least one subtest denominator")
    counts = {key: 0 for key in COUNT_KEYS}
    mapping = {"PASS": "pass", "FAIL": "fail", "SKIP": "skipped", "NOTRUN": "skipped"}
    for index, subtest in enumerate(subtests):
        if not isinstance(subtest, dict):
            raise MatrixError(f"{where}.subtests[{index}]: expected an object")
        status = subtest.get("status")
        if status not in mapping:
            raise MatrixError(f"{where}.subtests[{index}]: unsupported status {status!r}")
        counts[mapping[status]] += 1
        counts["total"] += 1
    return counts


def validate_result_summary(result_file: dict[str, Any], where: str) -> None:
    results = result_file.get("results")
    summary = result_file.get("summary")
    if not isinstance(results, list) or not isinstance(summary, dict):
        raise MatrixError(f"{where}: missing results or summary")
    paths: set[str] = set()
    aggregate = {key: 0 for key in COUNT_KEYS}
    document_passes = 0
    for index, result in enumerate(results):
        if not isinstance(result, dict):
            raise MatrixError(f"{where}.results[{index}]: expected an object")
        path = require_string(result.get("path"), f"{where}.results[{index}].path")
        if path in paths:
            raise MatrixError(f"{where}: duplicate result path {path}")
        paths.add(path)
        counts = result_counts(result, f"{where}:{path}")
        for key in COUNT_KEYS:
            aggregate[key] += counts[key]
        if result.get("status") == "PASS":
            document_passes += 1
    expected = {
        "tests": len(results),
        "passed": document_passes,
        "failed": len(results) - document_passes,
        "subtests": aggregate["total"],
        "subtestsPassed": aggregate["pass"],
        "subtestsFailed": aggregate["fail"],
    }
    for key, value in expected.items():
        if summary.get(key) != value:
            raise MatrixError(f"{where}.summary.{key}: expected {value}, got {summary.get(key)!r}")


def _profile_entries(profile: dict[str, Any]) -> dict[str, dict[str, Any]]:
    entries: dict[str, dict[str, Any]] = {}
    for section in ("required", "candidate", "harnessBlocked"):
        values = profile.get(section)
        if not isinstance(values, list):
            raise MatrixError(f"profile.{section}: expected an array")
        for entry in values:
            if not isinstance(entry, dict):
                raise MatrixError(f"profile.{section}: entry must be an object")
            path = require_string(entry.get("path"), f"profile.{section}.path")
            if path in entries:
                raise MatrixError(f"profile: duplicate contract path {path}")
            entries[path] = entry
    return entries


def validate_matrix(root: Path, matrix: dict[str, Any], schema: dict[str, Any]) -> dict[str, Any]:
    validate_schema_contract(schema)
    require_exact_keys(
        matrix,
        {"schema", "matrixVersion", "provenance", "limits", "capabilities"},
        "matrix",
    )
    if matrix["schema"] != SCHEMA_NAME:
        raise MatrixError(f"matrix.schema: expected {SCHEMA_NAME}")
    require_string(matrix["matrixVersion"], "matrix.matrixVersion", r"[0-9]+\.[0-9]+\.[0-9]+")

    provenance = matrix["provenance"]
    if not isinstance(provenance, dict):
        raise MatrixError("matrix.provenance: expected an object")
    require_exact_keys(
        provenance,
        {"webSceneBaseline", "wptRepository", "wptRevision", "wptRevisionFile", "wptFiles", "wptFilesSha256", "profile", "profileSha256"},
        "matrix.provenance",
    )
    baseline = require_string(provenance["webSceneBaseline"], "matrix.provenance.webSceneBaseline", r"[0-9a-f]{40}")
    require_git_revision(root, baseline, "matrix.provenance.webSceneBaseline")
    wpt_revision = require_string(provenance["wptRevision"], "matrix.provenance.wptRevision", r"[0-9a-f]{40}")
    require_string(provenance["wptRepository"], "matrix.provenance.wptRepository")
    revision_file = repository_path(root, provenance["wptRevisionFile"], "matrix.provenance.wptRevisionFile")
    if parse_wpt_revision(revision_file) != wpt_revision:
        raise MatrixError("matrix.provenance.wptRevision does not match upstream-revision.txt")
    wpt_files_path = repository_path(root, provenance["wptFiles"], "matrix.provenance.wptFiles")
    wpt_files_hash = require_string(provenance["wptFilesSha256"], "matrix.provenance.wptFilesSha256", r"[0-9a-f]{64}")
    if normalized_sha256(wpt_files_path) != wpt_files_hash:
        raise MatrixError("matrix.provenance.wptFilesSha256 does not match the normalized WPT file manifest")
    wpt_files = load_json(wpt_files_path)
    if wpt_files.get("revision") != wpt_revision or not isinstance(wpt_files.get("files"), dict):
        raise MatrixError("WPT file manifest does not match the pinned revision")
    profile_path = repository_path(root, provenance["profile"], "matrix.provenance.profile")
    profile_hash = require_string(provenance["profileSha256"], "matrix.provenance.profileSha256", r"[0-9a-f]{64}")
    if normalized_sha256(profile_path) != profile_hash:
        raise MatrixError("matrix.provenance.profileSha256 does not match the normalized profile")
    profile = load_json(profile_path)
    if profile.get("wptRevision") != wpt_revision:
        raise MatrixError("profile WPT revision does not match matrix provenance")
    profile_entries = _profile_entries(profile)

    limits = matrix["limits"]
    if not isinstance(limits, dict):
        raise MatrixError("matrix.limits: expected an object")
    require_exact_keys(limits, {"maxCapabilities", "maxEvidenceFiles", "maxInputBytes", "maxReportBytes"}, "matrix.limits")
    for key in ("maxCapabilities", "maxEvidenceFiles", "maxInputBytes", "maxReportBytes"):
        if not isinstance(limits[key], int) or isinstance(limits[key], bool) or limits[key] <= 0:
            raise MatrixError(f"matrix.limits.{key}: expected a positive integer")
    if limits["maxCapabilities"] > 4096 or limits["maxEvidenceFiles"] > 256:
        raise MatrixError("matrix.limits: capability or evidence-file bound exceeds the schema maximum")
    if limits["maxInputBytes"] < 1024 or limits["maxReportBytes"] < 1024:
        raise MatrixError("matrix.limits: byte bounds must be at least 1024")
    if limits["maxInputBytes"] > HARD_INPUT_LIMIT:
        raise MatrixError(f"matrix.limits.maxInputBytes exceeds hard bound {HARD_INPUT_LIMIT}")
    if limits["maxReportBytes"] > 1024 * 1024:
        raise MatrixError("matrix.limits.maxReportBytes exceeds the schema maximum")

    capabilities = matrix["capabilities"]
    if not isinstance(capabilities, list) or not capabilities:
        raise MatrixError("matrix.capabilities: expected a non-empty array")
    if len(capabilities) > limits["maxCapabilities"]:
        raise MatrixError("matrix.capabilities exceeds maxCapabilities")

    ids: set[str] = set()
    areas: set[str] = set()
    claimed_contracts: set[str] = set()
    evidence_cache: dict[Path, dict[str, Any]] = {}
    aggregate = {lane: {key: 0 for key in COUNT_KEYS} for lane in LANES}
    status_counts = {status: 0 for status in STATUSES}

    for index, capability in enumerate(capabilities):
        where = f"matrix.capabilities[{index}]"
        if not isinstance(capability, dict):
            raise MatrixError(f"{where}: expected an object")
        require_exact_keys(capability, {"id", "area", "status", "boundary", "issues", "contracts", "evidence"}, where)
        identifier = require_string(capability["id"], f"{where}.id", r"[a-z0-9][a-z0-9-]*")
        if identifier in ids:
            raise MatrixError(f"{where}.id: duplicate capability {identifier}")
        ids.add(identifier)
        area = capability["area"]
        if area not in AREAS:
            raise MatrixError(f"{where}.area: unknown area {area!r}")
        areas.add(area)
        status = capability["status"]
        if status not in STATUSES:
            raise MatrixError(f"{where}.status: unknown status {status!r}")
        status_counts[status] += 1
        if len(require_string(capability["boundary"], f"{where}.boundary")) < 20:
            raise MatrixError(f"{where}.boundary: boundary must be specific")
        issues = capability["issues"]
        if not isinstance(issues, list) or not issues or any(type(issue) is not int or issue <= 0 for issue in issues):
            raise MatrixError(f"{where}.issues: expected positive issue numbers")
        if len(set(issues)) != len(issues):
            raise MatrixError(f"{where}.issues: duplicates are not allowed")

        contracts = capability["contracts"]
        if not isinstance(contracts, list) or not contracts:
            raise MatrixError(f"{where}.contracts: expected a non-empty array")
        for contract_index, contract in enumerate(contracts):
            contract_where = f"{where}.contracts[{contract_index}]"
            if not isinstance(contract, dict):
                raise MatrixError(f"{contract_where}: expected an object")
            require_exact_keys(contract, {"path", "capabilityTokens"}, contract_where)
            contract_path = require_string(contract["path"], f"{contract_where}.path")
            if contract_path in claimed_contracts:
                raise MatrixError(f"{contract_where}.path: contract is claimed more than once")
            claimed_contracts.add(contract_path)
            entry = profile_entries.get(contract_path)
            if entry is None:
                raise MatrixError(f"{contract_where}.path: not present in the pinned profile")
            if contract_path.startswith("contracts/"):
                contract_file = root / "tests/WebPlatformSubset" / contract_path
            else:
                contract_file = root / "tests/WebPlatformSubset/upstream" / contract_path
                expected_hash = wpt_files["files"].get(contract_path)
                if expected_hash is None:
                    raise MatrixError(f"{contract_where}.path: absent from the pinned WPT file manifest")
                actual_hash = hashlib.sha256(contract_file.read_bytes()).hexdigest() if contract_file.is_file() else None
                if actual_hash != expected_hash:
                    raise MatrixError(f"{contract_where}.path: pinned WPT file digest mismatch")
            if not contract_file.is_file():
                raise MatrixError(f"{contract_where}.path: contract file is missing")
            tokens = contract["capabilityTokens"]
            if not isinstance(tokens, list) or not tokens or len(set(tokens)) != len(tokens):
                raise MatrixError(f"{contract_where}.capabilityTokens: expected unique tokens")
            missing_tokens = set(tokens) - set(entry.get("capabilities", []))
            if missing_tokens:
                raise MatrixError(f"{contract_where}: tokens absent from profile: {sorted(missing_tokens)}")

        evidence = capability["evidence"]
        if not isinstance(evidence, list):
            raise MatrixError(f"{where}.evidence: expected an array")
        evidence_by_lane: dict[str, dict[str, int]] = {}
        for evidence_index, item in enumerate(evidence):
            evidence_where = f"{where}.evidence[{evidence_index}]"
            if not isinstance(item, dict):
                raise MatrixError(f"{evidence_where}: expected an object")
            require_exact_keys(item, {"lane", "resultFile", "resultPath", "provenance", "counts"}, evidence_where)
            lane = item["lane"]
            if lane not in LANES or lane in evidence_by_lane:
                raise MatrixError(f"{evidence_where}.lane: expected one unique browser and native lane")
            result_file_path = repository_path(root, item["resultFile"], f"{evidence_where}.resultFile")
            if result_file_path not in evidence_cache:
                evidence_cache[result_file_path] = load_json(result_file_path, max_bytes=limits["maxInputBytes"])
                validate_result_summary(evidence_cache[result_file_path], str(result_file_path.relative_to(root)))
            result_file = evidence_cache[result_file_path]
            expected_engine = "chrome" if lane == "browser" else "native"
            if result_file.get("engine") != expected_engine:
                raise MatrixError(f"{evidence_where}: {lane} lane references engine {result_file.get('engine')!r}")
            result_path = require_string(item["resultPath"], f"{evidence_where}.resultPath")
            matching = [result for result in result_file.get("results", []) if result.get("path") == result_path]
            if len(matching) != 1:
                raise MatrixError(f"{evidence_where}: expected exactly one result for {result_path}")
            normalized_result_path = result_path.removeprefix("upstream/")
            if normalized_result_path not in {contract["path"] for contract in contracts}:
                raise MatrixError(f"{evidence_where}.resultPath: does not map to this capability's contract")
            actual_counts = result_counts(matching[0], f"{evidence_where}:{result_path}")
            counts = item["counts"]
            if not isinstance(counts, dict):
                raise MatrixError(f"{evidence_where}.counts: expected an object")
            require_exact_keys(counts, set(COUNT_KEYS), f"{evidence_where}.counts")
            if counts != actual_counts:
                raise MatrixError(f"{evidence_where}.counts: expected {actual_counts}, got {counts}")
            if counts["total"] != sum(counts[key] for key in COUNT_KEYS[:-1]):
                raise MatrixError(f"{evidence_where}.counts: denominator does not balance")

            evidence_provenance = item["provenance"]
            if not isinstance(evidence_provenance, dict):
                raise MatrixError(f"{evidence_where}.provenance: expected an object")
            require_exact_keys(evidence_provenance, {"webSceneRevision", "wptRevision", "engineIdentity"}, f"{evidence_where}.provenance")
            source_revision = require_string(evidence_provenance["webSceneRevision"], f"{evidence_where}.provenance.webSceneRevision", r"[0-9a-f]{40}")
            require_git_revision(root, source_revision, f"{evidence_where}.provenance.webSceneRevision")
            if evidence_provenance["wptRevision"] != wpt_revision:
                raise MatrixError(f"{evidence_where}.provenance.wptRevision: stale revision")
            identity_field = "identity" if lane == "browser" else "nativeEngineIdentity"
            if result_file.get(identity_field) != evidence_provenance["engineIdentity"]:
                raise MatrixError(f"{evidence_where}.provenance.engineIdentity: does not match result artifact")
            if lane == "native":
                if result_file.get("wptRevision") != wpt_revision:
                    raise MatrixError(f"{evidence_where}: native result WPT revision is stale")
                if result_file.get("profileSha256") != profile_hash:
                    raise MatrixError(f"{evidence_where}: native result profile hash is stale")
            evidence_by_lane[lane] = counts
            for key in COUNT_KEYS:
                aggregate[lane][key] += counts[key]

        if set(evidence_by_lane) != set(LANES):
            raise MatrixError(f"{where}.evidence: both browser and native lanes are required")
        combined = {key: sum(evidence_by_lane[lane][key] for lane in LANES) for key in COUNT_KEYS}
        if status == "supported" and (combined["pass"] != combined["total"] or any(combined[key] for key in ("fail", "skipped", "unavailable"))):
            raise MatrixError(f"{where}.status: supported requires every recorded assertion to pass")
        if status == "partial" and not (combined["pass"] > 0 and combined["pass"] < combined["total"]):
            raise MatrixError(f"{where}.status: partial requires both passing and non-passing evidence")
        if status == "unsupported" and not (combined["pass"] == 0 and combined["fail"] > 0):
            raise MatrixError(f"{where}.status: unsupported requires failures and no passes")
        if status == "out-of-scope" and not (combined["pass"] == 0 and combined["fail"] == 0 and combined["skipped"] + combined["unavailable"] > 0):
            raise MatrixError(f"{where}.status: out-of-scope requires only skipped/unavailable evidence")

    if areas != set(AREAS):
        raise MatrixError(f"matrix: area coverage mismatch; missing {sorted(set(AREAS) - areas)}")
    if claimed_contracts != set(profile_entries):
        missing = sorted(set(profile_entries) - claimed_contracts)
        extra = sorted(claimed_contracts - set(profile_entries))
        raise MatrixError(f"matrix/profile contract mismatch; missing={missing}, extra={extra}")
    if len(evidence_cache) > limits["maxEvidenceFiles"]:
        raise MatrixError("matrix evidence exceeds maxEvidenceFiles")
    input_bytes = (sum(path.stat().st_size for path in evidence_cache)
                   + profile_path.stat().st_size + wpt_files_path.stat().st_size)
    if input_bytes > limits["maxInputBytes"]:
        raise MatrixError(f"profile and evidence use {input_bytes} bytes, above {limits['maxInputBytes']}")
    return {
        "statusCounts": status_counts,
        "laneCounts": aggregate,
        "evidenceFiles": len(evidence_cache),
        "inputBytes": input_bytes,
    }


def format_counts(counts: dict[str, int]) -> str:
    return "/".join(str(counts[key]) for key in COUNT_KEYS)


def render_report(matrix: dict[str, Any], summary: dict[str, Any]) -> str:
    provenance = matrix["provenance"]
    limits = matrix["limits"]
    lines = [
        "# WebScene CSS capability matrix",
        "",
        "> Generated by `python3 scripts/css_capability_matrix.py`. Do not edit this report directly.",
        "",
        "This report makes bounded claims only. `supported` means every recorded browser and native assertion for the stated boundary passed. `partial` preserves every recorded failure. The denominator order is **pass / fail / skipped / unavailable / total**.",
        "",
        "## Provenance",
        "",
        "| Input | Exact revision or digest |",
        "| --- | --- |",
        f"| Matrix | `{matrix['matrixVersion']}` |",
        f"| WebScene baseline | `{provenance['webSceneBaseline']}` |",
        f"| WPT | `{provenance['wptRevision']}` |",
        f"| WPT file manifest | `{provenance['wptFiles']}` / `{provenance['wptFilesSha256']}` |",
        f"| Evidence profile | `{provenance['profile']}` / `{provenance['profileSha256']}` |",
        "",
        "## Summary",
        "",
        "| Status | Capabilities |",
        "| --- | ---: |",
    ]
    for status in STATUSES:
        lines.append(f"| {status} | {summary['statusCounts'][status]} |")
    lines.extend([
        "",
        "| Lane | Pass | Fail | Skipped | Unavailable | Total |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ])
    for lane in LANES:
        counts = summary["laneCounts"][lane]
        lines.append(f"| {lane} | {counts['pass']} | {counts['fail']} | {counts['skipped']} | {counts['unavailable']} | {counts['total']} |")
    lines.extend([
        "",
        "## Capabilities",
        "",
        "| Area | Capability | State | Native | Browser | Owners |",
        "| --- | --- | --- | ---: | ---: | --- |",
    ])
    for capability in matrix["capabilities"]:
        lane_counts = {item["lane"]: item["counts"] for item in capability["evidence"]}
        owners = ", ".join(f"#{issue}" for issue in capability["issues"])
        lines.append(
            f"| {capability['area']} | `{capability['id']}` | {capability['status']} | "
            f"{format_counts(lane_counts['native'])} | {format_counts(lane_counts['browser'])} | {owners} |"
        )
    for capability in matrix["capabilities"]:
        lines.extend([
            "",
            f"### `{capability['id']}`",
            "",
            capability["boundary"],
            "",
            f"Contract: `{capability['contracts'][0]['path']}`.",
        ])
        for item in capability["evidence"]:
            provenance_item = item["provenance"]
            lines.append(
                f"- **{item['lane']}** — {format_counts(item['counts'])}; "
                f"WebScene `{provenance_item['webSceneRevision']}`; engine `{provenance_item['engineIdentity']}`; "
                f"artifact `{item['resultFile']}`."
            )
    lines.extend([
        "",
        "## Fail-closed generation bounds",
        "",
        f"The validator accepted {len(matrix['capabilities'])}/{limits['maxCapabilities']} capability rows and "
        f"{summary['evidenceFiles']}/{limits['maxEvidenceFiles']} evidence files. Profile plus evidence inputs use "
        f"{summary['inputBytes']}/{limits['maxInputBytes']} bytes. The generated report must remain at or below "
        f"{limits['maxReportBytes']} bytes.",
        "",
        "Validation rejects stale WPT/profile digests, unknown Git revisions in a complete checkout, duplicated or orphaned profile contracts, changed engine identities, unbalanced denominators, denominators that differ from result subtests, and a `supported` claim with any non-passing assertion.",
        "",
    ])
    report = "\n".join(lines)
    report_bytes = len(report.encode())
    if report_bytes > limits["maxReportBytes"]:
        raise MatrixError(f"generated report is {report_bytes} bytes, above {limits['maxReportBytes']}")
    return report


def run(root: Path, matrix_path: Path, schema_path: Path) -> tuple[dict[str, Any], dict[str, Any], str]:
    matrix = load_json(matrix_path)
    schema = load_json(schema_path)
    summary = validate_matrix(root, matrix, schema)
    report = render_report(matrix, summary)
    return matrix, summary, report


def main() -> int:
    root = repository_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, default=root / "tests/WebPlatformSubset/css-capability-matrix.json")
    parser.add_argument("--schema", type=Path, default=root / "tests/WebPlatformSubset/css-capability-matrix.schema.json")
    parser.add_argument("--output", type=Path, default=root / "docs/validation/css-capability-matrix.md")
    parser.add_argument("--check", action="store_true", help="Fail when the generated report differs from --output")
    args = parser.parse_args()
    try:
        matrix, summary, report = run(root, args.matrix.resolve(), args.schema.resolve())
        if args.check:
            try:
                existing = args.output.read_text()
            except FileNotFoundError as error:
                raise MatrixError(f"missing generated report: {args.output}") from error
            if existing != report:
                raise MatrixError(f"generated report is stale: run {Path(__file__).name}")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(report)
        lanes = ", ".join(f"{lane} {format_counts(summary['laneCounts'][lane])}" for lane in LANES)
        print(
            f"CSS capability matrix {matrix['matrixVersion']}: {len(matrix['capabilities'])} capabilities; "
            f"{lanes}; report {len(report.encode())}/{matrix['limits']['maxReportBytes']} bytes"
        )
        return 0
    except MatrixError as error:
        print(f"CSS capability matrix validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
