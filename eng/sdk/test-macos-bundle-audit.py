#!/usr/bin/env python3
"""Portable contract and adversarial fixture tests for the macOS bundle audit."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

try:
    import resource
except ImportError:  # Windows portable contract lane.
    resource = None


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "src/WebScene.Sdk/tools/audit_macos_bundle.py"
SCHEMA = ROOT / "src/WebScene.Sdk/schemas/webscene-macho-audit.schema.json"
SPEC = importlib.util.spec_from_file_location("audit_macos_bundle", TOOL)
audit = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(audit)

MAGIC = b"\xcf\xfa\xed\xfe"
SYSTEM = "/usr/lib/libSystem.B.dylib"


def load_commands(target="14.0", rpaths=(), identifier=None):
    commands = [
        "Load command 0",
        "      cmd LC_BUILD_VERSION",
        "  cmdsize 32",
        " platform 1",
        f"    minos {target}",
        "      sdk 15.0",
    ]
    index = 1
    if identifier is not None:
        commands += [
            f"Load command {index}",
            "          cmd LC_ID_DYLIB",
            "      cmdsize 64",
            f"         name {identifier} (offset 24)",
        ]
        index += 1
    for rpath in rpaths:
        commands += [
            f"Load command {index}",
            "          cmd LC_RPATH",
            "      cmdsize 48",
            f"         path {rpath} (offset 12)",
        ]
        index += 1
    return "\n".join(commands) + "\n"


def dependencies(path, values):
    body = "\n".join(f"\t{value} (compatibility version 1.0.0, current version 1.0.0)"
                     for value in values)
    return f"{path}:\n{body}\n"


class Fixture:
    def __init__(self, root: Path, architectures=("arm64",)):
        self.bundle = root / "Fixture.app"
        self.architectures = list(architectures)
        self.host = self.add_macho("Contents/MacOS/Fixture")
        self.library = self.add_macho("Contents/Frameworks/libfixture.dylib")
        self.extension = self.add_macho("Contents/PlugIns/sample.node")
        self.commands = {
            "Contents/MacOS/Fixture": load_commands(
                rpaths=("@executable_path/../Frameworks",)
            ),
            "Contents/Frameworks/libfixture.dylib": load_commands(
                identifier="@rpath/libfixture.dylib"
            ),
            "Contents/PlugIns/sample.node": load_commands(
                rpaths=("@loader_path/../Frameworks",)
            ),
        }
        self.dependencies = {
            "Contents/MacOS/Fixture": ["@rpath/libfixture.dylib", SYSTEM],
            "Contents/Frameworks/libfixture.dylib": ["@rpath/libfixture.dylib", SYSTEM],
            "Contents/PlugIns/sample.node": ["@rpath/libfixture.dylib", SYSTEM],
        }
        self.architecture_overrides = {}
        self.calls = []
        self.mutate_on_call = None

    def add_macho(self, relative):
        path = self.bundle / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(MAGIC + relative.encode())
        return path

    def relative(self, value):
        return Path(value).relative_to(self.bundle.resolve()).as_posix()

    def runner(self, arguments):
        self.calls.append(tuple(arguments))
        if self.mutate_on_call == len(self.calls):
            self.host.write_bytes(self.host.read_bytes() + b"mutation")
        command = arguments[0]
        relative = self.relative(arguments[-1])
        if command == "lipo":
            return " ".join(self.architecture_overrides.get(relative, self.architectures)) + "\n"
        if command != "otool" or len(arguments) != 5 or arguments[1] != "-arch":
            raise AssertionError(arguments)
        architecture = arguments[2]
        if architecture not in self.architectures:
            raise AssertionError(arguments)
        if arguments[3] == "-l":
            value = self.commands[relative]
            if isinstance(value, dict):
                value = value[architecture]
            return value
        if arguments[3] == "-L":
            return dependencies(arguments[-1], self.dependencies[relative])
        raise AssertionError(arguments)

    def run(self, **kwargs):
        return audit.audit_bundle(
            self.bundle,
            "Contents/MacOS/Fixture",
            self.architectures,
            "15.0",
            runner=self.runner,
            **kwargs,
        )


class BundleAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.fixture = Fixture(Path(self.temporary.name))

    def assertAuditFails(self, pattern, **kwargs):
        with self.assertRaisesRegex(audit.AuditError, pattern):
            self.fixture.run(**kwargs)

    def test_recursive_native_extension_and_deterministic_relative_evidence(self):
        alias = self.fixture.bundle / "Contents/FrameworkAliases"
        try:
            alias.symlink_to("Frameworks", target_is_directory=True)
        except (OSError, NotImplementedError):
            pass
        first = self.fixture.run()
        second = self.fixture.run()
        self.assertEqual(first, second)
        self.assertEqual(first["summary"]["machoFiles"], 3)
        self.assertEqual(
            [item["path"] for item in first["binaries"]],
            ["Contents/Frameworks/libfixture.dylib", "Contents/MacOS/Fixture",
             "Contents/PlugIns/sample.node"],
        )
        extension = next(item for item in first["binaries"] if item["path"].endswith(".node"))
        self.assertEqual(extension["slices"][0]["dependencies"][0]["resolvedPath"],
                         "Contents/Frameworks/libfixture.dylib")
        payload = audit.encode_evidence(first, 64 * 1024)
        self.assertNotIn(os.fsencode(self.temporary.name), payload)
        self.assertEqual(payload, audit.encode_evidence(second, 64 * 1024))
        with self.assertRaisesRegex(audit.AuditError, "evidence exceeds"):
            audit.encode_evidence(first, 1)

    def test_fat_binary_is_checked_per_slice(self):
        fixture = Fixture(Path(self.temporary.name) / "fat", ("arm64", "x86_64"))
        fixture.commands["Contents/MacOS/Fixture"] = {
            "arm64": load_commands("13.0", ("@executable_path/../Frameworks",)),
            "x86_64": load_commands("12.0", ("@executable_path/../Frameworks",)),
        }
        fixture.commands["Contents/Frameworks/libfixture.dylib"] = {
            arch: load_commands("12.0", identifier="@rpath/libfixture.dylib")
            for arch in fixture.architectures
        }
        fixture.commands["Contents/PlugIns/sample.node"] = {
            arch: load_commands("12.0", ("@loader_path/../Frameworks",))
            for arch in fixture.architectures
        }
        evidence = fixture.run()
        host = next(item for item in evidence["binaries"] if item["path"].endswith("Fixture"))
        self.assertEqual([item["architecture"] for item in host["slices"]],
                         ["arm64", "x86_64"])
        self.assertEqual([item["deploymentTarget"] for item in host["slices"]],
                         ["13.0", "12.0"])

    def test_wrong_architecture_and_deployment_are_rejected(self):
        self.fixture.architecture_overrides["Contents/PlugIns/sample.node"] = ["x86_64"]
        self.assertAuditFails("architecture mismatch.*sample[.]node")
        self.fixture.architecture_overrides.clear()
        self.fixture.commands["Contents/PlugIns/sample.node"] = load_commands(
            "16.0", ("@loader_path/../Frameworks",)
        )
        self.assertAuditFails("requires macOS 16[.]0")

    def test_missing_or_ambiguous_deployment_is_rejected(self):
        self.fixture.commands["Contents/MacOS/Fixture"] = ""
        self.assertAuditFails("exactly one macOS deployment target")
        self.fixture.commands["Contents/MacOS/Fixture"] = (
            load_commands() + load_commands()
        )
        self.assertAuditFails("exactly one macOS deployment target")

    def test_malformed_and_duplicate_tool_output_is_rejected(self):
        with self.assertRaisesRegex(audit.AuditError, "duplicate architectures"):
            audit.parse_architectures("arm64 arm64")
        with self.assertRaisesRegex(audit.AuditError, "invalid architecture"):
            audit.parse_architectures("arm64,evil")
        with self.assertRaisesRegex(audit.AuditError, "duplicate LC_RPATH"):
            audit.parse_load_commands(load_commands(
                rpaths=("@loader_path", "@loader_path")
            ))
        duplicate_id = load_commands(identifier="@rpath/one") + load_commands(
            identifier="@rpath/two"
        )
        with self.assertRaisesRegex(audit.AuditError, "multiple LC_ID_DYLIB"):
            audit.parse_load_commands(duplicate_id)
        duplicate_dependency = dependencies("binary", [SYSTEM, SYSTEM])
        with self.assertRaisesRegex(audit.AuditError, "duplicate dependency"):
            audit.parse_dependencies(duplicate_dependency)

    def test_dangling_case_mismatch_and_non_macho_dependencies_are_rejected(self):
        self.fixture.dependencies["Contents/MacOS/Fixture"][0] = "@rpath/LibFixture.dylib"
        self.assertAuditFails("unresolved bundled dependency")
        self.fixture.dependencies["Contents/MacOS/Fixture"][0] = "@rpath/missing.dylib"
        self.assertAuditFails("unresolved bundled dependency")
        plain = self.fixture.bundle / "Contents/Frameworks/plain.dylib"
        plain.write_text("plain")
        self.fixture.dependencies["Contents/MacOS/Fixture"][0] = "@rpath/plain.dylib"
        self.assertAuditFails("unresolved bundled dependency")

    def test_escaping_absolute_and_malformed_load_paths_are_rejected(self):
        cases = (
            "@loader_path/../../../../outside.dylib",
            "/opt/vendor/libbad.dylib",
            "relative/libbad.dylib",
            "@rpath",
        )
        for value in cases:
            with self.subTest(value=value):
                self.fixture.dependencies["Contents/MacOS/Fixture"][0] = value
                self.assertAuditFails("dependency")
        self.fixture.dependencies["Contents/MacOS/Fixture"][0] = "@rpath/libfixture.dylib"
        self.fixture.commands["Contents/MacOS/Fixture"] = load_commands(
            rpaths=("@executable_path/../../../outside",)
        )
        self.assertAuditFails("load path|LC_RPATH")

    def test_install_name_and_rpath_integrity_are_rejected(self):
        self.fixture.commands["Contents/Frameworks/libfixture.dylib"] = load_commands(
            identifier="/tmp/libfixture.dylib"
        )
        self.assertAuditFails("absolute non-system dylib install name")
        self.fixture.commands["Contents/Frameworks/libfixture.dylib"] = load_commands(
            identifier="@rpath/libfixture.dylib"
        )
        self.fixture.dependencies["Contents/Frameworks/libfixture.dylib"].remove(
            "@rpath/libfixture.dylib"
        )
        self.assertAuditFails("install name is absent")
        self.fixture.dependencies["Contents/Frameworks/libfixture.dylib"].insert(
            0, "@rpath/libfixture.dylib"
        )
        self.fixture.commands["Contents/MacOS/Fixture"] = load_commands(
            rpaths=("@rpath/nested",)
        )
        self.assertAuditFails("unsupported LC_RPATH")

    @unittest.skipIf(os.name == "nt", "symlink creation needs elevated Windows privileges")
    def test_internal_symlink_dependency_and_invalid_symlinks(self):
        alias = self.fixture.bundle / "Contents/AliasFrameworks"
        alias.symlink_to("Frameworks", target_is_directory=True)
        self.fixture.commands["Contents/MacOS/Fixture"] = load_commands(
            rpaths=("@executable_path/../AliasFrameworks",)
        )
        evidence = self.fixture.run()
        host = next(item for item in evidence["binaries"] if item["path"].endswith("Fixture"))
        self.assertEqual(host["slices"][0]["dependencies"][0]["resolvedPath"],
                         "Contents/Frameworks/libfixture.dylib")
        alias.unlink()
        alias.symlink_to("missing", target_is_directory=True)
        self.assertAuditFails("dangling")
        alias.unlink()
        (Path(self.temporary.name) / "escape").mkdir()
        alias.symlink_to("../../escape", target_is_directory=True)
        self.assertAuditFails("escapes the root")
        alias.unlink()
        alias.symlink_to(self.fixture.bundle / "Contents/Frameworks",
                         target_is_directory=True)
        self.assertAuditFails("absolute target")

    def test_case_and_unicode_collision_key_is_portable(self):
        collisions = {}
        audit.claim_collision_key("Contents/Foo", collisions)
        with self.assertRaisesRegex(audit.AuditError, "collision"):
            audit.claim_collision_key("contents/foo", collisions)
        collisions = {}
        audit.claim_collision_key("Café", collisions)
        with self.assertRaisesRegex(audit.AuditError, "collision"):
            audit.claim_collision_key("CAFE\u0301", collisions)
        unicode_path = self.fixture.bundle / "Contents/Resources/Żółć.txt"
        unicode_path.parent.mkdir()
        unicode_path.write_text("portable")
        evidence = self.fixture.run()
        self.assertEqual(evidence["summary"]["entriesScanned"], 9)

    @unittest.skipIf(os.name == "nt", "FIFO is not portable to Windows")
    def test_special_entries_are_rejected(self):
        fifo = self.fixture.bundle / "Contents/fifo"
        os.mkfifo(fifo)
        self.assertAuditFails("unsupported special bundle entry")

    def test_mutation_is_rejected_even_when_size_and_mtime_are_restored(self):
        original = self.fixture.host.stat()

        def mutating_runner(arguments):
            output = self.fixture.runner(arguments)
            if len(self.fixture.calls) == 7:
                data = bytearray(self.fixture.host.read_bytes())
                data[-1] ^= 1
                self.fixture.host.write_bytes(data)
                os.utime(self.fixture.host, ns=(original.st_atime_ns, original.st_mtime_ns))
            return output

        with self.assertRaisesRegex(audit.AuditError, "Mach-O mutated during audit"):
            audit.audit_bundle(
                self.fixture.bundle, "Contents/MacOS/Fixture", ["arm64"], "15.0",
                runner=mutating_runner,
            )

    def test_schema_and_evidence_contract(self):
        schema = json.loads(SCHEMA.read_text())
        self.assertEqual(schema["$schema"], "https://json-schema.org/draft/2020-12/schema")
        self.assertEqual(schema["properties"]["schemaVersion"]["const"],
                         audit.SCHEMA_VERSION)
        self.assertEqual(set(schema["required"]), set(self.fixture.run()))
        self.assertIn("slices", schema["properties"]["binaries"]["items"]["required"])

    def test_packager_runs_audit_before_outer_signature(self):
        source = (ROOT / "src/WebScene.Sdk/tools/package_macos.py").read_text()
        audit_position = source.index("evidence = audit_bundle(")
        evidence_position = source.index("evidence_path.write_bytes(")
        signature_position = source.index(
            "run('codesign', '--force', '--sign', '-', str(bundle))"
        )
        verification_position = source.index(
            "run('codesign', '--verify', '--deep', '--strict', str(bundle))"
        )
        self.assertLess(audit_position, evidence_position)
        self.assertLess(evidence_position, signature_position)
        self.assertLess(signature_position, verification_position)
        for option in ("--architecture", "--maximum-deployment-target",
                       "--maximum-audit-entries", "--maximum-audit-evidence-bytes"):
            self.assertIn(option, source)

    def test_ten_thousand_entry_limits_time_fds_rss_and_evidence(self):
        fixture = Fixture(Path(self.temporary.name) / "scale")
        # Five directories + three Mach-O files + 9,992 payload files = 10,000 entries.
        payload = fixture.bundle / "payload"
        payload.mkdir()
        for index in range(9_992):
            (payload / f"{index:04x}").write_bytes(b"")
        fd_root = Path("/proc/self/fd") if Path("/proc/self/fd").is_dir() else Path("/dev/fd")
        before_fds = len(list(fd_root.iterdir())) if fd_root.is_dir() else None
        before_rss = (resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
                      if resource is not None else None)
        started = time.monotonic()
        evidence = fixture.run(maximum_entries=10_000)
        elapsed = time.monotonic() - started
        after_rss = (resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
                     if resource is not None else None)
        after_fds = len(list(fd_root.iterdir())) if fd_root.is_dir() else None
        self.assertEqual(evidence["summary"]["entriesScanned"], 10_000)
        self.assertLess(elapsed, 10.0)
        if before_fds is not None:
            self.assertLessEqual(after_fds, before_fds + 2)
        # ru_maxrss is KiB on Unix and bytes on macOS. Both bounds equal 128 MiB.
        if before_rss is not None:
            rss_multiplier = 1 if sys.platform == "darwin" else 1024
            self.assertLessEqual(max(0, after_rss - before_rss) * rss_multiplier,
                                 128 * 1024 * 1024)
        self.assertLess(len(audit.encode_evidence(evidence, 64 * 1024)), 64 * 1024)
        with self.assertRaisesRegex(audit.AuditError, "9999-entry audit limit"):
            fixture.run(maximum_entries=9_999)


if __name__ == "__main__":
    unittest.main(verbosity=2)
