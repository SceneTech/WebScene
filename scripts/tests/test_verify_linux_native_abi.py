from __future__ import annotations

import importlib.util
import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "verify-linux-native-abi.py"
SPEC = importlib.util.spec_from_file_location("verify_linux_native_abi", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def elf_text(machine: str = "AArch64", glibc: str = "2.27", *, runpath: bool = False) -> str:
    path_line = " 0x0 (RUNPATH) Library runpath: [/workspace/out]" if runpath else ""
    return f"""
  Machine:                           {machine}
 0x0 (NEEDED) Shared library: [libc.so.6]
 0x0 (NEEDED) Shared library: [libstdc++.so.6]
 {path_line}
  Name: GLIBC_{glibc}
  Name: GLIBCXX_3.4.24
  Name: CXXABI_1.3.11
  42: 0 8 FUNC GLOBAL DEFAULT 12 webscene_engine_get_abi_version
"""


class LinuxNativeAbiVerifierTests(unittest.TestCase):
    def test_accepts_arm64_at_contract_ceiling(self) -> None:
        report = MODULE.verify_text(elf_text(), "linux-arm64", "2.27", "3.4.24", "1.3.11")
        self.assertEqual("pass", report["status"], report)

    def test_rejects_newer_glibc(self) -> None:
        report = MODULE.verify_text(elf_text(glibc="2.28"), "linux-arm64", "2.27", "3.4.24", "1.3.11")
        self.assertEqual("fail", report["status"])
        self.assertTrue(any("GLIBC requires 2.28" in issue for issue in report["issues"]))

    def test_rejects_wrong_architecture_and_runpath(self) -> None:
        report = MODULE.verify_text(elf_text(machine="Advanced Micro Devices X86-64", runpath=True), "linux-arm64", "2.27", "3.4.24", "1.3.11")
        self.assertEqual("fail", report["status"])
        self.assertTrue(any("ELF machine" in issue for issue in report["issues"]))
        self.assertTrue(any("RPATH/RUNPATH" in issue for issue in report["issues"]))

    def test_rejects_missing_contract_export(self) -> None:
        report = MODULE.verify_text(
            elf_text(), "linux-arm64", "2.27", "3.4.24", "1.3.11",
            {"webscene_engine_get_abi_version", "webscene_engine_create"},
        )
        self.assertEqual("fail", report["status"])
        self.assertTrue(any("webscene_engine_create" in issue for issue in report["issues"]))

    def test_rejects_interpreter_on_shared_library(self) -> None:
        text = elf_text() + "\n[Requesting program interpreter: /lib/ld-linux-aarch64.so.1]\n"
        report = MODULE.verify_text(text, "linux-arm64", "2.27", "3.4.24", "1.3.11")
        self.assertEqual("fail", report["status"])
        self.assertTrue(any("ELF interpreter" in issue for issue in report["issues"]))

    def test_accepts_glibc_architecture_loader_dependency(self) -> None:
        text = elf_text() + "\n 0x0 (NEEDED) Shared library: [ld-linux-aarch64.so.1]\n"
        report = MODULE.verify_text(text, "linux-arm64", "2.27", "3.4.24", "1.3.11")
        self.assertEqual("pass", report["status"], report)

    def test_rejects_dynamic_openssl_and_zlib_dependencies(self) -> None:
        text = elf_text() + """
 0x0 (NEEDED) Shared library: [libssl.so.1.1]
 0x0 (NEEDED) Shared library: [libcrypto.so.1.1]
 0x0 (NEEDED) Shared library: [libz.so.1]
"""
        report = MODULE.verify_text(text, "linux-arm64", "2.27", "3.4.24", "1.3.11")
        self.assertEqual("fail", report["status"])
        self.assertTrue(any("libssl.so.1.1" in issue for issue in report["issues"]))


if __name__ == "__main__":
    unittest.main()
