"""Fail-closed contracts for the CSS capability matrix tooling."""

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "css_capability_matrix", ROOT / "scripts/css_capability_matrix.py")
matrix_tool = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(matrix_tool)


class CssCapabilityMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.matrix_path = ROOT / "tests/WebPlatformSubset/css-capability-matrix.json"
        cls.schema_path = ROOT / "tests/WebPlatformSubset/css-capability-matrix.schema.json"
        cls.report_path = ROOT / "docs/validation/css-capability-matrix.md"
        cls.matrix = matrix_tool.load_json(cls.matrix_path)
        cls.schema = matrix_tool.load_json(cls.schema_path)

    def validate(self, matrix):
        return matrix_tool.validate_matrix(ROOT, matrix, self.schema)

    def test_checked_in_matrix_and_report_are_current_and_bounded(self):
        matrix, summary, report = matrix_tool.run(
            ROOT, self.matrix_path, self.schema_path)
        self.assertEqual(report, self.report_path.read_text())
        self.assertLessEqual(len(report.encode()), matrix["limits"]["maxReportBytes"])
        self.assertLessEqual(summary["inputBytes"], matrix["limits"]["maxInputBytes"])
        self.assertEqual(
            summary["laneCounts"],
            {
                "browser": {"pass": 25, "fail": 7, "skipped": 0, "unavailable": 0, "total": 32},
                "native": {"pass": 32, "fail": 0, "skipped": 0, "unavailable": 0, "total": 32},
            },
        )

    def test_denominator_drift_is_rejected(self):
        matrix = copy.deepcopy(self.matrix)
        matrix["capabilities"][0]["evidence"][0]["counts"]["pass"] -= 1
        with self.assertRaisesRegex(matrix_tool.MatrixError, "expected .* got"):
            self.validate(matrix)

    def test_supported_claim_cannot_hide_browser_failure(self):
        matrix = copy.deepcopy(self.matrix)
        matrix["capabilities"][0]["status"] = "supported"
        with self.assertRaisesRegex(matrix_tool.MatrixError, "supported requires every recorded assertion"):
            self.validate(matrix)

    def test_stale_profile_digest_is_rejected(self):
        matrix = copy.deepcopy(self.matrix)
        matrix["provenance"]["profileSha256"] = "0" * 64
        with self.assertRaisesRegex(matrix_tool.MatrixError, "profileSha256"):
            self.validate(matrix)

    def test_stale_wpt_file_manifest_digest_is_rejected(self):
        matrix = copy.deepcopy(self.matrix)
        matrix["provenance"]["wptFilesSha256"] = "0" * 64
        with self.assertRaisesRegex(matrix_tool.MatrixError, "wptFilesSha256"):
            self.validate(matrix)

    def test_engine_identity_drift_is_rejected(self):
        matrix = copy.deepcopy(self.matrix)
        matrix["capabilities"][0]["evidence"][0]["provenance"]["engineIdentity"] = "abi=3;sha256=" + "0" * 64
        with self.assertRaisesRegex(matrix_tool.MatrixError, "engineIdentity"):
            self.validate(matrix)

    def test_missing_revision_is_rejected_in_complete_checkout(self):
        completed = matrix_tool.subprocess.CompletedProcess([], 1)
        complete = matrix_tool.subprocess.CompletedProcess([], 0, stdout="false\n")
        with patch.object(matrix_tool.subprocess, "run", side_effect=[completed, complete]):
            with self.assertRaisesRegex(matrix_tool.MatrixError, "unavailable in this complete checkout"):
                matrix_tool.require_git_revision(ROOT, "0" * 40, "test.revision")

    def test_shallow_checkout_accepts_exact_external_revision(self):
        completed = matrix_tool.subprocess.CompletedProcess([], 1)
        shallow = matrix_tool.subprocess.CompletedProcess([], 0, stdout="true\n")
        with patch.object(matrix_tool.subprocess, "run", side_effect=[completed, shallow]):
            matrix_tool.require_git_revision(ROOT, "0" * 40, "test.revision")

    def test_profile_contract_or_capability_token_cannot_be_orphaned(self):
        matrix = copy.deepcopy(self.matrix)
        matrix["capabilities"][0]["contracts"][0]["capabilityTokens"][0] = "invented-capability"
        with self.assertRaisesRegex(matrix_tool.MatrixError, "tokens absent from profile"):
            self.validate(matrix)

    def test_generation_limits_are_enforced(self):
        matrix = copy.deepcopy(self.matrix)
        matrix["limits"]["maxCapabilities"] = len(matrix["capabilities"]) - 1
        with self.assertRaisesRegex(matrix_tool.MatrixError, "exceeds maxCapabilities"):
            self.validate(matrix)

    def test_input_and_report_byte_limits_are_enforced(self):
        matrix = copy.deepcopy(self.matrix)
        matrix["limits"]["maxInputBytes"] = 1024
        with self.assertRaisesRegex(matrix_tool.MatrixError, "1024"):
            self.validate(matrix)

        matrix = copy.deepcopy(self.matrix)
        matrix["limits"]["maxReportBytes"] = 1024
        summary = self.validate(matrix)
        with self.assertRaisesRegex(matrix_tool.MatrixError, "generated report is"):
            matrix_tool.render_report(matrix, summary)

    def test_duplicate_json_keys_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate.json"
            path.write_text('{"schema":"first","schema":"second"}')
            with self.assertRaisesRegex(matrix_tool.MatrixError, "duplicate JSON key"):
                matrix_tool.load_json(path)


if __name__ == "__main__":
    unittest.main()
