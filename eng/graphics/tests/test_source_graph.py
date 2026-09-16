import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).parents[1]
sys.path.insert(0, str(HERE))
spec = importlib.util.spec_from_file_location('graphics_source_graph', HERE / 'build.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class SourceGraphTests(unittest.TestCase):
    def test_missing_head_is_rejected_with_a_focused_error(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            dependency = source / 'third_party' / 'fixture'
            dependency.mkdir(parents=True)
            subprocess.check_call(['git', 'init', str(dependency)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            with self.assertRaisesRegex(ValueError, r'missing HEAD.*third_party.*/fixture'):
                builder.source_graph(source)

    def test_clean_unborn_dawn_dependency_is_refetched_before_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            dependency = source / 'third_party' / 'fixture'
            dependency.mkdir(parents=True)
            subprocess.check_call(['git', 'init', str(dependency)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            tools = source / 'tools'
            tools.mkdir()
            (tools / 'fetch_dawn_dependencies.py').write_text(
                """#!/usr/bin/env python3
from pathlib import Path
import subprocess

source = Path(__file__).parents[1]
dependency = source / 'third_party' / 'fixture'
dependency.mkdir(parents=True)
subprocess.check_call(['git', 'init', str(dependency)], stdout=subprocess.DEVNULL)
(dependency / 'LICENSE').write_text('Pinned dependency\\n')
subprocess.check_call(['git', '-C', str(dependency), 'add', '.'])
subprocess.check_call([
    'git', '-C', str(dependency), '-c', 'user.name=SDK Test',
    '-c', 'user.email=sdk-test@example.invalid', 'commit', '-m', 'fixture'
], stdout=subprocess.DEVNULL)
""",
                encoding='utf-8')

            graph = builder.repair_dawn_dependencies(source)

            revision = subprocess.check_output(
                ['git', '-C', str(dependency), 'rev-parse', 'HEAD'], text=True).strip()
            self.assertEqual(graph, {'third_party/fixture': revision})

    def test_unborn_dependency_with_files_is_not_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            dependency = source / 'third_party' / 'fixture'
            dependency.mkdir(parents=True)
            subprocess.check_call(['git', 'init', str(dependency)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            marker = dependency / 'keep.txt'
            marker.write_text('do not discard\n')

            with self.assertRaisesRegex(ValueError, r'non-empty invalid dependency checkout'):
                builder.repair_dawn_dependencies(source)
            self.assertEqual(marker.read_text(), 'do not discard\n')

    def test_long_dependency_paths_remain_verifiable_and_edits_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            dependency = source / 'third_party' / 'rust'
            dependency.mkdir(parents=True)
            env = dict(os.environ, **builder.angle_git_environment())

            def git(*args):
                return subprocess.check_output(['git', '-C', str(dependency), *args],
                                               env=env, stderr=subprocess.DEVNULL, text=True).strip()

            git('init')
            # Force the repository's default off so the build policy must win.
            git('config', 'core.longpaths', 'false')
            relative = ('chromium_crates_io/vendor/icu_calendar-v2/src/tests/snapshots/'
                        'icu_calendar__tests__date_arithmetic_snapshot__test_date_until_snapshot__'
                        'date_until_snapshot_Ethiopian (Amete Alem).snap')
            snapshot = dependency / relative
            while len(str(snapshot)) <= 260:
                relative = 'nested/' + relative
                snapshot = dependency / relative
            if os.name == 'nt':
                # Python's runtime may lack the OS long-path opt-in. Git must
                # still handle its ordinary (unprefixed) checkout path itself.
                snapshot = Path('\\\\?\\' + str(snapshot))
            snapshot.parent.mkdir(parents=True)
            try:
                original = b'Pinned snapshot\n'
                snapshot.write_bytes(original)
                git('add', '.')
                git('-c', 'user.name=SDK Test', '-c', 'user.email=sdk-test@example.invalid',
                    'commit', '-m', 'long path fixture')
                revision = git('rev-parse', 'HEAD')
                snapshot.unlink()
                git('checkout', 'HEAD', '--', relative)
                self.assertEqual(snapshot.read_bytes(), original)
                if os.name == 'nt':
                    self.assertIn('1 file changed', git('-c', 'core.longpaths=false', 'diff', 'HEAD', '--stat'))
                self.assertEqual(builder.source_graph(source, env=env), {'third_party/rust': revision})
                snapshot.write_bytes(original + b'Actual source edit\n')
                with self.assertRaisesRegex(ValueError, 'Modified transitive dependency:'):
                    builder.source_graph(source, env=env)
            finally:
                snapshot.unlink(missing_ok=True)

    def test_sealing_preserves_checkout_policy_and_rejects_real_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'source'
            dependency = source / 'third_party' / 'fixture'
            dependency.mkdir(parents=True)
            # Deliberately make the caller's Git unusable. The supplied build
            # environment must reach every dependency verification command.
            env = dict(os.environ, GIT_CONFIG_COUNT='2',
                       GIT_CONFIG_KEY_0='core.autocrlf', GIT_CONFIG_VALUE_0='false',
                       GIT_CONFIG_KEY_1='core.eol', GIT_CONFIG_VALUE_1='lf')
            def git(*args):
                return subprocess.check_output(['git', '-C', str(dependency), *args],
                                               env=env, stderr=subprocess.DEVNULL, text=True).strip()
            git('init')
            original = b'Upstream CRLF bytes\r\nSecond line\r\n'
            (dependency / 'LICENSE').write_bytes(original)
            git('add', '.')
            git('-c', 'user.name=SDK Test', '-c', 'user.email=sdk-test@example.invalid',
                'commit', '-m', 'fixture')
            revision = git('rev-parse', 'HEAD')
            from unittest.mock import patch
            with patch.dict(os.environ, {'GIT_CONFIG_COUNT': 'invalid'}):
                self.assertEqual(builder.source_graph(source, env=env),
                                 {'third_party/fixture': revision})
                self.assertEqual((dependency / 'LICENSE').read_bytes(), original)
                (dependency / 'LICENSE').write_bytes(original + b'Actual source edit\r\n')
                with self.assertRaisesRegex(ValueError, r'Modified transitive dependency:[\s\S]*LICENSE'):
                    builder.source_graph(source, env=env)
