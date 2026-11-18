#!/usr/bin/env python3
"""Fetch the pinned NuGet native runtime, verify its manifest, and print its library path."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import platform
import urllib.request
import zipfile

VERSION = "1.0.37"


def main():
    system = {"Darwin": "osx", "Linux": "linux", "Windows": "win"}.get(platform.system())
    arch = {"aarch64": "arm64", "arm64": "arm64", "AMD64": "x64", "x86_64": "x64"}.get(platform.machine())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rid", default=f"{system}-{arch}", choices=["osx-arm64", "osx-x64", "linux-arm64", "linux-x64", "win-arm64", "win-x64"])
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "target" / "runtime")
    args = parser.parse_args()
    destination = args.output.resolve() / VERSION / args.rid
    package = f"webscene.nativeengine.runtime.{args.rid}"
    url = f"https://api.nuget.org/v3-flatcontainer/{package}/{VERSION}/{package}.{VERSION}.nupkg"
    with urllib.request.urlopen(url, timeout=120) as response:
        archive = zipfile.ZipFile(io.BytesIO(response.read()))
    prefix = f"runtimes/{args.rid}/native/"
    manifest = json.loads(archive.read(prefix + "webscene-native-runtime.json"))
    if manifest["packageVersion"] != VERSION or manifest["abiVersion"] != 3:
        raise ValueError("Unexpected native runtime manifest")
    files = {}
    for file_key, hash_key in [("fileName", "sha256"), ("icuFileName", "icuSha256"), ("snapshotFileName", "snapshotSha256"), ("snapshotMetadataFileName", "snapshotMetadataSha256")]:
        name = manifest[file_key]
        if Path(name).name != name:
            raise ValueError("Invalid runtime file name")
        data = archive.read(prefix + name)
        if hashlib.sha256(data).hexdigest().lower() != manifest[hash_key].lower():
            raise ValueError(f"Checksum mismatch: {name}")
        files[name] = data
    destination.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (destination / name).write_bytes(data)
    (destination / "webscene-native-runtime.json").write_text(json.dumps(manifest), encoding="utf-8")
    print(destination / manifest["fileName"])


if __name__ == "__main__":
    main()
