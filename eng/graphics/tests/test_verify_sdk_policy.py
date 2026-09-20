import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "verify_sdk", ROOT / "eng/graphics/verify-sdk.py")
VERIFY_SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY_SDK)


class DawnSymbolPolicyTests(unittest.TestCase):
    def test_current_policy_is_accepted_for_every_rid(self):
        for rid in ("osx-arm64", "linux-x64", "win-x64"):
            self.assertTrue(
                VERIFY_SDK.symbol_policy_matches("current", "current", rid))

    def test_qualified_legacy_policy_is_macos_only(self):
        legacy = next(iter(VERIFY_SDK.LEGACY_OSX_DAWN_SYMBOL_POLICIES))
        self.assertTrue(
            VERIFY_SDK.symbol_policy_matches(legacy, "current", "osx-arm64"))
        self.assertFalse(
            VERIFY_SDK.symbol_policy_matches(legacy, "current", "linux-x64"))
        self.assertFalse(
            VERIFY_SDK.symbol_policy_matches(legacy, "current", "win-x64"))

    def test_unknown_policy_fails_closed(self):
        for rid in ("osx-arm64", "linux-x64", "win-x64"):
            self.assertFalse(
                VERIFY_SDK.symbol_policy_matches("unknown", "current", rid))


if __name__ == "__main__":
    unittest.main()
