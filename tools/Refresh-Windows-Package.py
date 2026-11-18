"""Refresh only repository delivery files after an observed successful Release build."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PRODUCT_NAME = 'HEAVEN by lil grass block'


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def file_version(path):
    # Read PE resources without loading or executing the plugin.
    literal = str(path.resolve()).replace("'", "''")
    return subprocess.check_output([
        'powershell.exe', '-NoProfile', '-Command',
        f"(Get-Item -LiteralPath '{literal}').VersionInfo.FileVersion"], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--build-log', type=Path, required=True)
    parser.add_argument('--build-exit-file', type=Path, required=True,
                        help='Build runner exit-code record; must contain 0')
    parser.add_argument('--version', default='1.12.0')
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', args.version):
        parser.error('Expected major.minor.patch version')
    if args.build_exit_file.read_text().strip() != '0':
        raise RuntimeError('Release build has not completed successfully')
    log = args.build_log.read_text(encoding='utf-8', errors='replace')
    if 'HalfCpu:' not in log or re.search(r'FAILED:|ninja: build stopped|fatal error|\^C', log):
        raise RuntimeError('Build log is not a clean resource-wrapped build')
    cache = (args.build_directory / 'CMakeCache.txt').read_text(encoding='utf-8')
    if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache:
        raise RuntimeError('Expected a Release build tree')
    cmake = (ROOT / 'source/CMakeLists.txt').read_text(encoding='utf-8')
    if f'project(VocalStack VERSION {args.version} ' not in cmake:
        raise RuntimeError('Source project version differs from requested release')
    artifacts = args.build_directory / 'VocalStack_artefacts/Release'
    binaries = {
        f'CLAP/{PRODUCT_NAME}.clap': artifacts / f'CLAP/{PRODUCT_NAME}.clap',
        f'VST3/{PRODUCT_NAME}.vst3/Contents/x86_64-win/{PRODUCT_NAME}.vst3':
            artifacts / f'VST3/{PRODUCT_NAME}.vst3/Contents/x86_64-win/{PRODUCT_NAME}.vst3',
    }
    inputs = [ROOT / 'source/CMakeLists.txt']
    for directory in ('source/src', 'source/assets', 'source/cmake'):
        inputs += [p for p in (ROOT / directory).rglob('*') if p.is_file()]
    newest_input = max(p.stat().st_mtime_ns for p in inputs)
    hashes = {}
    for name, binary in binaries.items():
        if file_version(binary) != args.version:
            raise RuntimeError(f'Binary version mismatch: {binary}')
        if binary.stat().st_mtime_ns < newest_input:
            raise RuntimeError(f'Binary predates source inputs: {binary}')
        hashes['Windows-x64/' + name] = sha256(binary)
    source_hashes = {p.relative_to(ROOT).as_posix(): sha256(p) for p in sorted(inputs)}
    provenance = {
        'version': args.version,
        'source_mode': 'uncommitted-working-tree',
        'base_commit_not_producing_revision': subprocess.check_output(
            ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'observed_utc': datetime.now(timezone.utc).isoformat(),
        'build_directory': args.build_directory.as_posix(),
        'build_log': args.build_log.as_posix(), 'build_log_sha256': sha256(args.build_log),
        'build_exit_code': 0,
        'source_inputs_sha256': source_hashes, 'packaged_binaries_sha256': hashes,
    }
    destination = ROOT / 'Windows-x64'
    # Stage and verify all inputs before touching the delivery tree. Never install.
    with tempfile.TemporaryDirectory(prefix='vocalstack-delivery-') as temporary:
        stage = Path(temporary)
        shutil.copy2(binaries[f'CLAP/{PRODUCT_NAME}.clap'], stage / f'{PRODUCT_NAME}.clap')
        shutil.copytree(artifacts / f'VST3/{PRODUCT_NAME}.vst3', stage / f'{PRODUCT_NAME}.vst3')
        shutil.copytree(ROOT / 'ThirdParty', stage / 'ThirdParty')
        for name, binary in binaries.items():
            staged = (stage / f'{PRODUCT_NAME}.clap' if name.startswith('CLAP/') else
                      stage / name.removeprefix('VST3/'))
            if sha256(staged) != hashes['Windows-x64/' + name] or sha256(binary) != sha256(staged):
                raise RuntimeError('Build output changed during staging')
        if any(sha256(ROOT / p) != h for p, h in source_hashes.items()):
            raise RuntimeError('Source inputs changed during staging')
        (destination / 'CLAP').mkdir(parents=True, exist_ok=True)
        (destination / 'VST3').mkdir(parents=True, exist_ok=True)
        shutil.copy2(stage / f'{PRODUCT_NAME}.clap', destination / f'CLAP/{PRODUCT_NAME}.clap')
        target = destination / f'VST3/{PRODUCT_NAME}.vst3'
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(stage / f'{PRODUCT_NAME}.vst3', target)
        shutil.copytree(stage / 'ThirdParty', destination / 'ThirdParty', dirs_exist_ok=True)
        for source, target_name in (('LICENSE', 'LICENSE.txt'), ('COPYING.AGPLv3', 'COPYING.AGPLv3'),
                                    ('ThirdParty/PixelifySans-OFL.txt', 'PixelifySans-OFL.txt')):
            shutil.copy2(ROOT / source, destination / target_name)
    (destination / 'BUILD-PROVENANCE.json').write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    # Retire only the known old repository deliveries, after the new files and
    # provenance were written successfully. Installed plugins are never touched.
    (destination / 'CLAP/Vocal Stack.clap').unlink(missing_ok=True)
    legacy_vst3 = destination / 'VST3/Vocal Stack.vst3'
    if legacy_vst3.exists():
        shutil.rmtree(legacy_vst3)
    print(json.dumps(hashes, indent=2))
    print('Refreshed repository Windows-x64 only; no installed plugins touched')


if __name__ == '__main__':
    main()
