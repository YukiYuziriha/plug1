"""Preview only; run via Invoke-HalfCpu.ps1. Existing ffmpeg/ffprobe, no installs.
Native fixtures enumerate/place windows; output is offscreen component snapshots,
not desktop recordings or desktop FPS. Generated PCM uses the real processor.
"""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / 'source/build/approval-1.11.1-compressor'
FRAMES = ROOT / 'frames'
EXE = PROJECT / 'source/build/VocalStackEditorTests_artefacts/Release/VocalStackEditorTests.exe'
FRAMES.mkdir(parents=True, exist_ok=True)
ffmpeg, ffprobe = shutil.which('ffmpeg'), shutil.which('ffprobe')
if not ffmpeg or not ffprobe:
    raise SystemExit('Existing ffmpeg/ffprobe required; nothing installed')


def ff(*arguments):
    subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-threads', '2',
                    '-filter_threads', '2', '-filter_complex_threads', '2', *map(str, arguments)], check=True, timeout=90)


manifest = []
for theme, name in enumerate(['device', 'neon', 'spectral']):
    log = ROOT / f'{name}.log'
    with log.open('w') as stream:
        result = subprocess.run([str(EXE), '--render-update-gallery', 'compressor', str(theme)],
                                cwd=FRAMES, stdout=stream, stderr=subprocess.STDOUT, timeout=150)
    print(log.read_text(), flush=True)
    result.check_returncode()
    for view in ['inspector', 'rack']:
        stem = f'compressor-{view}-{name}'
        metadata = json.loads((FRAMES / f'{stem}.json').read_text())
        with (FRAMES / f'{stem}-measurements.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        sequence = sorted(FRAMES.glob(f'{stem}-[0-9][0-9][0-9].png'))
        assert len(sequence) == 100
        unique = len({hashlib.sha256(p.read_bytes()).hexdigest() for p in sequence})
        assert unique > 25 and metadata['changed_frames'] > 25
        assert max(float(r['input_rms']) for r in rows) > .1
        assert max(float(r['wet_rms']) for r in rows) > .001
        frames, palette, output = FRAMES / f'{stem}-%03d.png', ROOT / f'{stem}-palette.png', ROOT / f'{stem}.gif'
        ff('-framerate', 25, '-i', frames, '-vf', 'palettegen=max_colors=96:stats_mode=diff', '-frames:v', 1, palette)
        ff('-framerate', 25, '-i', frames, '-i', palette, '-lavfi', 'paletteuse=dither=none:diff_mode=rectangle', '-loop', 0, output)
        info = json.loads(subprocess.check_output([ffprobe, '-v', 'error', '-count_frames', '-show_entries',
            'stream=width,height,nb_read_frames,duration', '-of', 'json', str(output)]))['streams'][0]
        assert int(info['nb_read_frames']) == 100 and abs(float(info['duration'])-4) < .001
        assert output.stat().st_size < 8*1048576
        for suffix in ['context', 'plot']:
            shutil.copyfile(FRAMES / f'{stem}-{suffix}.png', ROOT / f'{stem}-{suffix}.png')
        shutil.copyfile(sequence[45], ROOT / f'{stem}-panel.png')
        metadata.update(info)
        metadata.update(unique_frames=unique, bytes=output.stat().st_size, file=output.name)
        manifest.append(metadata)
        print(f'{stem}: {unique}/100 unique, {output.stat().st_size} bytes', flush=True)
(ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2))
print(f'Preview: {ROOT}; six actual panels, six close plots, six full contexts, six four-second GIFs')
