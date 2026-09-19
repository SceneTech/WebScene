#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).with_name("verify-parser-components.py")
SPEC = importlib.util.spec_from_file_location("verify_parser_components", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifyParserComponentsTests(unittest.TestCase):
    @staticmethod
    def full_symbols():
        return "\n".join(MODULE.HTML_SYMBOLS + MODULE.CSS_SYMBOLS)

    @staticmethod
    def css_symbols():
        return "\n".join(MODULE.CSS_SYMBOLS)

    def test_accepts_both_budget_boundaries(self):
        report = MODULE.verify_components(
            self.full_symbols(), self.css_symbols(), 20_000_000, 18_000_000)
        self.assertEqual(report["maximumCssToFullSizeRatio"], 0.90)
        self.assertEqual(report["maximumCssSelectorArchiveBytes"], 18_000_000)

    def test_ratio_budget_fails_independently(self):
        with self.assertRaisesRegex(RuntimeError, "size ratio"):
            MODULE.verify_components(
                self.full_symbols(), self.css_symbols(), 10_000_000, 9_000_001)

    def test_absolute_budget_fails_independently(self):
        with self.assertRaisesRegex(RuntimeError, "archive size"):
            MODULE.verify_components(
                self.full_symbols(), self.css_symbols(), 25_000_000, 18_000_001)

    def test_missing_css_abi_fails(self):
        with self.assertRaisesRegex(RuntimeError, "missingCss"):
            MODULE.verify_components(
                self.full_symbols(), self.css_symbols().replace(MODULE.CSS_SYMBOLS[0], ""),
                20_000_000, 10_000_000)

    def test_retained_html_symbols_fail(self):
        for retained in (MODULE.HTML_SYMBOLS[0], "html5ever::"):
            with self.subTest(retained=retained), self.assertRaisesRegex(
                    RuntimeError, "retainedHtml"):
                MODULE.verify_components(
                    self.full_symbols(), self.css_symbols() + "\n" + retained,
                    20_000_000, 10_000_000)


if __name__ == "__main__":
    unittest.main()
