"""Installed CMake helper/depfile contracts with a fixture compiler, no SDK download."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
HELPER = ROOT / 'src/WebScene.Sdk/cmake/WebScenePrecompiledJavaScript.cmake'
DRIVER = ROOT / 'src/WebScene.Sdk/tools/precompile_javascript.py'

@unittest.skipUnless(shutil.which('cmake') and shutil.which('ninja'), 'CMake/Ninja required')
class CMakeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='precompiled cmake ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        sdk = self.root / 'sdk'
        tool = sdk / 'share/webscene/tools/precompile_javascript.py'
        tool.parent.mkdir(parents=True)
        shutil.copy2(DRIVER, tool)
        self.compiler = self.root / 'fixture-compiler'
        self.compiler.write_text(f'''#!{sys.executable}
import argparse
from pathlib import Path
p=argparse.ArgumentParser()
for a in ('input','output','kind','name'): p.add_argument('--'+a)
a=p.parse_args()
n=len(Path(a.input).read_bytes())
Path(a.output).write_text('namespace {{\\nint cache_size = '+str(n)+';\\n}}\\n')
''')
        self.compiler.chmod(0o755)
        (self.root/'main.cpp').write_text('int main() { return 0; }\n')
        (self.root/'app.js').write_text('const answer = 42;\n')
        (self.root/'index.html').write_text('<script src="app.js"></script>')
        self.prelude = f'''cmake_minimum_required(VERSION 3.28)
project(PrecompiledFixture LANGUAGES CXX)
include("{HELPER.as_posix()}")
set(WebScene_SDK_ROOT "{sdk.as_posix()}")
add_library(WebScene::Runtime INTERFACE IMPORTED)
add_executable(WebScene::JavaScriptCompiler IMPORTED)
set_property(TARGET WebScene::JavaScriptCompiler PROPERTY IMPORTED_LOCATION "{self.compiler.as_posix()}")
add_executable(app main.cpp)
'''

    def configure(self, suffix):
        (self.root/'CMakeLists.txt').write_text(self.prelude + suffix)
        return subprocess.run(['cmake', '-S', str(self.root), '-B', str(self.root/'build'), '-G', 'Ninja'], capture_output=True, text=True)

    def build(self):
        result = subprocess.run(['cmake', '--build', str(self.root/'build')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        return result

    def test_html_dependency_changes_rebuild_generated_cpp(self):
        result = self.configure('webscene_precompile_javascript(app HTML index.html)\n')
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.build()
        generated = self.root/'build/app_javascript.cpp'
        before = generated.read_text()
        self.assertIn('cache_size = 19;', before)
        (self.root/'app.js').write_text('const answer = 12345;\n')
        self.build()
        self.assertNotEqual(before, generated.read_text())
        self.assertIn('no work to do', self.build().stdout)

    def test_no_inputs_is_error(self):
        result = self.configure('webscene_precompile_javascript(app)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('requires SOURCES', result.stderr)

    def test_duplicate_declarations_are_error(self):
        result = self.configure('webscene_precompile_javascript(app SOURCES app.js)\nwebscene_precompile_javascript(app SOURCES app.js)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('one call', result.stderr)

    def test_cross_compilation_is_rejected(self):
        result = self.configure('set(CMAKE_CROSSCOMPILING TRUE)\nwebscene_precompile_javascript(app SOURCES app.js)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('target-native', result.stderr)

    def test_unknown_keyword_is_rejected(self):
        result = self.configure('webscene_precompile_javascript(app UNKNOWN app.js)\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid', result.stderr)

if __name__ == '__main__': unittest.main()
