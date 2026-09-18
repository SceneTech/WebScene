#!/usr/bin/env python3
"""Audit every Mach-O image and dependency in a final macOS bundle."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import posixpath
import re
import stat
import subprocess
import sys
import tempfile
import unicodedata


SCHEMA_VERSION = 2
DEFAULT_MAXIMUM_ENTRIES = 10_000
DEFAULT_MAXIMUM_EVIDENCE_BYTES = 8 * 1024 * 1024
MAXIMUM_TOOL_OUTPUT_BYTES = 1024 * 1024
MACHO_MAGICS = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
}
DEFAULT_SYSTEM_PREFIXES = ("/System/Library/", "/usr/lib/")
LOAD_PATH_PREFIXES = ("@rpath", "@loader_path", "@executable_path")


class AuditError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_macho(path: Path) -> bool:
    try:
        with path.open("rb") as stream:
            return stream.read(4) in MACHO_MAGICS
    except OSError:
        return False


def normalized_collision_key(relative: str) -> str:
    return unicodedata.normalize("NFC", relative).casefold()


def claim_collision_key(relative: str, collisions: dict[str, str]) -> None:
    key = normalized_collision_key(relative)
    previous = collisions.get(key)
    if previous is not None:
        raise AuditError(f"case or Unicode-normalization collision: {previous} and {relative}")
    collisions[key] = relative


def _relative(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def snapshot_tree(root: Path, maximum_entries: int) -> dict[str, dict]:
    if root.is_symlink() or not root.is_dir():
        raise AuditError("bundle root must be a regular directory")
    entries: dict[str, dict] = {}
    collisions: dict[str, str] = {}
    pending = [root]
    while pending:
        directory = pending.pop()
        try:
            children = sorted(os.scandir(directory), key=lambda item: os.fsencode(item.name))
        except OSError as error:
            display = _relative(root, directory) or "."
            raise AuditError(f"cannot enumerate {display}: {error.strerror}") from error
        child_directories = []
        for child in children:
            path = Path(child.path)
            relative = _relative(root, path)
            if len(os.fsencode(relative)) > 4096:
                raise AuditError(f"bundle path exceeds 4096 bytes: {relative}")
            if len(entries) >= maximum_entries:
                raise AuditError(f"bundle exceeds the {maximum_entries}-entry audit limit")
            claim_collision_key(relative, collisions)
            metadata = child.stat(follow_symlinks=False)
            common = {
                "device": metadata.st_dev,
                "inode": metadata.st_ino,
                "mode": metadata.st_mode,
                "links": metadata.st_nlink,
                "uid": metadata.st_uid,
                "gid": metadata.st_gid,
                "size": metadata.st_size,
                "mtimeNs": metadata.st_mtime_ns,
            }
            if stat.S_ISDIR(metadata.st_mode):
                entries[relative] = {**common, "type": "directory"}
                child_directories.append(path)
            elif stat.S_ISREG(metadata.st_mode):
                entries[relative] = {**common, "type": "file", "macho": is_macho(path)}
            elif stat.S_ISLNK(metadata.st_mode):
                target = os.readlink(path)
                if os.path.isabs(target):
                    raise AuditError(
                        f"bundle symlink has an absolute target: {relative} -> {target}"
                    )
                try:
                    resolved = path.resolve(strict=True)
                    resolved_relative = resolved.relative_to(root.resolve()).as_posix()
                except FileNotFoundError as error:
                    raise AuditError(
                        f"bundle symlink is dangling: {relative} -> {target}"
                    ) from error
                except RuntimeError as error:
                    raise AuditError(f"bundle symlink cycle: {relative} -> {target}") from error
                except ValueError as error:
                    raise AuditError(
                        f"bundle symlink escapes the root: {relative} -> {target}"
                    ) from error
                if resolved_relative == ".":
                    raise AuditError(
                        f"bundle symlink resolves to the bundle root: {relative} -> {target}"
                    )
                entries[relative] = {
                    **common,
                    "type": "symlink",
                    "target": target,
                    "resolvedRelative": resolved_relative,
                }
            else:
                raise AuditError(f"unsupported special bundle entry: {relative}")
        pending.extend(reversed(child_directories))
    for relative, entry in entries.items():
        if entry["type"] == "symlink" and entry["resolvedRelative"] not in entries:
            raise AuditError(
                f"symlink target has a case mismatch: {relative} -> {entry['target']}"
            )
    return entries


def stable_metadata(entry: dict) -> tuple:
    if entry["type"] == "directory":
        # Directory identity and timestamps are not stable across equivalent
        # Windows scans. The entry inventory detects additions and removals.
        return ("directory",)
    fields = ["type", "device", "inode", "mode", "size", "mtimeNs"]
    # Windows reports POSIX ownership/link fields that are not stable file
    # identity. Ownership and hard-link invariants apply to macOS packages.
    if os.name != "nt":
        fields[4:4] = ["links", "uid", "gid"]
    return tuple(entry.get(field) for field in fields) + (
        entry.get("target"), entry.get("resolvedRelative"), entry.get("macho")
    )


def file_metadata(path: Path) -> dict:
    metadata = path.stat(follow_symlinks=False)
    return {
        "type": "file",
        "device": metadata.st_dev,
        "inode": metadata.st_ino,
        "mode": metadata.st_mode,
        "links": metadata.st_nlink,
        "uid": metadata.st_uid,
        "gid": metadata.st_gid,
        "size": metadata.st_size,
        "mtimeNs": metadata.st_mtime_ns,
        "macho": is_macho(path),
    }


def run_tool(arguments: list[str]) -> str:
    environment = dict(os.environ)
    environment.update(LC_ALL="C", LANG="C")
    completed = subprocess.run(
        arguments,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        env=environment,
    )
    output = completed.stdout
    if len(output.encode("utf-8", errors="replace")) > MAXIMUM_TOOL_OUTPUT_BYTES:
        raise AuditError(f"tool output exceeds {MAXIMUM_TOOL_OUTPUT_BYTES} bytes: {arguments[0]}")
    if completed.returncode:
        raise AuditError(f"{arguments[0]} failed: {output.strip()[-4096:]}")
    return output


def normalize_single_architecture(bundle: Path, architecture: str,
                                  maximum_entries: int = DEFAULT_MAXIMUM_ENTRIES,
                                  runner=run_tool) -> dict:
    """Atomically thin every universal Mach-O in a safe final-bundle inventory."""
    if not re.fullmatch(r"[A-Za-z0-9_]+", architecture):
        raise AuditError(f"invalid requested architecture: {architecture}")
    bundle = bundle.resolve()
    before = snapshot_tree(bundle, maximum_entries)
    candidates = []
    for relative in sorted(path for path, entry in before.items()
                           if entry["type"] == "file" and entry.get("macho")):
        entry = before[relative]
        if os.name != "nt" and entry["links"] != 1:
            raise AuditError(f"Mach-O has multiple hard links: {relative}")
        path = bundle / relative
        architectures = parse_architectures(runner(["lipo", "-archs", str(path)]))
        if architecture not in architectures:
            raise AuditError(
                f"requested architecture {architecture} is absent from {relative}: "
                f"{architectures}"
            )
        candidates.append({
            "path": relative,
            "architectures": architectures,
            "bytes": entry["size"],
            "sha256": sha256(path),
        })

    results = []
    peak_temporary_bytes = 0
    for candidate in candidates:
        relative = candidate["path"]
        path = bundle / relative
        original = before[relative]
        if candidate["architectures"] == [architecture]:
            results.append({
                "path": relative,
                "action": "unchanged",
                "originalArchitectures": candidate["architectures"],
                "resultArchitectures": [architecture],
                "originalBytes": candidate["bytes"],
                "resultBytes": candidate["bytes"],
                "originalSha256": candidate["sha256"],
                "resultSha256": candidate["sha256"],
            })
            continue
        current = path.stat(follow_symlinks=False)
        current_metadata = file_metadata(path)
        if (stable_metadata(current_metadata) != stable_metadata(original)
                or sha256(path) != candidate["sha256"]):
            raise AuditError(f"Mach-O mutated before normalization: {relative}")
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.webscene-thin-", dir=path.parent
        )
        os.close(descriptor)
        temporary = Path(temporary_name)
        try:
            runner(["lipo", str(path), "-thin", architecture,
                    "-output", str(temporary)])
            temporary_metadata = temporary.stat(follow_symlinks=False)
            if (not stat.S_ISREG(temporary_metadata.st_mode)
                    or (os.name != "nt" and temporary_metadata.st_nlink != 1)
                    or not is_macho(temporary)):
                raise AuditError(f"lipo produced an unsafe non-Mach-O file: {relative}")
            result_architectures = parse_architectures(
                runner(["lipo", "-archs", str(temporary)])
            )
            if result_architectures != [architecture]:
                raise AuditError(
                    f"lipo result has wrong architectures for {relative}: "
                    f"{result_architectures}"
                )
            if (os.name != "nt"
                    and (temporary_metadata.st_uid != original["uid"]
                         or temporary_metadata.st_gid != original["gid"])):
                try:
                    os.chown(temporary, original["uid"], original["gid"])
                except OSError as error:
                    raise AuditError(
                        f"cannot preserve ownership while normalizing {relative}"
                    ) from error
            os.chmod(temporary, stat.S_IMODE(original["mode"]))
            os.utime(temporary, ns=(current.st_atime_ns, original["mtimeNs"]))
            result_metadata = temporary.stat(follow_symlinks=False)
            if (stat.S_IMODE(result_metadata.st_mode) != stat.S_IMODE(original["mode"])
                    or (os.name != "nt" and (
                        result_metadata.st_uid != original["uid"]
                        or result_metadata.st_gid != original["gid"]))
                    or result_metadata.st_mtime_ns != original["mtimeNs"]):
                raise AuditError(f"cannot preserve metadata while normalizing {relative}")
            result_bytes = result_metadata.st_size
            if result_bytes > candidate["bytes"]:
                raise AuditError(f"lipo result grew while normalizing {relative}")
            peak_temporary_bytes = max(peak_temporary_bytes, result_bytes)
            result_hash = sha256(temporary)
            with temporary.open("rb") as stream:
                os.fsync(stream.fileno())
            if (stable_metadata(current_metadata) != stable_metadata(file_metadata(path))
                    or sha256(path) != candidate["sha256"]):
                raise AuditError(f"Mach-O mutated during normalization: {relative}")
            os.replace(temporary, path)
            if os.name != "nt":
                directory_descriptor = os.open(path.parent, os.O_RDONLY)
                try:
                    os.fsync(directory_descriptor)
                finally:
                    os.close(directory_descriptor)
            results.append({
                "path": relative,
                "action": "thinned",
                "originalArchitectures": candidate["architectures"],
                "resultArchitectures": [architecture],
                "originalBytes": candidate["bytes"],
                "resultBytes": result_bytes,
                "originalSha256": candidate["sha256"],
                "resultSha256": result_hash,
            })
        finally:
            temporary.unlink(missing_ok=True)

    after = snapshot_tree(bundle, maximum_entries)
    if set(before) != set(after):
        changed = sorted(set(before) ^ set(after))
        raise AuditError(f"bundle inventory changed during normalization: {changed[0]}")
    result_by_path = {item["path"]: item for item in results}
    for relative in sorted(before):
        if relative not in result_by_path:
            if stable_metadata(before[relative]) != stable_metadata(after[relative]):
                raise AuditError(f"bundle mutated during normalization: {relative}")
            continue
        result = result_by_path[relative]
        if ((os.name != "nt" and after[relative]["links"] != 1)
                or not after[relative].get("macho")):
            raise AuditError(f"normalized Mach-O became unsafe: {relative}")
        if result["action"] == "unchanged":
            if (stable_metadata(before[relative]) != stable_metadata(after[relative])
                    or sha256(bundle / relative) != result["resultSha256"]):
                raise AuditError(f"unchanged Mach-O was modified: {relative}")
        elif (after[relative]["mode"] != before[relative]["mode"]
              or (os.name != "nt" and (
                  after[relative]["uid"] != before[relative]["uid"]
                  or after[relative]["gid"] != before[relative]["gid"]))
              or after[relative]["mtimeNs"] != before[relative]["mtimeNs"]
              or after[relative]["size"] != result["resultBytes"]
              or sha256(bundle / relative) != result["resultSha256"]):
            raise AuditError(f"normalized Mach-O mutated after replacement: {relative}")

    original_bytes = sum(item["originalBytes"] for item in results)
    result_bytes = sum(item["resultBytes"] for item in results)
    return {
        "enabled": True,
        "requestedArchitecture": architecture,
        "files": results,
        "summary": {
            "machoFiles": len(results),
            "thinnedFiles": sum(item["action"] == "thinned" for item in results),
            "originalBytes": original_bytes,
            "resultBytes": result_bytes,
            "savedBytes": original_bytes - result_bytes,
            "peakTemporaryBytes": peak_temporary_bytes,
        },
    }


def parse_architectures(output: str) -> list[str]:
    architectures = output.strip().split()
    if not architectures or any(
            not re.fullmatch(r"[A-Za-z0-9_]+", value) for value in architectures):
        raise AuditError("lipo returned an invalid architecture set")
    if len(set(architectures)) != len(architectures):
        raise AuditError("lipo returned duplicate architectures")
    return sorted(architectures)


def _load_commands(output: str) -> list[tuple[str, list[str]]]:
    commands: list[tuple[str, list[str]]] = []
    name = None
    lines: list[str] = []
    for raw in output.splitlines():
        stripped = raw.strip()
        if stripped.startswith("cmd "):
            if name is not None:
                commands.append((name, lines))
            name = stripped[4:]
            lines = []
        elif name is not None:
            lines.append(stripped)
    if name is not None:
        commands.append((name, lines))
    return commands


def _command_value(lines: list[str], prefix: str) -> str | None:
    for line in lines:
        if line.startswith(prefix):
            value = line[len(prefix):]
            return value.split(" (offset", 1)[0].strip()
    return None


def parse_load_commands(output: str) -> dict:
    rpaths = []
    identifiers = []
    deployments = []
    for command, lines in _load_commands(output):
        if command == "LC_RPATH":
            value = _command_value(lines, "path ")
            if value is None:
                raise AuditError("LC_RPATH is missing its path")
            rpaths.append(value)
        elif command == "LC_ID_DYLIB":
            value = _command_value(lines, "name ")
            if value is None:
                raise AuditError("LC_ID_DYLIB is missing its name")
            identifiers.append(value)
        elif command == "LC_BUILD_VERSION":
            value = _command_value(lines, "minos ")
            if value is None:
                raise AuditError("LC_BUILD_VERSION is missing minos")
            deployments.append(value)
        elif command == "LC_VERSION_MIN_MACOSX":
            value = _command_value(lines, "version ")
            if value is None:
                raise AuditError("LC_VERSION_MIN_MACOSX is missing version")
            deployments.append(value)
    if len(identifiers) > 1:
        raise AuditError("Mach-O contains multiple LC_ID_DYLIB commands")
    if len(set(rpaths)) != len(rpaths):
        raise AuditError("Mach-O contains duplicate LC_RPATH commands")
    return {
        "rpaths": rpaths,
        "identifier": identifiers[0] if identifiers else None,
        "deploymentTargets": deployments,
    }


def parse_dependencies(output: str) -> list[str]:
    dependencies = []
    for line in output.splitlines()[1:]:
        match = re.match(r"^\s+(.+?) \(compatibility version ", line)
        if match:
            dependencies.append(match.group(1))
    if len(dependencies) != len(set(dependencies)):
        raise AuditError("Mach-O contains duplicate dependency commands")
    return dependencies


def version_tuple(value: str) -> tuple[int, int, int]:
    if not re.fullmatch(r"\d+(?:\.\d+){0,2}", value):
        raise AuditError(f"invalid macOS deployment target: {value}")
    values = [int(part) for part in value.split(".")]
    return tuple((values + [0, 0])[:3])


def is_system_path(value: str, prefixes: tuple[str, ...]) -> bool:
    return posixpath.normpath(value) == value and any(
        value.startswith(prefix) for prefix in prefixes
    )


def normalized_relative(value: str, context: str) -> str:
    if "\x00" in value or "\\" in value:
        raise AuditError(f"invalid {context}: {value!r}")
    normalized = posixpath.normpath(value)
    if normalized in ("", ".", "..") or normalized.startswith("../") or normalized.startswith("/"):
        raise AuditError(f"escaping {context}: {value}")
    return normalized


def expand_load_path(value: str, loader: str, executable: str) -> str | None:
    for prefix, base in (("@loader_path", PurePosixPath(loader).parent),
                         ("@executable_path", PurePosixPath(executable).parent)):
        if value == prefix:
            return base.as_posix()
        if value.startswith(prefix + "/"):
            suffix = value[len(prefix) + 1:]
            if "\x00" in suffix or "\\" in suffix or suffix.startswith("/"):
                raise AuditError(f"invalid load path: {value!r}")
            return normalized_relative((base / suffix).as_posix(), "load path")
    return None


def validate_install_name(value: str, context: str, system_prefixes: tuple[str, ...]) -> None:
    if value.startswith("/"):
        if not is_system_path(value, system_prefixes):
            raise AuditError(f"absolute non-system {context}: {value}")
        return
    prefix = next((item for item in LOAD_PATH_PREFIXES
                   if value == item or value.startswith(item + "/")), None)
    if prefix is None:
        raise AuditError(f"unsupported {context}: {value}")
    if value == prefix:
        raise AuditError(f"incomplete {context}: {value}")
    normalized_relative(value[len(prefix) + 1:], context)


def _resolved_entry(relative: str, entries: dict[str, dict], context: str) -> tuple[str, dict]:
    seen = set()
    while True:
        entry = entries.get(relative)
        if entry is not None:
            if entry["type"] != "symlink":
                return relative, entry
            prefix = relative
            suffix = ""
        else:
            parts = PurePosixPath(relative).parts
            prefix = ""
            suffix = ""
            for index in range(len(parts) - 1, 0, -1):
                candidate = PurePosixPath(*parts[:index]).as_posix()
                candidate_entry = entries.get(candidate)
                if candidate_entry is not None and candidate_entry["type"] == "symlink":
                    prefix = candidate
                    suffix = PurePosixPath(*parts[index:]).as_posix()
                    entry = candidate_entry
                    break
            if not prefix:
                raise AuditError(f"dangling or case-mismatched {context}: {relative}")
        if prefix in seen:
            raise AuditError(f"symlink cycle while resolving {context}: {prefix}")
        seen.add(prefix)
        relative = entry["resolvedRelative"]
        if suffix:
            relative = normalized_relative(
                (PurePosixPath(relative) / suffix).as_posix(), context
            )


def _resolve_rpath(value: str, owner: str, executable: str,
                   entries: dict[str, dict]) -> str:
    if value.startswith("@rpath") or value.startswith("/"):
        raise AuditError(f"unsupported LC_RPATH: {value}")
    relative = expand_load_path(value, owner, executable)
    if relative is None:
        raise AuditError(f"unsupported LC_RPATH: {value}")
    resolved, entry = _resolved_entry(relative, entries, "LC_RPATH")
    if entry["type"] != "directory":
        raise AuditError(f"LC_RPATH does not resolve to a directory: {value}")
    return resolved


def _resolve_dependency(value: str, loader: str, executable: str,
                        rpaths: list[tuple[str, str]], entries: dict[str, dict],
                        system_prefixes: tuple[str, ...]) -> dict:
    validate_install_name(value, "dependency", system_prefixes)
    if value.startswith("/"):
        return {"name": value, "kind": "system"}
    candidates: list[str] = []
    if value.startswith("@rpath/"):
        suffix = normalized_relative(value[len("@rpath/"):], "dependency")
        for rpath, owner in rpaths:
            base = expand_load_path(rpath, owner, executable)
            if base is None:
                raise AuditError(f"unsupported LC_RPATH: {rpath}")
            candidates.append(normalized_relative((PurePosixPath(base) / suffix).as_posix(),
                                                  "dependency"))
    else:
        expanded = expand_load_path(value, loader, executable)
        if expanded is not None:
            candidates.append(expanded)
    for candidate in candidates:
        try:
            resolved, entry = _resolved_entry(candidate, entries, "dependency")
        except AuditError:
            continue
        if entry["type"] == "file" and entry.get("macho"):
            return {"name": value, "kind": "bundle", "resolvedPath": resolved}
    raise AuditError(f"unresolved bundled dependency in {loader}: {value}")


def audit_bundle(bundle: Path, executable: str, architectures: list[str],
                 maximum_deployment_target: str,
                 system_prefixes: tuple[str, ...] = DEFAULT_SYSTEM_PREFIXES,
                 maximum_entries: int = DEFAULT_MAXIMUM_ENTRIES,
                 runner=run_tool, normalization: dict | None = None) -> dict:
    bundle = bundle.resolve()
    executable = normalized_relative(executable, "executable path")
    expected_architectures = sorted(set(architectures))
    if not expected_architectures or len(expected_architectures) != len(architectures):
        raise AuditError("configured architectures must be nonempty and unique")
    maximum_version = version_tuple(maximum_deployment_target)
    if not system_prefixes or len(set(system_prefixes)) != len(system_prefixes):
        raise AuditError("system prefixes must be nonempty and unique")
    for prefix in system_prefixes:
        if (not prefix.startswith("/") or prefix == "/" or not prefix.endswith("/")
                or posixpath.normpath(prefix) + "/" != prefix):
            raise AuditError(f"invalid system prefix: {prefix}")
    before = snapshot_tree(bundle, maximum_entries)
    executable_entry = before.get(executable)
    if (not executable_entry or executable_entry["type"] != "file"
            or not executable_entry.get("macho")):
        raise AuditError(f"bundle executable is not a regular Mach-O file: {executable}")
    binaries = []
    load_commands: dict[str, dict[str, dict]] = {}
    hashes: dict[str, str] = {}
    for relative in sorted(path for path, entry in before.items()
                           if entry["type"] == "file" and entry.get("macho")):
        path = bundle / relative
        actual_architectures = parse_architectures(runner(["lipo", "-archs", str(path)]))
        if actual_architectures != expected_architectures:
            raise AuditError(
                f"architecture mismatch in {relative}: expected {expected_architectures}, "
                f"found {actual_architectures}"
            )
        slice_commands = {}
        for architecture in actual_architectures:
            commands = parse_load_commands(
                runner(["otool", "-arch", architecture, "-l", str(path)])
            )
            if len(commands["deploymentTargets"]) != 1:
                raise AuditError(
                    f"{relative} ({architecture}) must contain exactly one macOS deployment target"
                )
            target = commands["deploymentTargets"][0]
            if version_tuple(target) > maximum_version:
                raise AuditError(
                    f"{relative} ({architecture}) requires macOS {target}, above package maximum "
                    f"{maximum_deployment_target}"
                )
            if commands["identifier"] is not None:
                validate_install_name(
                    commands["identifier"], "dylib install name", system_prefixes
                )
                if commands["identifier"].startswith("/"):
                    raise AuditError(
                        f"bundled dylib has a system-path install name in {relative} "
                        f"({architecture}): {commands['identifier']}"
                    )
            for rpath in commands["rpaths"]:
                _resolve_rpath(rpath, relative, executable, before)
            slice_commands[architecture] = commands
        load_commands[relative] = slice_commands
        hashes[relative] = sha256(path)

    for relative in sorted(load_commands):
        slices = []
        for architecture in expected_architectures:
            commands = load_commands[relative][architecture]
            raw_dependencies = parse_dependencies(
                runner(["otool", "-arch", architecture, "-L", str(bundle / relative)])
            )
            identifier = commands["identifier"]
            if identifier is not None:
                if identifier not in raw_dependencies:
                    raise AuditError(
                        f"dylib install name is absent from otool dependencies: "
                        f"{relative} ({architecture})"
                    )
                raw_dependencies.remove(identifier)
            own_rpaths = [(value, relative) for value in commands["rpaths"]]
            executable_rpaths = [
                (value, executable)
                for value in load_commands[executable][architecture]["rpaths"]
            ]
            inherited = [] if relative == executable else executable_rpaths
            resolved_dependencies = [
                _resolve_dependency(value, relative, executable,
                                    own_rpaths + inherited, before, system_prefixes)
                for value in raw_dependencies
            ]
            slices.append({
                "architecture": architecture,
                "deploymentTarget": commands["deploymentTargets"][0],
                "installName": identifier,
                "rpaths": commands["rpaths"],
                "dependencies": resolved_dependencies,
            })
        binaries.append({
            "path": relative,
            "bytes": before[relative]["size"],
            "sha256": hashes[relative],
            "architectures": expected_architectures,
            "slices": slices,
        })

    after = snapshot_tree(bundle, maximum_entries)
    if set(before) != set(after):
        changed = sorted(set(before) ^ set(after))
        raise AuditError(f"bundle mutated during audit: {changed[0]}")
    for relative in sorted(before):
        if stable_metadata(before[relative]) != stable_metadata(after[relative]):
            raise AuditError(f"bundle mutated during audit: {relative}")
    for relative, digest in hashes.items():
        if sha256(bundle / relative) != digest:
            raise AuditError(f"Mach-O mutated during audit: {relative}")

    symlinks = [
        {"path": relative, "target": entry["target"],
         "resolvedPath": entry["resolvedRelative"]}
        for relative, entry in sorted(before.items()) if entry["type"] == "symlink"
    ]
    return {
        "schemaVersion": SCHEMA_VERSION,
        "configuration": {
            "architectures": expected_architectures,
            "maximumDeploymentTarget": maximum_deployment_target,
            "maximumEntries": maximum_entries,
            "systemPrefixes": list(system_prefixes),
        },
        "summary": {
            "entriesScanned": len(before),
            "machoFiles": len(binaries),
            "symlinks": len(symlinks),
        },
        "executable": executable,
        "binaries": binaries,
        "symlinks": symlinks,
        "normalization": normalization or {
            "enabled": False,
            "requestedArchitecture": None,
            "files": [],
            "summary": {
                "machoFiles": len(binaries),
                "thinnedFiles": 0,
                "originalBytes": sum(item["bytes"] for item in binaries),
                "resultBytes": sum(item["bytes"] for item in binaries),
                "savedBytes": 0,
                "peakTemporaryBytes": 0,
            },
        },
    }


def default_deployment_target(bundle: Path) -> str:
    path = bundle / "Contents/Info.plist"
    try:
        with path.open("rb") as stream:
            info = plistlib.load(stream)
    except (OSError, plistlib.InvalidFileException) as error:
        raise AuditError("bundle Info.plist is missing or invalid") from error
    value = info.get("LSMinimumSystemVersion")
    if not isinstance(value, str):
        raise AuditError("bundle Info.plist lacks LSMinimumSystemVersion")
    version_tuple(value)
    return value


def encode_evidence(evidence: dict, maximum_bytes: int) -> bytes:
    payload = (json.dumps(evidence, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode()
    if len(payload) > maximum_bytes:
        raise AuditError(f"audit evidence exceeds {maximum_bytes} bytes")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--executable", required=True,
                        help="Bundle-relative main executable path")
    parser.add_argument("--architecture", action="append", required=True)
    parser.add_argument("--maximum-deployment-target")
    parser.add_argument("--system-prefix", action="append")
    parser.add_argument("--maximum-entries", type=int, default=DEFAULT_MAXIMUM_ENTRIES)
    parser.add_argument("--maximum-evidence-bytes", type=int,
                        default=DEFAULT_MAXIMUM_EVIDENCE_BYTES)
    parser.add_argument("--evidence", type=Path)
    arguments = parser.parse_args()
    try:
        if arguments.maximum_entries <= 0 or arguments.maximum_evidence_bytes <= 0:
            raise AuditError("audit bounds must be positive")
        bundle = arguments.bundle.resolve()
        target = (arguments.maximum_deployment_target
                  or default_deployment_target(bundle))
        evidence = audit_bundle(
            bundle,
            arguments.executable,
            arguments.architecture,
            target,
            tuple(arguments.system_prefix or DEFAULT_SYSTEM_PREFIXES),
            arguments.maximum_entries,
        )
        payload = encode_evidence(evidence, arguments.maximum_evidence_bytes)
        if arguments.evidence:
            arguments.evidence.parent.mkdir(parents=True, exist_ok=True)
            arguments.evidence.write_bytes(payload)
        else:
            sys.stdout.buffer.write(payload)
    except (AuditError, OSError) as error:
        print(f"Mach-O bundle audit failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
