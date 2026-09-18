#!/usr/bin/env python3
"""Exercise the installed SDK audit tool against real macOS Mach-O binaries."""
from __future__ import annotations

import json
import os
from pathlib import Path
import platform
import plistlib
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
        installed_before = {
            path.relative_to(prefix).as_posix(): path.read_bytes()
            for path in prefix.rglob("*") if path.is_file()
        }

        bundle = work / "Fixture.app"
        macos = bundle / "Contents/MacOS"
        frameworks = bundle / "Contents/Frameworks"
        plugins = bundle / "Contents/PlugIns"
        for directory in (macos, frameworks, plugins):
            directory.mkdir(parents=True)
        with (bundle / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({
                "CFBundleExecutable": "Fixture",
                "CFBundleIdentifier": "dev.webscene.audit-fixture",
                "CFBundlePackageType": "APPL",
                "LSMinimumSystemVersion": "13.0",
            }, stream)
        source = work / "fixture.c"
        source.write_text("int fixture_value(void) { return 0; }\n")
        host_source = work / "host.c"
        host_source.write_text(
            "extern int fixture_value(void); int main(void) { return fixture_value(); }\n"
        )
        extension_source = work / "extension.c"
        extension_source.write_text("int extension_entry(void) { return 42; }\n")
        library = frameworks / "libfixture.dylib"
        host = macos / "Fixture"
        extension = plugins / "sample.node"
        absolute_extension = plugins / "absolute.node"
        alternate_architecture = "x86_64" if architecture == "arm64" else "arm64"
        common = ["-arch", architecture, "-mmacosx-version-min=13.0"]
        run(["xcrun", "clang", *common, "-dynamiclib", str(source),
             "-install_name", "@rpath/libfixture.dylib", "-o", str(library)])
        run(["xcrun", "clang", *common, str(host_source), str(library),
             "-Wl,-rpath,@executable_path/../Frameworks", "-o", str(host)])
        extension_slices = []
        for slice_architecture in (architecture, alternate_architecture):
            output = work / f"sample-{slice_architecture}.node"
            run([
                "xcrun", "clang", "-arch", slice_architecture,
                "-mmacosx-version-min=13.0", "-bundle", str(extension_source),
                "-o", str(output),
            ])
            extension_slices.append(output)
        run(["xcrun", "lipo", "-create", *map(str, extension_slices),
             "-output", str(extension)])
        producer_identity = "/Users/runner/work/native/release/deps/libabsolute.node"
        run(["xcrun", "clang", *common, "-dynamiclib", str(extension_source),
             "-install_name", producer_identity, "-o", str(absolute_extension)])
        original_extension_bytes = extension.stat().st_size
        initial_architectures = run(
            ["xcrun", "lipo", "-archs", str(extension)], capture_output=True
        ).stdout.split()
        if set(initial_architectures) != {"arm64", "x86_64"}:
            raise RuntimeError(f"fixture is not universal: {initial_architectures}")

        run([
            sys.executable, str(package_tool), "--bundle", str(bundle),
            "--sdk", str(prefix), "--executable", "Fixture",
            "--architecture", architecture,
            "--maximum-deployment-target", "15.0",
            "--maximum-audit-entries", "10000",
            "--maximum-audit-evidence-bytes", str(64 * 1024),
        ], stdout=subprocess.DEVNULL)
        installed_after = {
            path.relative_to(prefix).as_posix(): path.read_bytes()
            for path in prefix.rglob("*") if path.is_file()
        }
        if installed_after != installed_before:
            added = sorted(set(installed_after) - set(installed_before))
            changed = sorted(
                path for path in set(installed_after) & set(installed_before)
                if installed_after[path] != installed_before[path]
            )
            raise RuntimeError(
                f"installed SDK mutated during packaging: added={added}, changed={changed}"
            )
        final_architectures = run(
            ["xcrun", "lipo", "-archs", str(extension)], capture_output=True
        ).stdout.split()
        if final_architectures != [architecture]:
            raise RuntimeError(f"installed packager did not thin .node: {final_architectures}")
        package_evidence_path = (
            bundle / "Contents/Resources/webscene-macho-audit.json"
        )
        package_payload = package_evidence_path.read_bytes()
        package_evidence = json.loads(package_payload)
        normalization = package_evidence["normalization"]
        extension_normalization = next(
            item for item in normalization["files"]
            if item["path"] == "Contents/PlugIns/sample.node"
        )
        if extension_normalization["action"] != "thinned":
            raise RuntimeError("installed packager did not record .node thinning")
        if set(extension_normalization["originalArchitectures"]) != {"arm64", "x86_64"}:
            raise RuntimeError("installed packager recorded wrong original .node slices")
        if extension_normalization["resultArchitectures"] != [architecture]:
            raise RuntimeError("installed packager recorded wrong resulting .node slice")
        if normalization["summary"]["peakTemporaryBytes"] > original_extension_bytes:
            raise RuntimeError("normalization exceeded its temporary disk bound")
        if normalization["summary"]["savedBytes"] <= 0:
            raise RuntimeError("normalization did not reduce the universal bundle")
        if normalization["summary"]["relocatedInstallNames"] != 1:
            raise RuntimeError("installed packager recorded wrong relocated identity count")
        if len(normalization["installNames"]) != 1:
            raise RuntimeError("installed packager omitted install-name evidence")
        install_name = normalization["installNames"][0]
        if (install_name["path"] != "Contents/PlugIns/absolute.node"
                or install_name["original"] != producer_identity
                or install_name["result"] != "@rpath/libabsolute.node"):
            raise RuntimeError(f"wrong install-name evidence: {install_name}")
        final_identity = run(
            ["xcrun", "otool", "-D", str(absolute_extension)], capture_output=True
        ).stdout.splitlines()[1:]
        if final_identity != ["@rpath/libabsolute.node"]:
            raise RuntimeError(f"absolute producer identity was not relocated: {final_identity}")
        if any(".webscene-thin-" in item.name for item in plugins.iterdir()):
            raise RuntimeError("normalization left a temporary file")
        if os.fsencode(temporary) in package_payload:
            raise RuntimeError("package evidence contains an absolute temporary path")
        run(["xcrun", "codesign", "--verify", "--deep", "--strict", str(bundle)])

        loader_source = work / "loader.c"
        loader_source.write_text(
            "#include <dlfcn.h>\n"
            "typedef int (*entry_t)(void);\n"
            "int main(int argc, char **argv) {\n"
            "  if (argc != 2) return 2;\n"
            "  void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);\n"
            "  if (!handle) return 3;\n"
            "  entry_t entry = (entry_t)dlsym(handle, \"extension_entry\");\n"
            "  if (!entry || entry() != 42) return 4;\n"
            "  return dlclose(handle) == 0 ? 0 : 5;\n"
            "}\n"
        )
        loader = work / "load-extension"
        run(["xcrun", "clang", *common, str(loader_source), "-o", str(loader)])
        run([str(loader), str(extension)])
        run([str(loader), str(absolute_extension)])

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
        if evidence["summary"]["machoFiles"] != 4:
            raise RuntimeError("installed tool did not recursively audit all four Mach-O files")
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
             "@rpath/missing.dylib", str(host)])
        rejected = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, check=False)
        if rejected.returncode == 0 or "unresolved bundled dependency" not in rejected.stdout:
            raise RuntimeError("installed tool accepted a dangling bundled dependency")
        print(
            f"installed macOS bundle audit: universal .node thinned, signed, loaded; "
            f"4 Mach-O files, 10 stable passes in {elapsed:.3f}s, "
            f"{len(expected)} evidence bytes"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
