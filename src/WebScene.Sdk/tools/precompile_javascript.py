#!/usr/bin/env python3
"""Build V8 code caches with the SDK's native compiler, never Node's V8.

Inputs must already be JavaScript: use the project's existing TS/bundler build
step first. The original files remain deployment inputs, unchanged.
"""
from __future__ import annotations
import argparse
import hashlib
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import unquote, urlsplit

LIMIT = 32 * 1024 * 1024
JS_MIMES = {"", "text/javascript", "application/javascript", "application/ecmascript", "text/ecmascript"}


def source_bytes(path: Path) -> bytes:
    if path.stat().st_size > LIMIT:
        raise ValueError(f"JavaScript/HTML input exceeds 32 MiB: {path}")
    data = path.read_bytes()
    data.decode("utf-8")  # Do not silently replace invalid encoding or strip a BOM.
    return data


class ScriptCollector(HTMLParser):
    def __init__(self, path: Path):
        super().__init__(convert_charrefs=False)
        self.path = path
        self.active = None
        self.scripts = []
        self.dependencies = {path}
        self.ordinal = 0

    def handle_starttag(self, tag, attributes):
        attributes = dict(reversed(attributes))  # HTML keeps the first duplicate attribute.
        if tag == "base":
            raise ValueError(f"Precompiled HTML discovery does not support <base>; list the resolved scripts explicitly: {self.path}")
        if tag != "script":
            return
        self.ordinal += 1
        mime = (attributes.get("type") or "").strip().lower()
        if mime not in JS_MIMES and mime != "module":
            self.active = None
            return  # application/json, import maps and other data are not JS.
        kind = "module" if mime == "module" else "classic"
        src = attributes.get("src")
        if src is not None:
            url = urlsplit(src)
            if url.scheme or url.netloc or not url.path or url.path.startswith("/"):
                raise ValueError(f"Precompiled HTML requires local relative script src: {src!r} in {self.path}")
            target = (self.path.parent / unquote(url.path)).resolve()
            if not target.is_relative_to(self.path.parent.resolve()):
                raise ValueError(f"Script src escapes the HTML directory: {src!r}")
            if target.suffix.lower() in (".ts", ".tsx", ".jsx"):
                raise ValueError(f"Transpile {target.name} to JavaScript before precompilation")
            data = source_bytes(target)
            self.dependencies.add(target)
            self.scripts.append((kind, target.name, data))
            self.active = None
        else:
            self.active = [kind, f"{self.path.name}#inline-{self.ordinal}", []]

    def handle_data(self, text):
        if self.active is not None:
            self.active[2].append(text)

    def handle_endtag(self, tag):
        if tag == "script" and self.active is not None:
            kind, name, chunks = self.active
            self.scripts.append((kind, name, "".join(chunks).encode("utf-8")))
            self.active = None


def discover(manifest: dict):
    if not isinstance(manifest, dict) or set(manifest) != {"schemaVersion", "inputs"} or manifest["schemaVersion"] != 1:
        raise ValueError("Expected JavaScript input manifest schemaVersion 1")
    if not isinstance(manifest["inputs"], list) or not manifest["inputs"]:
        raise ValueError("Precompiled mode requires at least one JavaScript or HTML input")
    scripts, dependencies = [], set()
    for entry in manifest["inputs"]:
        if not isinstance(entry, dict) or set(entry) != {"kind", "path"} or entry["kind"] not in ("classic", "module", "html"):
            raise ValueError("Each input requires kind classic|module|html and a path")
        path = Path(entry["path"]).resolve(strict=True)
        if not path.is_file():
            raise ValueError(f"Input is not a regular file: {path}")
        data = source_bytes(path)
        dependencies.add(path)
        if entry["kind"] == "html":
            collector = ScriptCollector(path)
            collector.feed(data.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n").replace("\0", "\ufffd"))
            collector.close()
            if collector.active is not None:
                raise ValueError(f"Unclosed inline script in {path}")
            scripts.extend(collector.scripts)
            dependencies.update(collector.dependencies)
        else:
            if path.suffix.lower() in (".ts", ".tsx", ".jsx"):
                raise ValueError(f"Transpile {path.name} to JavaScript before precompilation")
            scripts.append((entry["kind"], path.name, data))
    unique = {}
    for kind, name, data in scripts:
        unique.setdefault((kind, hashlib.sha256(data).hexdigest()), (kind, name, data))
    if not unique:
        raise ValueError("No executable JavaScript found in precompiled inputs")
    if len(unique) > 4096:
        raise ValueError("Precompiled input exceeds 4096 scripts")
    return list(unique.values()), dependencies


def dep_escape(path: Path) -> str:
    return str(path).replace("\\", "/").replace("$", "$$").replace("#", "\\#").replace(" ", "\\ ").replace(":", "\\:")


def write_changed(path: Path, text: str):
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def build(compiler: Path, manifest: Path, output: Path, depfile: Path):
    scripts, dependencies = discover(json.loads(manifest.read_text(encoding="utf-8")))
    dependencies.update((manifest.resolve(), compiler.resolve(), Path(__file__).resolve()))
    generated = ["// Build-time V8 caches; source assets must also be shipped.\n"]
    with tempfile.TemporaryDirectory(prefix="webscene-jsc-") as temporary:
        root = Path(temporary)
        for index, (kind, name, data) in enumerate(scripts):
            source, cpp = root / f"{index}.js", root / f"{index}.cpp"
            source.write_bytes(data)
            subprocess.run([str(compiler), "--input", str(source), "--output", str(cpp),
                            "--kind", kind, "--name", name], check=True)
            code = cpp.read_text(encoding="utf-8")
            # Each compiler output owns an anonymous namespace. Add a distinct
            # nested namespace when aggregating multiple scripts into one TU.
            if code.count("namespace {\n") != 1:
                raise ValueError("Unexpected compiler output format; update the SDK as one unit")
            generated.append(code.replace("namespace {\n", f"namespace {{ namespace unit_{index} {{\n", 1) + "}\n")
    write_changed(output, "".join(generated))
    write_changed(depfile, dep_escape(output.resolve()) + ": " + " ".join(dep_escape(p) for p in sorted(dependencies)) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--depfile", type=Path, required=True)
    args = parser.parse_args()
    try:
        build(args.compiler, args.manifest, args.output, args.depfile)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"precompile-javascript: {error}\n")


if __name__ == "__main__":
    main()
