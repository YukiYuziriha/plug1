"""Verify HEAVEN release ZIPs, faithful build-source hashes and licenses; no build."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def members(archive, prefix):
    names = [item.filename for item in archive.infolist() if not item.is_dir()]
    if len(names) != len(set(names)):
        raise RuntimeError('Duplicate ZIP members')
    for name in names:
        if not name.startswith(prefix) or '\\' in name or '..' in PurePosixPath(name).parts:
            raise RuntimeError(f'Unsafe or unexpected ZIP member: {name}')
    if archive.testzip() is not None:
        raise RuntimeError('ZIP integrity failure')
    return {name.removeprefix(prefix) for name in names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--version', default='1.12.2')
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', args.version):
        parser.error('Expected major.minor.patch version')
    expected = {}
    for line in (args.directory / 'SHA256SUMS.txt').read_text(encoding='ascii').splitlines():
        checksum, name = line.split('  ', 1)
        if not name or '/' in name or '\\' in name or ':' in name or name in ('.', '..'):
            raise RuntimeError('Unsafe checksum filename')
        if name in expected or not re.fullmatch(r'[0-9a-f]{64}', checksum):
            raise RuntimeError('Duplicate filename or invalid checksum')
        with (args.directory / name).open('rb') as stream:
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
        prefix = f'HEAVEN-{args.version}/'
        source_names = members(source, prefix)
        windows_names = members(windows, prefix)
        manifest = json.loads(source.read(prefix + 'SOURCE-MANIFEST.json'))
        if manifest['source_mode'] != 'working-tree-snapshot' or manifest['binary_source_commit'] is not None:
            raise RuntimeError('Expected explicitly uncommitted source provenance')
        if source_names != set(manifest['files_sha256']) | {'SOURCE-MANIFEST.json'}:
            raise RuntimeError('Source manifest inventory mismatch')
        for name, checksum in manifest['files_sha256'].items():
            if Path(name).suffix.lower() in ('.clap', '.vst3', '.dll', '.exe', '.obj', '.o', '.lib', '.a', '.pdb', '.gitbundle'):
                raise RuntimeError(f'Compiled output in source archive: {name}')
            if digest(source.read(prefix + name)) != checksum:
                raise RuntimeError(f'Source manifest mismatch: {name}')
        cmake = source.read(prefix + 'source/CMakeLists.txt').decode('utf-8')
        if not re.search(r'project\(VocalStack VERSION ' + re.escape(args.version) + r'(?:\s|\))', cmake):
            raise RuntimeError('Source project version mismatch')
        if 'PRODUCT_NAME "HEAVEN by lil grass block"' not in cmake:
            raise RuntimeError('Source product name mismatch')
        provenance_bytes = windows.read(prefix + 'Windows-x64/BUILD-PROVENANCE.json')
        if source.read(prefix + 'Windows-x64/BUILD-PROVENANCE.json') != provenance_bytes:
            raise RuntimeError('Source and Windows build provenance differ')
        provenance = json.loads(provenance_bytes)
        if (provenance['version'] != args.version or provenance['build_exit_code'] != 0
                or provenance.get('source_mode') != 'uncommitted-working-tree'):
            raise RuntimeError('Version or build evidence mismatch')
        inputs = {name for name in source_names if name == 'source/CMakeLists.txt'
                  or name.startswith(('source/src/', 'source/assets/', 'source/cmake/', 'ThirdParty/'))}
        if inputs != set(provenance['source_inputs_sha256']):
            raise RuntimeError('Build-source inventory mismatch')
        for name, checksum in provenance['source_inputs_sha256'].items():
            if digest(source.read(prefix + name)) != checksum:
                raise RuntimeError(f'Bundled source differs from observed build: {name}')
        binaries = {'Windows-x64/CLAP/HEAVEN by lil grass block.clap',
                    'Windows-x64/VST3/HEAVEN by lil grass block.vst3/Contents/x86_64-win/HEAVEN by lil grass block.vst3'}
        actual_binaries = {name for name in windows_names if Path(name).suffix.lower() in ('.clap', '.vst3', '.dll', '.exe')}
        if actual_binaries != binaries or set(provenance['packaged_binaries_sha256']) != binaries:
            raise RuntimeError('Unexpected or missing packaged binaries')
        for name, checksum in provenance['packaged_binaries_sha256'].items():
            if digest(windows.read(prefix + name)) != checksum:
                raise RuntimeError(f'Bundled binary differs from observed build: {name}')
        notices = {name for name in source_names if name.startswith('ThirdParty/')}
        delivered = {name.removeprefix('Windows-x64/') for name in windows_names
                     if name.startswith('Windows-x64/ThirdParty/')}
        if notices != delivered:
            raise RuntimeError('Third-party notice inventory mismatch')
        for name in notices:
            if source.read(prefix + name) != windows.read(prefix + 'Windows-x64/' + name):
                raise RuntimeError(f'Third-party notice mismatch: {name}')
        for name, packaged in (('LICENSE', 'LICENSE.txt'), ('COPYING.AGPLv3', 'COPYING.AGPLv3'),
                               ('ThirdParty/PixelifySans-OFL.txt', 'PixelifySans-OFL.txt'),
                               ('Windows-x64/SOURCE.md', 'SOURCE.md')):
            if source.read(prefix + name) != windows.read(prefix + 'Windows-x64/' + packaged):
                raise RuntimeError(f'License/source guidance mismatch: {name}')
        declarations = dict(re.findall(r'FetchContent_Declare\(([^\s]+)(.*?)\)', cmake, re.S))
        if not declarations or set(declarations) != set(manifest['dependencies']):
            raise RuntimeError('Missing pinned external dependency sources')
        for name, declaration in declarations.items():
            dependency = manifest['dependencies'][name]
            for token, field in (('GIT_REPOSITORY', 'url'), ('GIT_TAG', 'commit'), ('URL', 'url'), ('URL_HASH SHA256=', 'sha256')):
                match = re.search(r'\b' + token + (r'([0-9a-f]{64})' if field == 'sha256' else r'\s+(\S+)'), declaration)
                if match and dependency.get(field) != match.group(1):
                    raise RuntimeError(f'Dependency pin mismatch: {name}')
            if not any(path.startswith(f'external/{name}/') for path in source_names):
                raise RuntimeError(f'Missing dependency source: {name}')
            if dependency.get('submodules_enabled'):
                for gitlink in dependency.get('submodule_gitlinks', []):
                    _, submodule = gitlink.split('\t', 1)
                    if not any(path.startswith(f'external/{name}/{submodule}/') for path in source_names):
                        raise RuntimeError(f'Missing dependency submodule source: {name}/{submodule}')
        print(f"PASS: {len(manifest['files_sha256'])} source-file hashes, "
              f"{len(binaries)} binary hashes, dependency pins, notices/licenses and ZIP integrity")
        print(json.dumps(expected, indent=2))


if __name__ == '__main__':
    main()
