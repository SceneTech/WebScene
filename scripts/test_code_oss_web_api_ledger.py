#!/usr/bin/env python3

from __future__ import annotations

import copy
import importlib.util
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("code_oss_web_api_ledger.py")
SPEC = importlib.util.spec_from_file_location("code_oss_web_api_ledger", SCRIPT)
assert SPEC and SPEC.loader
ledger_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ledger_module)


class CodeOssWebApiLedgerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.catalog = ledger_module.load_json(ledger_module.CATALOG_PATH)
        cls.snapshot = ledger_module.load_json(ledger_module.SNAPSHOT_PATH)
        cls.ledger = ledger_module.load_json(ledger_module.LEDGER_PATH)

    def test_repository_ledger_is_valid(self) -> None:
        ledger_module.validate_ledger(self.catalog, self.snapshot, self.ledger)

    def test_catalog_gap_is_rejected(self) -> None:
        ledger = copy.deepcopy(self.ledger)
        ledger["claims"].pop()
        with self.assertRaisesRegex(ledger_module.LedgerError, "exactly match"):
            ledger_module.validate_ledger(self.catalog, self.snapshot, ledger)

    def test_newly_reachable_api_is_rejected(self) -> None:
        snapshot = copy.deepcopy(self.snapshot)
        snapshot["apis"][0]["reachable"] = False
        snapshot["apis"][0]["hitCount"] = 0
        snapshot["apis"][0]["fileCount"] = 0
        snapshot["apis"][0]["examples"] = []
        with self.assertRaisesRegex(ledger_module.LedgerError, "reachability is stale"):
            ledger_module.validate_ledger(self.catalog, snapshot, self.ledger)

    def test_partial_claim_without_product_and_contract_evidence_is_rejected(self) -> None:
        ledger = copy.deepcopy(self.ledger)
        ledger["claims"][0]["state"] = "partial"
        with self.assertRaisesRegex(ledger_module.LedgerError, "require native, product, and WPT/browser"):
            ledger_module.validate_ledger(self.catalog, self.snapshot, ledger)

    def test_unknown_metadata_is_rejected(self) -> None:
        ledger = copy.deepcopy(self.ledger)
        ledger["claims"][0]["silentSkip"] = True
        with self.assertRaisesRegex(ledger_module.LedgerError, "extra=.*silentSkip"):
            ledger_module.validate_ledger(self.catalog, self.snapshot, ledger)

    def test_css_digest_drift_is_rejected(self) -> None:
        ledger = copy.deepcopy(self.ledger)
        ledger["cssSlice"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ledger_module.LedgerError, "digest is stale"):
            ledger_module.validate_ledger(self.catalog, self.snapshot, ledger)

    def test_css_denominator_drift_is_rejected(self) -> None:
        ledger = copy.deepcopy(self.ledger)
        ledger["cssSlice"]["denominators"]["browser"]["fail"] -= 1
        with self.assertRaisesRegex(ledger_module.LedgerError, "denominators changed"):
            ledger_module.validate_ledger(self.catalog, self.snapshot, ledger)

    def test_report_is_deterministic_and_bounded(self) -> None:
        first = ledger_module.render_report(self.catalog, self.snapshot, self.ledger)
        second = ledger_module.render_report(self.catalog, self.snapshot, self.ledger)
        self.assertEqual(first, second)
        self.assertLessEqual(len(first.encode("utf-8")), 131_072)
        self.assertIn("25/25 catalog entries reachable", first)
        self.assertIn("Browser assertions: 25 pass, 7 fail", first)


if __name__ == "__main__":
    unittest.main()
