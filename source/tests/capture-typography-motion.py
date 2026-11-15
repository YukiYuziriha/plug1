"""Run via Invoke-HalfCpu.ps1. No installs/network/audio assets. Offscreen snapshots
of production components; matched 25 fps timelines, 1x playback, not desktop FPS.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / 'source/build/approval-1.11.1-typography-motion'
FRAMES = ROOT / 'frames'
MEDIA = PROJECT / 'docs/showcase'
EXE = PROJECT / 'source/build/VocalStackEditorTests_artefacts/Release/VocalStackEditorTests.exe'
FRAMES.mkdir(parents=True, exist_ok=True)
ffmpeg, ffprobe = shutil.which('ffmpeg'), shutil.which('ffprobe')
if not ffmpeg or not ffprobe:
    raise SystemExit('Existing ffmpeg/ffprobe required; nothing installed')

def ff(*args):
    subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-threads', '2',
                    '-filter_threads', '2', '-filter_complex_threads', '2', *map(str, args)], check=True, timeout=120)

if '--encode-only' not in sys.argv:
    with (ROOT / 'capture.log').open('w') as log:
        result = subprocess.run([str(EXE), '--render-typography-motion'], cwd=FRAMES,
                                stdout=log, stderr=subprocess.STDOUT, timeout=240)
    print((ROOT / 'capture.log').read_text(), flush=True)
    result.check_returncode()

# Three matched, same-scale production Stack panels, each with the real header
# Motion control and a nearest-neighbour 2x texture crop. Never invent glints.
# Drawtext is editorial labeling, not a replacement for the captured UI control.
filters = []
for i, percent in enumerate([0, 3, 100]):
    filters.append(f'[{i}:v]split=3[a{i}][b{i}][c{i}]')
    filters.append(f'[a{i}]crop=320:50:220:10,scale=470:74:flags=neighbor[h{i}]')
    filters.append(f'[b{i}]scale=470:-1:flags=neighbor[overview{i}]')
    filters.append(f'[c{i}]crop=224:120:500:550,scale=448:240:flags=neighbor[t{i}]')
    filters.append(f'[h{i}][overview{i}]vstack=inputs=2[context{i}]')
    filters.append(f'[t{i}]pad=470:268:11:24:color=0x11151c[texture{i}]')
    filters.append(f'[context{i}][texture{i}]vstack=inputs=2[col{i}]')
filters.append('[col0][col1][col2]hstack=inputs=3,pad=iw:ih+66:0:66:color=0x11151c,'
               "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='1.11.1 APPROVAL PREVIEW | 10 s sampled timeline | 25 fps / 1x playback | silence':x=14:y=10:fontsize=19:fontcolor=white,"
               "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Motion 0 - frozen':x=14:y=38:fontsize=19:fontcolor=white,"
                              "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Motion 3 - original speed':x=484:y=38:fontsize=19:fontcolor=white,"
                              "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Motion 100 - 33.333x':x=954:y=38:fontsize=19:fontcolor=white[out]")
inputs = []
for percent in [0, 3, 100]:
    sequence = sorted(FRAMES.glob(f'motion-neon-{percent}-[0-9][0-9][0-9].png'))
    assert len(sequence) == 250
    unique = len({hashlib.sha256(f.read_bytes()).hexdigest() for f in sequence})
    assert unique == 1 if percent == 0 else unique > 25
    print(f'Motion {percent}: {unique}/250 distinct production frames', flush=True)
    inputs += ['-framerate', '25', '-i', str(FRAMES / f'motion-neon-{percent}-%03d.png')]
combined = FRAMES / 'comparison-%03d.png'
ff(*inputs, '-filter_complex', ';'.join(filters), '-map', '[out]', '-frames:v', 250, combined)
palette = ROOT / 'motion-palette.png'
ff('-framerate', 25, '-i', combined, '-vf', 'palettegen=max_colors=96:stats_mode=diff', '-frames:v', 1, palette)
output = MEDIA / 'motion.gif'
ff('-framerate', 25, '-i', combined, '-i', palette, '-lavfi', 'paletteuse=dither=none:diff_mode=rectangle', '-loop', 0, output)
info = json.loads(subprocess.check_output([ffprobe, '-v', 'error', '-count_frames', '-show_entries',
    'stream=width,height,nb_read_frames,duration', '-of', 'json', str(output)]))['streams'][0]
assert int(info['nb_read_frames']) == 250 and abs(float(info['duration']) - 10) < .001
assert output.stat().st_size < 3 * 1048576
# Baselines must be the same actual decorative phase, excluding the header value.
for percent in [0, 3, 100]:
    ff('-i', FRAMES / f'motion-neon-{percent}-000.png', '-vf', 'crop=224:120:500:550', '-frames:v', 1, ROOT / f'baseline-{percent}.png')
assert len({hashlib.sha256((ROOT / f'baseline-{p}.png').read_bytes()).hexdigest() for p in [0, 3, 100]}) == 1
for theme in ['device', 'neon', 'spectral']:
    shutil.copyfile(FRAMES / f'motion-{theme}-3-000.png', ROOT / f'motion-{theme}-context.png')
# Matched native-size old/new compressor typography; no artificial sharpening.
before = PROJECT / 'source/build/approval-1.11.1-typography-before/compressor-inspector-neon-plot.png'
after = PROJECT / 'source/build/approval-1.11.1-compressor/compressor-inspector-neon-plot.png'
if before.exists() and after.exists():
    ff('-i', before, '-i', after, '-filter_complex', '[0:v][1:v]hstack=inputs=2', '-frames:v', 1, ROOT / 'typography-before-after.png')
    shutil.copyfile(ROOT / 'typography-before-after.png', MEDIA / 'typography-preview-1.11.1.png')
shutil.copyfile(PROJECT / 'source/build/approval-1.11.1-compressor/compressor-rack-neon.gif', MEDIA / 'compressor-preview-1.11.1.gif')
info.update(bytes=output.stat().st_size, version='1.11.1 approval preview',
            kind='Offscreen production snapshots; deterministic production clock sampled at 25 fps; 1x playback, not native FPS',
            motion=[0, 3, 100], seconds=10, audio='silence; no injected telemetry', matched_baseline=True,
            full_context='source/build/approval-1.11.1-typography-motion/motion-<theme>-context.png')
(ROOT / 'manifest.json').write_text(json.dumps(info, indent=2))
print(json.dumps(info, indent=2), flush=True)
