"""Verify a working-tree Tuning delivery's ZIPs, source provenance and licenses; no build."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--version', default='1.12.0')
    args = parser.parse_args()
    expected = {}
    for line in (args.directory / 'SHA256SUMS.txt').read_text(encoding='ascii').splitlines():
        checksum, name = line.split('  ', 1)
        if Path(name).name != name:
            raise RuntimeError('Unsafe checksum filename')
        path = args.directory / name
        with path.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != checksum:
            raise RuntimeError(f'Checksum mismatch: {name}')
        expected[name] = actual
    source_name = f'HEAVEN-{args.version}-corresponding-source.zip'
    windows_name = f'HEAVEN-{args.version}-Windows-x64.zip'
    if not {source_name, windows_name} <= expected.keys():
        raise RuntimeError('Missing source or binary ZIP checksum')
    with zipfile.ZipFile(args.directory / source_name) as source, \
         zipfile.ZipFile(args.directory / windows_name) as windows:
        for archive in (source, windows):
            if archive.testzip() is not None:
                raise RuntimeError('ZIP integrity failure')
        source_prefix = f'HEAVEN-{args.version}/'
        windows_prefix = f'HEAVEN-{args.version}/'
        manifest = json.loads(source.read(source_prefix + 'SOURCE-MANIFEST.json'))
        if manifest['source_mode'] != 'working-tree-snapshot' or manifest['binary_source_commit'] is not None:
            raise RuntimeError('Expected explicitly uncommitted source provenance')
        for name, checksum in manifest['files_sha256'].items():
            if digest(source.read(source_prefix + name)) != checksum:
                raise RuntimeError(f'Source manifest mismatch: {name}')
        provenance = json.loads(windows.read(windows_prefix + 'Windows-x64/BUILD-PROVENANCE.json'))
        if provenance['version'] != args.version or provenance['build_exit_code'] != 0:
            raise RuntimeError('Version or build evidence mismatch')
        for name, checksum in provenance['source_inputs_sha256'].items():
            if digest(source.read(source_prefix + name)) != checksum:
                raise RuntimeError(f'Bundled source differs from observed build: {name}')
        for name, checksum in provenance['packaged_binaries_sha256'].items():
            if digest(windows.read(windows_prefix + name)) != checksum:
                raise RuntimeError(f'Bundled binary differs from observed build: {name}')
        for name in ('README.md', 'LICENSE', 'COPYING-autotalent', 'autotalent-0.2-notice.txt',
                     'pitch_detector_talent.cpp', 'pitch_detector_talent.h'):
            relative = 'ThirdParty/MXTune/' + name
            if source.read(source_prefix + relative) != windows.read(windows_prefix + 'Windows-x64/' + relative):
                raise RuntimeError(f'Donor notice/source mismatch: {relative}')
        for name in ('Tuning.md', 'Signalsmith-Stretch.txt', 'Signalsmith-Linear.txt', 'JUCE.md'):
            relative = 'ThirdParty/' + name
            if source.read(source_prefix + relative) != windows.read(windows_prefix + 'Windows-x64/' + relative):
                raise RuntimeError(f'Third-party notice mismatch: {relative}')
        for name, packaged in (('LICENSE', 'LICENSE.txt'), ('COPYING.AGPLv3', 'COPYING.AGPLv3'),
                               ('ThirdParty/PixelifySans-OFL.txt', 'PixelifySans-OFL.txt')):
            if source.read(source_prefix + name) != windows.read(windows_prefix + 'Windows-x64/' + packaged):
                raise RuntimeError(f'License text mismatch: {name}')
        if b'GPL-3.0-or-later' not in source.read(source_prefix + 'source/src/AutomaticTuner.h'):
            raise RuntimeError('Missing tuner adaptation license header')
        if not manifest['dependencies'] or not any(n.startswith(source_prefix + 'external/JUCE/') for n in source.namelist()):
            raise RuntimeError('Missing pinned external dependency sources')
        print(f"PASS: {len(manifest['files_sha256'])} source-file hashes, "
              f"{len(provenance['packaged_binaries_sha256'])} binary hashes, full donor notices/licenses and ZIP integrity")
        print(json.dumps(expected, indent=2))


if __name__ == '__main__':
    main()
