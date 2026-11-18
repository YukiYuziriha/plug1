"""Lightweight source-bundler regression tests; no compiler or plugin is launched."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import unquote, urlsplit
import zipfile

TOOLS = Path(__file__).resolve().parent


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), TOOLS / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


source = load('Bundle-Corresponding-Source')
release = load('Bundle-Release-Assets')
refresh = load('Refresh-Windows-Package')
verify = load('Verify-Release-Assets')


class SourceBundleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        names = set(source.LEGAL_OVERLAYS) | {
            'source/CMakeLists.txt', 'source/src/AutomaticTuner.h',
            'ThirdParty/MXTune/README.md', 'ThirdParty/MXTune/LICENSE', 'ThirdParty/MXTune/COPYING-autotalent',
            'ThirdParty/MXTune/autotalent-0.2-notice.txt',
            'ThirdParty/MXTune/pitch_detector_talent.cpp',
            'ThirdParty/MXTune/pitch_detector_talent.h', 'ThirdParty/Tuning.md',
            'docs/design/retained-prototype.txt'}
        for name in names:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture: ' + name, encoding='utf-8')
        (self.root / 'source/CMakeLists.txt').write_text('project(VocalStack VERSION 1.12.0)')
        self.output = self.root / 'output.zip'

    def git(self, repo, *args):
        if args[0] == 'rev-parse':
            return b'0123456789abcdef0123456789abcdef01234567\n'
        if args[0] == 'ls-files':
            names = [p.relative_to(self.root).as_posix() for p in self.root.rglob('*')
                     if p.is_file() and p != self.output and not p.name.endswith('.sha256')]
            return ('\0'.join(names) + '\0').encode()
        if args[0] == 'diff':
            return b'changed source'
        raise AssertionError(args)

    def run_bundle(self, *options):
        with patch.object(source, 'ROOT', self.root), patch.object(source, 'git', self.git), \
             patch.object(sys, 'argv', ['bundle', *options, '--output', str(self.output)]), \
             contextlib.redirect_stdout(io.StringIO()):
            source.main()

    def test_working_tree_includes_donor_and_honest_provenance(self):
        self.run_bundle('--working-tree')
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            manifest = json.loads(archive.read('HEAVEN-1.12.0/SOURCE-MANIFEST.json'))
            self.assertIsNone(manifest['binary_source_commit'])
            self.assertEqual(manifest['source_mode'], 'working-tree-snapshot')
            for name, expected in manifest['files_sha256'].items():
                self.assertEqual(hashlib.sha256(archive.read('HEAVEN-1.12.0/' + name)).hexdigest(), expected)
            self.assertIn('ThirdParty/MXTune/COPYING-autotalent', manifest['files_sha256'])
            self.assertIn('docs/design/retained-prototype.txt', manifest['files_sha256'])
        self.assertTrue(self.output.with_suffix('.zip.sha256').exists())

    def test_source_archive_root_uses_selected_version(self):
        (self.root / 'source/CMakeLists.txt').write_text('project(VocalStack VERSION 1.13.0)')
        self.run_bundle('--working-tree')
        with zipfile.ZipFile(self.output) as archive:
            self.assertTrue(all(name.startswith('HEAVEN-1.13.0/') for name in archive.namelist()))
            self.assertIn('HEAVEN-1.13.0/SOURCE-MANIFEST.json', archive.namelist())

    def test_committed_mode_still_refuses_dirty_source(self):
        with self.assertRaisesRegex(RuntimeError, 'differs from the binary revision'):
            self.run_bundle('--revision', 'HEAD')
        self.assertFalse(self.output.exists())

    def test_missing_mxtune_is_rejected(self):
        (self.root / 'ThirdParty/MXTune/pitch_detector_talent.cpp').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing Tuning corresponding source'):
            self.run_bundle('--working-tree')
        self.assertFalse(self.output.exists())

    def test_concurrent_source_edit_is_rejected(self):
        original = source.sha256
        changed = False

        def mutate_then_hash(path):
            nonlocal changed
            if not changed:
                path.write_bytes(path.read_bytes() + b' concurrent edit')
                changed = True
            return original(path)

        with patch.object(source, 'sha256', mutate_then_hash):
            with self.assertRaisesRegex(RuntimeError, 'Working tree changed during source export'):
                self.run_bundle('--working-tree')
        self.assertFalse(self.output.exists())

    def test_existing_zip_is_not_overwritten(self):
        self.output.write_bytes(b'keep existing output')
        with self.assertRaisesRegex(RuntimeError, 'Refusing to overwrite'):
            self.run_bundle('--working-tree')
        self.assertEqual(self.output.read_bytes(), b'keep existing output')


class ReleaseBundleTests(SourceBundleTests):
    def prepare_release(self):
        names = ['Windows-x64/CLAP/HEAVEN by lil grass block.clap',
                 'Windows-x64/VST3/HEAVEN by lil grass block.vst3/Contents/x86_64-win/HEAVEN by lil grass block.vst3',
                 'docs/Release-1.12.0-Validation.md', 'docs/Tuning.md', 'docs/Getting-Started.md',
                 'docs/Windows-Resource-Limits.md', 'docs/UiArchitecture.md', 'LICENSE']
        for name in names:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture ' + name)
        binaries = {n: release.file_digest(self.root / n) for n in names[:2]}
        provenance = {'version': '1.12.0', 'source_inputs_sha256': {
            'source/src/AutomaticTuner.h': release.file_digest(self.root / 'source/src/AutomaticTuner.h')},
            'packaged_binaries_sha256': binaries}
        (self.root / 'Windows-x64/BUILD-PROVENANCE.json').write_text(json.dumps(provenance))

    def bundle(self):
        def export_source(command, **kwargs):
            self.assertIn('--working-tree', command)
            with zipfile.ZipFile(command[-1], 'w') as archive:
                archive.writestr('source-fixture.txt', 'source export is tested separately')
        with patch.object(release, 'ROOT', self.root), \
             patch.object(release.subprocess, 'run', export_source), \
             patch.object(sys, 'argv', ['release', '--working-tree', '--skip-history',
                 '--version', '1.12.0', '--output-directory', str(self.root / 'publication')]), \
             contextlib.redirect_stdout(io.StringIO()):
            release.main()

    def test_versioned_release_preserves_bundle_and_notices(self):
        self.prepare_release()
        self.bundle()
        output = self.root / 'publication'
        with zipfile.ZipFile(output / 'HEAVEN-1.12.0-Windows-x64.zip') as archive:
            self.assertIsNone(archive.testzip())
            self.assertIn('HEAVEN-1.12.0/Windows-x64/BUILD-PROVENANCE.json', archive.namelist())
            self.assertIn('HEAVEN-1.12.0/docs/Tuning.md', archive.namelist())
            self.assertEqual(archive.read('HEAVEN-1.12.0/docs/UiArchitecture.md'),
                             (self.root / 'docs/UiArchitecture.md').read_bytes())
            self.assertIn('HEAVEN-1.12.0/Windows-x64/CLAP/HEAVEN by lil grass block.clap',
                          archive.namelist())
            self.assertFalse(any('VocalStack-1.12.0/' in name for name in archive.namelist()))
        checksums = (output / 'SHA256SUMS.txt').read_text()
        self.assertIn('HEAVEN-1.12.0-corresponding-source.zip', checksums)
        self.assertNotIn('VocalStack-source-history.zip', checksums)

    def test_changed_source_refuses_release(self):
        self.prepare_release()
        (self.root / 'source/src/AutomaticTuner.h').write_text('changed after build')
        with self.assertRaisesRegex(RuntimeError, 'Source changed since the observed build'):
            self.bundle()
        self.assertFalse((self.root / 'publication').exists())

    def test_changed_binary_refuses_release(self):
        self.prepare_release()
        (self.root / 'Windows-x64/CLAP/HEAVEN by lil grass block.clap').write_text('different binary')
        with self.assertRaisesRegex(RuntimeError, 'Packaged binary differs from observed build'):
            self.bundle()


class RefreshGuardTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'build').mkdir()
        (self.root / 'source').mkdir()
        (self.root / 'source/CMakeLists.txt').write_text('project(VocalStack VERSION 1.12.0 LANGUAGES CXX)')
        (self.root / 'build/CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Release')
        (self.root / 'build.log').write_text('HalfCpu: 6/12\nRelease link complete\n')
        (self.root / 'build.exit').write_text('0')

    def run_refresh(self, version='1.11.3'):
        with patch.object(refresh, 'ROOT', self.root), \
             patch.object(refresh, 'file_version', return_value=version), \
             patch.object(sys, 'argv', ['refresh', '--build-directory', str(self.root / 'build'),
                 '--build-log', str(self.root / 'build.log'),
                 '--build-exit-file', str(self.root / 'build.exit')]):
            refresh.main()

    def test_nonzero_build_exit_is_rejected(self):
        (self.root / 'build.exit').write_text('1')
        with self.assertRaisesRegex(RuntimeError, 'not completed successfully'):
            self.run_refresh()
        self.assertFalse((self.root / 'Windows-x64').exists())

    def test_interrupted_build_log_is_rejected(self):
        (self.root / 'build.log').write_text('HalfCpu: 6/12\n^C')
        with self.assertRaisesRegex(RuntimeError, 'not a clean resource-wrapped build'):
            self.run_refresh()

    def test_debug_tree_is_rejected(self):
        (self.root / 'build/CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Debug')
        with self.assertRaisesRegex(RuntimeError, 'Expected a Release build tree'):
            self.run_refresh()

    def test_old_pe_version_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'Binary version mismatch'):
            self.run_refresh()
        self.assertFalse((self.root / 'Windows-x64').exists())

    def prepare_migration(self):
        artifacts = self.root / 'build/VocalStack_artefacts/Release'
        for name in ('CLAP/HEAVEN by lil grass block.clap',
                     'VST3/HEAVEN by lil grass block.vst3/Contents/x86_64-win/HEAVEN by lil grass block.vst3'):
            path = artifacts / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'new binary')
        for name in ('CLAP/Vocal Stack.clap',
                     'VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3',
                     'CLAP/Unrelated.clap'):
            path = self.root / 'Windows-x64' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'keep until successful refresh')
        for name in ('LICENSE', 'COPYING.AGPLv3', 'ThirdParty/PixelifySans-OFL.txt'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('license fixture')

    def test_successful_refresh_migrates_only_known_legacy_deliveries(self):
        self.prepare_migration()
        with patch.object(refresh.subprocess, 'check_output', return_value='base-commit'), \
             contextlib.redirect_stdout(io.StringIO()):
            self.run_refresh(version='1.12.0')
        delivery = self.root / 'Windows-x64'
        self.assertFalse((delivery / 'CLAP/Vocal Stack.clap').exists())
        self.assertFalse((delivery / 'VST3/Vocal Stack.vst3').exists())
        self.assertTrue((delivery / 'CLAP/Unrelated.clap').exists())
        provenance = json.loads((delivery / 'BUILD-PROVENANCE.json').read_text())
        self.assertEqual(len(provenance['packaged_binaries_sha256']), 2)
        for name, checksum in provenance['packaged_binaries_sha256'].items():
            self.assertIn('HEAVEN by lil grass block', name)
            self.assertEqual(refresh.sha256(self.root / name), checksum)

    def test_failed_refresh_preserves_legacy_deliveries(self):
        self.prepare_migration()
        with self.assertRaisesRegex(RuntimeError, 'Binary version mismatch'):
            self.run_refresh()
        delivery = self.root / 'Windows-x64'
        self.assertTrue((delivery / 'CLAP/Vocal Stack.clap').exists())
        self.assertTrue((delivery / 'VST3/Vocal Stack.vst3').exists())
        self.assertFalse((delivery / 'BUILD-PROVENANCE.json').exists())

    def test_staging_failure_preserves_legacy_deliveries(self):
        self.prepare_migration()
        with patch.object(refresh.subprocess, 'check_output', return_value='base-commit'), \
             patch.object(refresh.shutil, 'copytree', side_effect=OSError('staging failed')):
            with self.assertRaisesRegex(OSError, 'staging failed'):
                self.run_refresh(version='1.12.0')
        delivery = self.root / 'Windows-x64'
        self.assertTrue((delivery / 'CLAP/Vocal Stack.clap').exists())
        self.assertTrue((delivery / 'VST3/Vocal Stack.vst3').exists())
        self.assertFalse((delivery / 'CLAP/HEAVEN by lil grass block.clap').exists())


class DocumentationLinksTests(unittest.TestCase):
    def test_release_document_local_links_exist(self):
        root = TOOLS.parent
        for name in ('README.md', 'docs/Building.md', 'docs/Getting-Started.md',
                     'docs/Licensing.md', 'docs/Tuning.md',
                     'docs/Release-1.12.0-Validation.md', 'ThirdParty/Tuning.md'):
            path = root / name
            text = path.read_text(encoding='utf-8')
            for target in re.findall(r'\]\(([^)]+)\)', text):
                url = urlsplit(target)
                if url.scheme or url.netloc or not url.path:
                    continue
                with self.subTest(document=name, target=target):
                    self.assertTrue((path.parent / unquote(url.path)).exists())


if __name__ == '__main__':
    unittest.main()
