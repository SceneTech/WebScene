"""Filesystem/discovery/build orchestration tests; native compiler is a fixture here.
Real V8 compiler/consumer execution is independently covered in test_v8.cjs.
"""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[4]
DRIVER = ROOT / 'src/WebScene.Sdk/tools/precompile_javascript.py'
spec = importlib.util.spec_from_file_location('precompile_javascript', DRIVER)
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


class DriverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='js inputs ')
        self.addCleanup(self.temp.cleanup)
        # macOS exposes the temporary directory through /var while filesystem
        # canonicalization returns /private/var. Keep expected dependency paths
        # canonical so the contract is independent of that system symlink.
        self.root = Path(self.temp.name).resolve()

    def file(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode() if isinstance(data, str) else data)
        return path

    def discover(self, *inputs):
        return driver.discover({'schemaVersion': 1, 'inputs': [
            {'kind': kind, 'path': str(path)} for kind, path in inputs]})

    def test_classic_and_modules_are_distinct(self):
        js = self.file('a.js', '42')
        scripts, deps = self.discover(('classic', js), ('module', js))
        self.assertEqual([s[0] for s in scripts], ['classic', 'module'])
        self.assertEqual(deps, {js})

    def test_identical_sources_are_deduplicated(self):
        a, b = self.file('a.js', '42'), self.file('b.js', '42')
        scripts, deps = self.discover(('classic', a), ('classic', b))
        self.assertEqual(len(scripts), 1)
        self.assertEqual(deps, {a, b})

    def test_html_inline_external_module_and_data(self):
        a = self.file('assets/a b.js', 'globalThis.answer = 42;')
        html = self.file('index.html', '''<script src="assets/a%20b.js?v=1"></script>
<script>const s = '&amp;';\n42;</script><script type="module">export const x = 1;</script>
<script type="application/json">{"not":"javascript"}</script>''')
        scripts, deps = self.discover(('html', html))
        self.assertEqual([s[0] for s in scripts], ['classic', 'classic', 'module'])
        self.assertEqual(scripts[1][2], b"const s = '&amp;';\n42;")
        self.assertEqual(deps, {a, html})

    def test_utf8_bom_is_not_stripped(self):
        source = self.file('a.js', b'\xef\xbb\xbf"\xc3\xa9"')
        scripts, _ = self.discover(('classic', source))
        self.assertEqual(scripts[0][2], source.read_bytes())

    def test_typescript_requires_existing_transpilation(self):
        for suffix in ('ts', 'tsx', 'jsx'):
            with self.subTest(suffix=suffix), self.assertRaisesRegex(ValueError, 'Transpile'):
                self.discover(('classic', self.file('a.'+suffix, 'const a: number = 1;')))

    def test_invalid_encoding(self):
        with self.assertRaises(UnicodeError):
            self.discover(('classic', self.file('a.js', b'\xff')))

    def test_html_unsupported_urls_and_base_fail(self):
        for markup in ('<script src="https://example.test/a.js"></script>',
                       '<script src="/absolute.js"></script>',
                       '<script src="../outside.js"></script>', '<base href="/">',
                       '<script>42;'):
            with self.subTest(markup=markup), self.assertRaises(ValueError):
                self.discover(('html', self.file('index.html', markup)))

    def test_manifest_is_strict(self):
        for manifest in ({}, {'schemaVersion': 2, 'inputs': []},
                         {'schemaVersion': 1, 'inputs': []},
                         {'schemaVersion': 1, 'inputs': [{'kind': 'typescript', 'path': 'x'}]}):
            with self.subTest(manifest=manifest), self.assertRaises(ValueError):
                driver.discover(manifest)

    def test_no_executable_scripts_is_error(self):
        with self.assertRaisesRegex(ValueError, 'No executable'):
            self.discover(('html', self.file('index.html', '<script type="application/json">{}</script>')))

    def test_depfile_escaping(self):
        self.assertEqual(driver.dep_escape(Path('a b#c$d')), 'a\\ b\\#c$$d')

    def test_aggregate_output_and_discovered_dependencies(self):
        js = self.file('a.js', '42;')
        html = self.file('index.html', '<script src="a.js"></script><script>43;</script>')
        manifest = self.file('inputs.json', json.dumps({'schemaVersion': 1, 'inputs': [{'kind':'html', 'path':str(html)}]}))
        output, depfile = self.root/'generated.cpp', self.root/'generated.d'
        calls = []
        def compile(command, **kwargs):
            calls.append(command)
            Path(command[command.index('--output')+1]).write_text('namespace {\nint sample;\n}\n')
        with patch.object(driver.subprocess, 'run', side_effect=compile):
            driver.build(Path(sys.executable), manifest, output, depfile)
        self.assertEqual(len(calls), 2)
        self.assertIn('namespace unit_0', output.read_text())
        self.assertIn('namespace unit_1', output.read_text())
        self.assertIn(driver.dep_escape(js), depfile.read_text())
        stamp = output.stat().st_mtime_ns
        with patch.object(driver.subprocess, 'run', side_effect=compile):
            driver.build(Path(sys.executable), manifest, output, depfile)
        self.assertEqual(stamp, output.stat().st_mtime_ns)

    def test_failed_compiler_cannot_replace_successful_output(self):
        js = self.file('a.js', '42;')
        manifest = self.file('inputs.json', json.dumps({'schemaVersion': 1, 'inputs': [{'kind':'classic', 'path':str(js)}]}))
        output = self.file('generated.cpp', 'previous successful build')
        with patch.object(driver.subprocess, 'run', side_effect=driver.subprocess.CalledProcessError(1, 'compiler')):
            with self.assertRaises(driver.subprocess.CalledProcessError):
                driver.build(Path(sys.executable), manifest, output, self.root/'deps.d')
        self.assertEqual(output.read_text(), 'previous successful build')


if __name__ == '__main__':
    unittest.main()
