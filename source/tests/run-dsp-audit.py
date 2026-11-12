"""Standalone MSVC audit; never configures or builds the shared CMake tree.

python source/tests/run-dsp-audit.py --measure docs/DSP-before.csv
python source/tests/run-dsp-audit.py --header PATH --measure OUTPUT.csv
python source/tests/run-dsp-audit.py

--header accepts a retained baseline header. All object/executable files and the
header copy live in an automatically removed temporary directory.
"""
import argparse
import pathlib
import shutil

import subprocess
import tempfile
import hashlib

root = pathlib.Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--header', type=pathlib.Path, default=root/'source/src/CreativeEffects.h')
p.add_argument('--measure', type=pathlib.Path)
p.add_argument('--decay', type=pathlib.Path)
p.add_argument('--benchmark', type=pathlib.Path, help='Run the existing informational effects/routing benchmark, current tree only')
p.add_argument('--audio', type=pathlib.Path, help='Optional 48k stereo interleaved float32 transient render with --measure')
p.add_argument('--revision', help='Read the baseline header from this Git revision; requires --measure or --decay')
p.add_argument('--existing', action='store_true', help='Run existing creative DSP regressions too')
p.add_argument('--extended', action='store_true', help='Also compile/run existing standalone tone, utility, waveshaper and Chorale suites')
a = p.parse_args()
if sum(bool(x) for x in (a.measure, a.decay, a.benchmark)) > 1:
    p.error('choose only one of --measure, --decay, --benchmark')
if a.benchmark and a.revision:
    p.error('--benchmark uses the current full routing tree; baseline revisions apply to the reverb probe only')
vswhere = pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe')
vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip()
vcvars = pathlib.Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
if a.revision and not (a.measure or a.decay):
    p.error('--revision requires --measure or --decay (new API regressions target the repaired header)')
prefix = subprocess.check_output(['git', '--no-pager', 'rev-parse', '--show-prefix'], cwd=root, text=True).strip() if a.revision else ''
header = subprocess.check_output(['git', '--no-pager', 'show', a.revision + ':' + prefix + 'source/src/CreativeEffects.h'], cwd=root) if a.revision else a.header.read_bytes()
print('Header SHA256:', hashlib.sha256(header).hexdigest(), flush=True)
with tempfile.TemporaryDirectory(prefix='vocalstack-dsp-') as temp:
    work = pathlib.Path(temp)
    # Isolate the whole header graph, so quoted includes in Routing.h resolve
    # the same CreativeEffects.h as the direct include (pragma once is per file).
    for path in (root/'source/src').glob('*.h'):
        shutil.copyfile(path, work/path.name)
    (work/'CreativeEffects.h').write_bytes(header)
    sources = [root/'source/tests/DspAuditProbe.cpp']
    if not (a.measure or a.decay):
        sources += [root/'source/tests/DspAuditTests.cpp', root/'source/tests/DspAuditRoutingTests.cpp']
    if a.extended:
        sources += [root/'source/tests'/name for name in ['ChoraleTests.cpp', 'ToneEffectsTests.cpp', 'UtilityFiltersTests.cpp', 'WaveshaperExpansionTests.cpp', 'CreativeTailTests.cpp', 'FxMonitorTests.cpp', 'MultibandTests.cpp']]
    if a.existing:
        sources.append(root/'source/tests/CreativeEffectsTests.cpp')
    if a.benchmark:
        sources = [root/'source/tests/EffectsBenchmark.cpp']
    for source in sources:
        # Rewrite only the include in the temporary existing test copy so that
        # --header tests the selected version rather than the working tree.
        text = source.read_text().replace('#include "../src/', '#include "')
        local = work/source.name
        local.write_text(text)
        exe = work/(source.stem+'.exe')
        cmd = work/'compile.cmd'
        cmd.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /O2 /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /I"{work}" /I"{root / 'source/src'}" "{local}" /Fe:"{exe}" /Fo:"{work/source.stem}.obj"\n')
        subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], cwd=work, check=True)
        if (a.measure or a.decay) and source.stem == 'DspAuditProbe':
            output_path = a.measure or a.decay
            args = [str(exe), '--measure' if a.measure else '--decay']
            if a.audio and a.measure:
                args.append(str(a.audio.resolve()))
            with output_path.open('w') as output:
                subprocess.run(args, stdout=output, check=True)
        elif a.benchmark:
            with a.benchmark.open('w') as output:
                subprocess.run([str(exe)], stdout=output, check=True)
        else:
            subprocess.run([str(exe)], check=True)
