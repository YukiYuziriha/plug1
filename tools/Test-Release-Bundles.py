"""Lightweight packaging regressions using temporary fixtures; no plugin build/run."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path

import sys
import tempfile
import unittest
from unittest.mock import patch
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
VERSION = '1.12.2'
PREFIX = 'HEAVEN-' + VERSION + '/'
CLAP = 'CLAP/HEAVEN by lil grass block.clap'
VST3 = 'VST3/HEAVEN by lil grass block.vst3/Contents/x86_64-win/HEAVEN by lil grass block.vst3'
NOTICES = ('JUCE.md', 'CLAP.txt', 'CLAP-Helpers.txt', 'CLAP-JUCE-Extensions.txt',
           'VST3-SDK.txt', 'Signalsmith-Linear.txt', 'Signalsmith-Stretch.txt',
           'PixelifySans-OFL.txt', 'Vital/LICENSE', 'Vital/src/reference.cpp')
CMAKE = ('project(VocalStack VERSION 1.12.2 LANGUAGES CXX)\n'
         'juce_add_plugin(VocalStack PRODUCT_NAME "HEAVEN by lil grass block")\n'
         + ''.join(f'FetchContent_Declare({name} GIT_REPOSITORY https://example.invalid/{name} '
                   f'GIT_TAG {"a" * 40})\n'
                   for name in ('JUCE', 'clap_juce', 'signalsmith-linear', 'signalsmith-stretch')))


class PackageFixture(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / 'output.zip'
        self.publication = self.root / 'publication'
        names = set(source.LEGAL_OVERLAYS) | {
            'LICENSE', 'COPYING.AGPLv3', 'source/src/VocalEngine.cpp',
            'source/assets/font.ttf', 'source/cmake/PatchClap.cmake',
            'docs/Getting-Started.md', 'docs/Windows-Resource-Limits.md',
            'docs/Historical-Source.md', 'docs/Historical-Source-Manifest.json'}
        names.update('ThirdParty/' + name for name in NOTICES)
        for name in names:
            self.put(name, 'fixture ' + name)
        self.put('source/CMakeLists.txt', CMAKE)
        self.put('build/CMakeCache.txt', 'CMAKE_BUILD_TYPE:STRING=Release')
        self.put('build.log', 'HalfCpu: 6/12\nRelease link complete\n')
        self.put('build.exit', '0')

    def put(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode() if isinstance(data, str) else data)
        return path

    def git(self, repo, *args):
        if args[0] == 'rev-parse':
            return b'0123456789abcdef0123456789abcdef01234567\n'
        if args[0] == 'ls-files':
            names = [p.relative_to(self.root).as_posix() for p in self.root.rglob('*') if p.is_file()]
            return ('\0'.join(names) + '\0').encode()
        if args[0] == 'ls-tree':
            return b''
        if args[0] == 'diff':
            return b'changed source'
        raise AssertionError(args)

    def export_dependency(self, repo, revision, destination, include_submodules=True):
        self.assertEqual(revision, 'a' * 40)
        destination.mkdir(parents=True)
        (destination / 'LICENSE').write_text('dependency source fixture')

    def bundle_source(self, *options):
        with patch.object(source, 'ROOT', self.root), patch.object(source, 'git', self.git), \
             patch.object(source, 'export_git', self.export_dependency), \
             patch.object(sys, 'argv', ['source', *options, '--output', str(self.output)]), \
             contextlib.redirect_stdout(io.StringIO()):
            source.main()

    def prepare_build(self):
        for name in (CLAP, VST3):
            self.put('build/VocalStack_artefacts/Release/' + name, b'new binary fixture')
        for name in ('CLAP/Vocal Stack.clap',
                     'VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3', 'CLAP/Unrelated.clap'):
            self.put('Windows-x64/' + name, b'old binary fixture')
        self.put('Windows-x64/ThirdParty/obsolete-notice.txt', 'must not survive refresh')

    def run_refresh(self, version=VERSION):
        with patch.object(refresh, 'ROOT', self.root), \
             patch.object(refresh, 'file_version', return_value=version), \
             patch.object(refresh.subprocess, 'check_output', return_value='base-commit'), \
             patch.object(sys, 'argv', ['refresh', '--build-directory', str(self.root / 'build'),
                 '--build-log', str(self.root / 'build.log'),
                 '--build-exit-file', str(self.root / 'build.exit')]), \
             contextlib.redirect_stdout(io.StringIO()):
            refresh.main()

    def prepare_release(self):
        self.prepare_build()
        self.run_refresh()
        # Refresh preserves unrelated deliveries; release packaging must reject them.
        (self.root / 'Windows-x64/CLAP/Unrelated.clap').unlink()


    def bundle(self):
        def export_source(command, **kwargs):
            self.assertIn('--working-tree', command)
            self.assertIn('--deps-cache', command)
            with patch.object(sys, 'argv', command[1:]):
                source.main()
        with patch.object(release, 'ROOT', self.root), patch.object(source, 'ROOT', self.root), \
             patch.object(source, 'git', self.git), patch.object(source, 'export_git', self.export_dependency), \
             patch.object(release.subprocess, 'run', export_source), \
             patch.object(sys, 'argv', ['release', '--working-tree',
                 '--output-directory', str(self.publication)]), contextlib.redirect_stdout(io.StringIO()):
            release.main()

    def verify(self):
        with patch.object(sys, 'argv', ['verify', '--directory', str(self.publication)]), \
             contextlib.redirect_stdout(io.StringIO()):
            verify.main()

    def rewrite_zip(self, suffix, changes):
        path = self.publication / ('HEAVEN-' + VERSION + '-' + suffix + '.zip')
        with zipfile.ZipFile(path) as archive:
            files = {name: archive.read(name) for name in archive.namelist()}
        for name, value in changes.items():
            files[PREFIX + name] = value
        with zipfile.ZipFile(path, 'w') as archive:
            for name, data in files.items():
                archive.writestr(name, data)
        checksums = []
        for name in ('corresponding-source', 'Windows-x64'):
            asset = self.publication / f'HEAVEN-{VERSION}-{name}.zip'
            checksums.append(release.file_digest(asset) + '  ' + asset.name + '\n')
        (self.publication / 'SHA256SUMS.txt').write_text(''.join(checksums), encoding='ascii')


class SourceTests(PackageFixture):
    def test_non_tuning_source_and_dependencies_have_honest_hashes(self):
        self.bundle_source('--working-tree')
        with zipfile.ZipFile(self.output) as archive:
            manifest = json.loads(archive.read(PREFIX + 'SOURCE-MANIFEST.json'))
            self.assertIsNone(manifest['binary_source_commit'])
            self.assertEqual(manifest['source_mode'], 'working-tree-snapshot')
            self.assertEqual(len(manifest['dependencies']), 4)
            for name, expected in manifest['files_sha256'].items():
                self.assertEqual(hashlib.sha256(archive.read(PREFIX + name)).hexdigest(), expected)
            self.assertIn('ThirdParty/Vital/LICENSE', manifest['files_sha256'])
            self.assertFalse(any('MXTune' in name or 'Tuning.md' in name for name in archive.namelist()))
        self.assertTrue(self.output.with_suffix('.zip.sha256').exists())

    def test_archive_root_uses_selected_version(self):
        self.put('source/CMakeLists.txt', CMAKE.replace(VERSION, '1.13.0'))
        self.bundle_source('--working-tree')
        with zipfile.ZipFile(self.output) as archive:
            self.assertTrue(all(name.startswith('HEAVEN-1.13.0/') for name in archive.namelist()))

    def test_dirty_committed_source_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'differs from the binary revision'):
            self.bundle_source('--revision', 'HEAD')
        self.assertFalse(self.output.exists())

    def test_missing_remaining_notice_is_rejected(self):
        (self.root / 'ThirdParty/CLAP.txt').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing corresponding-source notice'):
            self.bundle_source('--working-tree')
        self.assertFalse(self.output.exists())

    def test_binary_and_build_outputs_excluded(self):
        for name in ('source/build/generated.cpp', 'source/build-release/cache.txt',
                     'source/src/accidental.dll', 'tools/__pycache__/script.pyc'):
            self.put(name, 'not source')
        self.bundle_source('--working-tree')
        with zipfile.ZipFile(self.output) as archive:
            self.assertFalse(any('generated.cpp' in n or 'accidental.dll' in n or 'cache.txt' in n
                                 or '.pyc' in n for n in archive.namelist()))

    def test_concurrent_source_edit_is_rejected(self):
        original = source.sha256
        def mutate(path):
            if path == self.root / 'source/src/VocalEngine.cpp':
                path.write_bytes(path.read_bytes() + b' concurrent edit')
            return original(path)
        with patch.object(source, 'sha256', mutate):
            with self.assertRaisesRegex(RuntimeError, 'Working tree changed'):
                self.bundle_source('--working-tree')
        self.assertFalse(self.output.exists())

    def test_existing_zip_is_preserved(self):
        self.output.write_bytes(b'keep')
        with self.assertRaisesRegex(RuntimeError, 'Refusing to overwrite'):
            self.bundle_source('--working-tree')
        self.assertEqual(self.output.read_bytes(), b'keep')


class ReleaseTests(PackageFixture):
    def test_round_trip_without_tuning_or_history(self):
        self.prepare_release()
        self.bundle()
        self.verify()
        with zipfile.ZipFile(self.publication / f'HEAVEN-{VERSION}-Windows-x64.zip') as archive:
            self.assertIn(PREFIX + 'Windows-x64/' + CLAP, archive.namelist())
            self.assertFalse(any('Tuning' in name or 'MXTune' in name for name in archive.namelist()))
        self.assertNotIn('source-history', (self.publication / 'SHA256SUMS.txt').read_text())

    def test_changed_source_is_rejected(self):
        self.prepare_release()
        self.put('source/src/VocalEngine.cpp', 'changed')
        with self.assertRaisesRegex(RuntimeError, 'Source changed since'):
            self.bundle()

    def test_added_input_is_rejected(self):
        self.prepare_release()
        self.put('source/src/NewHeader.h', 'new input')
        with self.assertRaisesRegex(RuntimeError, 'Source input inventory changed'):
            self.bundle()

    def test_changed_binary_is_rejected(self):
        self.prepare_release()
        self.put('Windows-x64/' + CLAP, b'changed')
        with self.assertRaisesRegex(RuntimeError, 'Packaged binary differs'):
            self.bundle()

    def test_stale_notice_is_rejected(self):
        self.prepare_release()
        self.put('Windows-x64/ThirdParty/obsolete.txt', 'old dependency')
        with self.assertRaisesRegex(RuntimeError, 'Delivery ThirdParty tree differs'):
            self.bundle()

    def test_extra_binary_is_rejected(self):
        self.prepare_release()
        self.put('Windows-x64/CLAP/Unrelated.clap', b'not from build')
        with self.assertRaisesRegex(RuntimeError, 'Unverified binary'):
            self.bundle()

    def test_verifier_rejects_changed_binary_even_with_new_zip_checksum(self):
        self.prepare_release()
        self.bundle()
        self.rewrite_zip('Windows-x64', {'Windows-x64/' + CLAP: b'changed'})
        with self.assertRaisesRegex(RuntimeError, 'Bundled binary differs'):
            self.verify()

    def test_verifier_rejects_stale_dependency_notice(self):
        self.prepare_release()
        self.bundle()
        self.rewrite_zip('Windows-x64', {'Windows-x64/ThirdParty/obsolete.txt': b'stale'})
        with self.assertRaisesRegex(RuntimeError, 'Third-party notice inventory mismatch'):
            self.verify()

    def test_verifier_rejects_unmanifested_source(self):
        self.prepare_release()
        self.bundle()
        self.rewrite_zip('corresponding-source', {'source/src/extra.h': b'extra'})
        with self.assertRaisesRegex(RuntimeError, 'Source manifest inventory mismatch'):
            self.verify()

    def test_verifier_rejects_missing_enabled_submodule(self):
        self.prepare_release()
        self.bundle()
        path = self.publication / f'HEAVEN-{VERSION}-corresponding-source.zip'
        with zipfile.ZipFile(path) as archive:
            manifest = json.loads(archive.read(PREFIX + 'SOURCE-MANIFEST.json'))
        manifest['dependencies']['clap_juce']['submodule_gitlinks'] = [
            '160000 commit ' + 'c' * 40 + '\tclap-libs/clap']
        self.rewrite_zip('corresponding-source', {'SOURCE-MANIFEST.json': json.dumps(manifest).encode()})
        with self.assertRaisesRegex(RuntimeError, 'Missing dependency submodule source'):
            self.verify()

    def test_verifier_rejects_wrong_dependency_pin(self):
        self.prepare_release()
        self.bundle()
        path = self.publication / f'HEAVEN-{VERSION}-corresponding-source.zip'
        with zipfile.ZipFile(path) as archive:
            manifest = json.loads(archive.read(PREFIX + 'SOURCE-MANIFEST.json'))
        manifest['dependencies']['JUCE']['commit'] = 'b' * 40
        self.rewrite_zip('corresponding-source', {'SOURCE-MANIFEST.json': json.dumps(manifest).encode()})
        with self.assertRaisesRegex(RuntimeError, 'Dependency pin mismatch'):
            self.verify()


class RefreshTests(PackageFixture):
    def test_success_migrates_only_known_binaries_and_replaces_notices(self):
        self.prepare_build()
        self.run_refresh()
        delivery = self.root / 'Windows-x64'
        self.assertFalse((delivery / 'CLAP/Vocal Stack.clap').exists())
        self.assertFalse((delivery / 'VST3/Vocal Stack.vst3').exists())
        self.assertTrue((delivery / 'CLAP/Unrelated.clap').exists())
        self.assertFalse((delivery / 'ThirdParty/obsolete-notice.txt').exists())
        provenance = json.loads((delivery / 'BUILD-PROVENANCE.json').read_text())
        self.assertEqual(provenance['version'], VERSION)
        self.assertIn('ThirdParty/Vital/src/reference.cpp', provenance['source_inputs_sha256'])
        for name, checksum in provenance['packaged_binaries_sha256'].items():
            self.assertEqual(refresh.sha256(self.root / name), checksum)

    def test_nonzero_exit_is_rejected(self):
        self.put('build.exit', '1')
        with self.assertRaisesRegex(RuntimeError, 'not completed successfully'):
            self.run_refresh()

    def test_unwrapped_build_log_is_rejected(self):
        self.put('build.log', 'Release link complete')
        with self.assertRaisesRegex(RuntimeError, 'not a clean resource-wrapped build'):
            self.run_refresh()

    def test_interrupted_log_is_rejected(self):
        self.put('build.log', 'HalfCpu: 6/12\n^C')
        with self.assertRaisesRegex(RuntimeError, 'not a clean resource-wrapped build'):
            self.run_refresh()

    def test_debug_tree_is_rejected(self):
        self.put('build/CMakeCache.txt', 'CMAKE_BUILD_TYPE:STRING=Debug')
        with self.assertRaisesRegex(RuntimeError, 'Expected a Release build tree'):
            self.run_refresh()

    def test_old_binary_preserves_delivery(self):
        self.prepare_build()
        with self.assertRaisesRegex(RuntimeError, 'Binary version mismatch'):
            self.run_refresh(version='1.11.3')
        self.assertTrue((self.root / 'Windows-x64/CLAP/Vocal Stack.clap').exists())
        self.assertFalse((self.root / 'Windows-x64/BUILD-PROVENANCE.json').exists())

    def test_staging_failure_preserves_delivery(self):
        self.prepare_build()
        with patch.object(refresh.shutil, 'copytree', side_effect=OSError('staging failed')):
            with self.assertRaisesRegex(OSError, 'staging failed'):
                self.run_refresh()
        self.assertTrue((self.root / 'Windows-x64/CLAP/Vocal Stack.clap').exists())
        self.assertFalse((self.root / ('Windows-x64/' + CLAP)).exists())


if __name__ == '__main__':
    unittest.main()
