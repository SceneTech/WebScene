#!/usr/bin/env python3
"""Exercise the installed SDK audit tool against real macOS Mach-O binaries."""
from __future__ import annotations

import json
import os
from pathlib import Path
import platform
import resource
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, text=True, **kwargs)


def main():
    if sys.platform != "darwin":
        print("installed macOS bundle audit: skipped (requires macOS)")
        return 0
    architecture = platform.machine()
    if architecture not in ("arm64", "x86_64"):
        raise RuntimeError(f"unsupported test architecture: {architecture}")
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        build = work / "sdk-build"
        prefix = work / "sdk"
        run([
            "cmake", "-S", str(ROOT / "src/WebScene.Sdk"), "-B", str(build),
            "-DWEBSCENE_SDK_TOOLS_ONLY=ON", f"-DCMAKE_INSTALL_PREFIX={prefix}",
        ], stdout=subprocess.DEVNULL)
        run(["cmake", "--install", str(build)], stdout=subprocess.DEVNULL)
        tool = prefix / "share/webscene/tools/audit_macos_bundle.py"
        schema_path = prefix / "share/webscene/schemas/webscene-macho-audit.schema.json"
        package_tool = prefix / "share/webscene/tools/package_macos.py"
        for installed in (tool, schema_path, package_tool):
            if not installed.is_file():
                relative = installed.relative_to(prefix)
                raise RuntimeError(f"installed SDK surface is missing {relative}")

        bundle = work / "Fixture.app"
        macos = bundle / "Contents/MacOS"
        frameworks = bundle / "Contents/Frameworks"
        plugins = bundle / "Contents/PlugIns"
        for directory in (macos, frameworks, plugins):
            directory.mkdir(parents=True)
        source = work / "fixture.c"
        source.write_text("int fixture_value(void) { return 0; }\n")
        host_source = work / "host.c"
        host_source.write_text(
            "extern int fixture_value(void); int main(void) { return fixture_value(); }\n"
        )
        extension_source = work / "extension.c"
        extension_source.write_text(
            "extern int fixture_value(void); "
            "int extension_entry(void) { return fixture_value(); }\n"
        )
        library = frameworks / "libfixture.dylib"
        host = macos / "Fixture"
        extension = plugins / "sample.node"
        common = ["-arch", architecture, "-mmacosx-version-min=13.0"]
        run(["xcrun", "clang", *common, "-dynamiclib", str(source),
             "-install_name", "@rpath/libfixture.dylib", "-o", str(library)])
        run(["xcrun", "clang", *common, str(host_source), str(library),
             "-Wl,-rpath,@executable_path/../Frameworks", "-o", str(host)])
        run(["xcrun", "clang", *common, "-bundle", str(extension_source), str(library),
             "-Wl,-rpath,@loader_path/../Frameworks", "-o", str(extension)])

        evidence_path = work / "evidence.json"
        command = [
            sys.executable, str(tool), "--bundle", str(bundle),
            "--executable", "Contents/MacOS/Fixture",
            "--architecture", architecture,
            "--maximum-deployment-target", "15.0",
            "--maximum-evidence-bytes", str(64 * 1024),
            "--evidence", str(evidence_path),
        ]
        before_fds = len(list(Path("/dev/fd").iterdir()))
        before_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        started = time.monotonic()
        expected = None
        for _ in range(10):
            run(command, stdout=subprocess.DEVNULL)
            payload = evidence_path.read_bytes()
            if expected is None:
                expected = payload
            elif payload != expected:
                raise RuntimeError("installed audit evidence is not deterministic")
        elapsed = time.monotonic() - started
        after_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        after_fds = len(list(Path("/dev/fd").iterdir()))
        evidence = json.loads(expected)
        schema = json.loads(schema_path.read_text())
        if schema["properties"]["schemaVersion"]["const"] != evidence["schemaVersion"]:
            raise RuntimeError("installed evidence and schema versions differ")
        if evidence["summary"]["machoFiles"] != 3:
            raise RuntimeError("installed tool did not recursively audit all three Mach-O files")
        if not any(item["path"].endswith(".node") for item in evidence["binaries"]):
            raise RuntimeError("installed tool omitted the native extension")
        if os.fsencode(temporary) in expected:
            raise RuntimeError("installed evidence contains an absolute temporary path")
        if elapsed >= 10.0:
            raise RuntimeError(f"ten installed audits exceeded 10 seconds: {elapsed:.3f}s")
        if after_fds > before_fds + 2:
            raise RuntimeError(
                f"installed audits leaked file descriptors: {before_fds} -> {after_fds}"
            )
        if max(0, after_rss - before_rss) > 128 * 1024 * 1024:
            raise RuntimeError("installed audit child RSS grew by more than 128 MiB")
        if len(expected) >= 64 * 1024:
            raise RuntimeError("installed audit evidence exceeded 64 KiB")

        run(["xcrun", "install_name_tool", "-change", "@rpath/libfixture.dylib",
             "@rpath/missing.dylib", str(extension)])
        rejected = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, check=False)
        if rejected.returncode == 0 or "unresolved bundled dependency" not in rejected.stdout:
            raise RuntimeError("installed tool accepted a dangling native-extension dependency")
        print(
            f"installed macOS bundle audit: 3 Mach-O files, 10 stable passes in "
            f"{elapsed:.3f}s, {len(expected)} evidence bytes"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
