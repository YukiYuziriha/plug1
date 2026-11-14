"""Run production UI audit fixtures sequentially; invoke via Invoke-HalfCpu.ps1.

No dependencies installed. Requires existing ffmpeg/ffprobe on PATH for encode.
PNG sequences are labelled component snapshots, not desktop/FPS recordings.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / 'source/build/approval-1.11.0-node-gallery'
FRAMES = ROOT / 'frames'
EXE = PROJECT / 'source/build/VocalStackEditorTests_artefacts/Release/VocalStackEditorTests.exe'
SLUGS = ['chorale', 'waveshaper', 'eq8', 'multiband', 'delay', 'reverb',
         'compressor', 'bitcrusher', 'granular', 'spectral-morph', 'convolution',
         'saturator', 'gain', 'stereo', 'gate', 'xp-filter', 'synth-filter',
         'main-chorale', 'lead', *[f'voice-{i}' for i in range(1, 9)],
         'sidechain', 'dry-wet', 'splitter', 'main-mix', 'output']
parser = argparse.ArgumentParser(__doc__)
parser.add_argument('mode', choices=['capture', 'encode'])
parser.add_argument('--start', type=int, default=0)
parser.add_argument('--end', type=int, default=len(SLUGS))
args = parser.parse_args()
if not 0 <= args.start < args.end <= len(SLUGS):
    parser.error('Require 0 <= start < end <= inventory length')
FRAMES.mkdir(parents=True, exist_ok=True)


def execute(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)


if args.mode == 'capture':
    for slug in SLUGS[args.start:args.end]:
        with (ROOT / f'{slug}.log').open('w') as stream:
            result = subprocess.run([str(EXE), '--render-node-gallery', slug], cwd=FRAMES,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=110)
        print((ROOT / f'{slug}.log').read_text(), flush=True)
        if result.returncode:
            raise SystemExit(f'{slug} failed: {result.returncode}')
else:
    ffmpeg, probe = shutil.which('ffmpeg'), shutil.which('ffprobe')
    if not ffmpeg or not probe:
        raise SystemExit('Existing ffmpeg and ffprobe required; nothing installed')

    def ff(*arguments):
        execute([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-threads', '2',
                 '-filter_threads', '2', '-filter_complex_threads', '2', *map(str, arguments)])

    def verify(path):
        info = json.loads(subprocess.check_output([probe, '-v', 'error', '-count_frames',
            '-show_entries', 'stream=width,height,nb_read_frames,duration', '-of', 'json', str(path)]))['streams'][0]
        assert int(info['nb_read_frames']) == 100, (path, info)
        assert abs(float(info['duration']) - 4) < .001, (path, info)
        assert path.stat().st_size <= 8 * 1048576, (path, path.stat().st_size)
        pixels = subprocess.check_output([ffmpeg, '-v', 'error', '-threads', '2', '-i', str(path),
            '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'])
        assert len(set(pixels)) > 16 and sum(pixels)/len(pixels) > 2, f'Blank/black image: {path}'
        return {**info, 'bytes': path.stat().st_size}

    manifest = []
    for slug in SLUGS[args.start:args.end]:
        for view in ['inspector', 'rack']:
            stem = f'{slug}-{view}'
            metadata = FRAMES / f'{stem}.json'
            if not metadata.exists():
                if view == 'rack' and slug in ['multiband', 'lead', *[f'voice-{i}' for i in range(1, 9)],
                                              'sidechain', 'dry-wet', 'splitter', 'main-mix', 'output']:
                    continue
                raise RuntimeError(f'Missing required view: {metadata}')
            sequence = list(FRAMES.glob(f'{stem}-[0-9][0-9][0-9].png'))
            assert len(sequence) == 100, (stem, len(sequence))
            frames = FRAMES / f'{stem}-%03d.png'
            palette, output = ROOT / f'{stem}-palette.png', ROOT / f'{stem}.gif'
            ff('-framerate', 25, '-i', frames, '-vf', 'palettegen=max_colors=64:stats_mode=diff', '-frames:v', 1, palette)
            ff('-framerate', 25, '-i', frames, '-i', palette, '-lavfi',
               'paletteuse=dither=none:diff_mode=rectangle', '-loop', 0, output)
            item = json.loads(metadata.read_text())
            item.update(verify(output))
            item['file'] = output.name
            item['unique_png_frames'] = len({hashlib.sha256(p.read_bytes()).hexdigest() for p in sequence})
            with (FRAMES / f'{stem}-measurements.csv').open() as stream:
                measurements = list(csv.DictReader(stream))
            item['max_input_rms'] = max(float(r['input_rms']) for r in measurements)
            item['max_output_rms'] = max(float(r['output_rms']) for r in measurements)
            assert item['max_input_rms'] > 0 and item['max_output_rms'] > 0
            static_slugs = {'chorale', 'main-chorale', 'bitcrusher', 'granular', *SLUGS[18:]}
            if slug in static_slugs:
                assert item['changed_frames'] == 0 and item['unique_png_frames'] == 1, stem
            else:
                assert item['changed_frames'] > 0 and item['unique_png_frames'] > 1, stem
            manifest.append(item)
            print(f"{stem}: unique={item['unique_png_frames']}/100, {item['bytes']/1048576:.2f} MiB", flush=True)
    # Groups keep actual panel aspect ratios, with native-sized individual clips alongside.
    if args.start == 0 and args.end == len(SLUGS):
        assert len(manifest) == 49, len(manifest)
        groups = [('creative-a', SLUGS[4:8]), ('creative-b', SLUGS[8:12]),
                  ('tone', SLUGS[:4]), ('utility', SLUGS[12:17]),
                  ('sources-a', SLUGS[18:23]), ('sources-b', SLUGS[23:28]),
                  ('routing', ['main-chorale', *SLUGS[28:]])]
        sheets = []
        # Up to three rows: actual inspector left and supported rack right.
        for group, slugs in groups:
            for part in range(0, len(slugs), 3):
                selected = slugs[part:part+3]
                command, nodes = [], []
                filters = []
                for slug in selected:
                    for view in ['inspector', 'rack']:
                        source = ROOT / f'{slug}-{view}.gif'
                        if not source.exists():
                            continue
                        idx = len(nodes)
                        command += ['-i', str(source)]
                        # Inspector 400px wide and rack 600px wide remain legible in paired columns.
                        width, height = (400, 780) if view == 'inspector' else (600, 780)
                        filters.append(f'[{idx}:v]scale={width}:-1:flags=neighbor,pad={width}:{height}:0:0:color=0x10121a[v{idx}]')
                        nodes.append((slug, view))
                labels = ''.join(f'[v{i}]' for i in range(len(nodes)))
                # Pairs appear side-by-side, successive nodes stacked vertically.
                positions, x, y = [], 0, 0
                for i, (slug, view) in enumerate(nodes):
                    if i and slug != nodes[i-1][0]:
                        y += 780
                        x = 0
                    positions.append(f'{x}_{y}')
                    x += 400 if view == 'inspector' else 600
                if len(nodes) == 1:
                    filters.append('[v0]split[s][p]')
                else:
                    filters.append(f'{labels}xstack=inputs={len(nodes)}:layout={"|".join(positions)}:fill=0x10121a,split[s][p]')
                filters += ['[p]palettegen=max_colors=64[pal]', '[s][pal]paletteuse=dither=none:diff_mode=rectangle']
                output = ROOT / f'gallery-{group}-{part//3+1}.gif'
                ff(*command, '-filter_complex', ';'.join(filters), '-t', 4, '-loop', 0, output)
                sheets.append({'file': output.name, 'nodes': selected, **verify(output)})
        (ROOT / 'gif-manifest.json').write_text(json.dumps({'clips': manifest, 'sheets': sheets}, indent=2))
        lines = ['# Preview 1.11.0 — complete Neon node visual audit', '',
            '32 node/slot entries; 49 native-sized clips; 100 frames / 4 seconds / 25 fps each.',
            'Actual production UI and generated PCM through the processor. No injected visual telemetry.',
            'Component snapshots, **not desktop recordings or desktop FPS tests**. Motion is 0.',
            'Static clips are intentionally retained; do not infer audio reactivity from animated GIF format.', '',
            '## Animated contact sheets', '']
        for sheet in sheets:
            lines += [f"### {sheet['file']}", f"![{', '.join(sheet['nodes'])}]({sheet['file']})", '']
        lines += ['## Complete view inventory', '', '| Node | Right inspector | Stack FX | Signal/model and pixel activity |', '| --- | --- | --- | --- |']
        for slug in SLUGS:
            clips = [m for m in manifest if m['slug'] == slug]
            inspector = next(m for m in clips if m['view'] == 'inspector')
            rack = next((m for m in clips if m['view'] == 'rack'), None)
            rack_link = f"[GIF]({rack['file']})" if rack else '**NOT SUPPORTED** by production rack'
            lines.append(f"| {inspector['name']} | [GIF]({inspector['file']}) | {rack_link} | {inspector['model']}; unique frames I={inspector['unique_png_frames']}/100" + (f", R={rack['unique_png_frames']}/100" if rack else '') + ' |')
        lines += ['', '## Model choices exposed by production controls', '',
                  'One fixed representative configuration per node, not a claim of a clip for every model. All currently mounted selector options below come directly from production ComboBoxes. Hidden/scroll-later options require the source inventory in the audit document.', '']
        for item in manifest:
            if item['view'] != 'inspector':
                continue
            lines += [f"### {item['name']}"]
            for choice in item['model_choices']:
                lines.append(f"- `{choice['parameter'] or 'Morph mode / FFT selector'}` — selected **{choice['selected']}**; options: " + ', '.join(choice['choices']))
            if not item['model_choices']:
                lines.append('- No visible model selector; see source inventory for non-selector modes.')
        (ROOT / 'index.md').write_text('\n'.join(lines), encoding='utf-8')
        print(f'COMPLETE: {len(manifest)} clips, {len(sheets)} animated contact sheets; all below 8 MiB', flush=True)
