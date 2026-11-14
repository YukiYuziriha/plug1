"""Encode real processor-rendered PNGs; run via tools/Invoke-HalfCpu.ps1.

Requires an existing ffmpeg on PATH. No network or optional Python packages.
Baseline images are actual 1.10.0 captures, not reconstructed model renders.
Different stimuli/settings mean comparisons are visual, not numerical A/Bs.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
args = parser.parse_args()
root = args.directory.resolve()
after, before = root / "after", root / "before"
ffmpeg = shutil.which("ffmpeg")
if not ffmpeg:
    raise SystemExit("ffmpeg not found; no encoder installed automatically")


def run(*arguments):
    subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-threads", "2", *map(str, arguments)], check=True)


for effect in ("reverb", "convolution", "saturator"):
    for theme in ("device", "neon", "spectral"):
        stem = f"{effect}-{theme}"
        for view in ("graph", "panel"):
            frames = after / f"{stem}-{view}-%03d.png"
            palette = root / f"{stem}-{view}-palette.png"
            output = root / f"{stem}-{view}.gif"
            run("-framerate", "25", "-i", frames, "-vf", "palettegen=max_colors=96:stats_mode=diff", "-frames:v", "1", palette)
            run("-framerate", "25", "-i", frames, "-i", palette, "-lavfi", "paletteuse=dither=none:diff_mode=rectangle", "-loop", "0", output)
            print(f"{output.name}: {output.stat().st_size / 1048576:.2f} MiB", flush=True)
        if effect == "reverb":
            old = before / f"update-reverb-{theme}.png"
            crop = "crop=289:236:598:254,scale=324:254:flags=neighbor"
        else:
            old = before / f"{effect}-live-{theme}-frame-06.png"
            crop = "crop=324:254:8:8"
        old_graph = root / f"{stem}-before-graph.png"
        run("-i", old, "-vf", crop, "-frames:v", "1", old_graph)
        # The black top margin is outside either production graph. Labels make
        # model-only baseline vs measured field explicit without covering pixels.
        label = "drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':fontsize=14:fontcolor=white:y=5"
        filters = f"[0:v]pad=iw:ih+26:0:26:black,{label}:text='1.10.0 BEFORE (different stimulus)'[a];[1:v]pad=iw:ih+26:0:26:black,{label}:text='1.10.1 MEASURED FIELD'[b];[a][b]hstack"
        run("-i", old_graph, "-i", after / f"{stem}-graph-073.png", "-filter_complex", filters, "-frames:v", "1", root / f"{stem}-graph-before-after.png")
        old_editor = before / (f"update-reverb-{theme}.png" if effect == "reverb" else f"{effect}-live-{theme}-full-frame-06.png")
        filters = f"[0:v]scale=600:705:force_original_aspect_ratio=decrease,pad=600:731:0:26:black,{label}:text='1.10.0 BEFORE'[a];[1:v]scale=600:705:force_original_aspect_ratio=decrease,pad=600:731:0:26:black,{label}:text='1.10.1 AFTER - actual wet tail or drive'[b];[a][b]hstack"
        run("-i", old_editor, "-i", after / f"{stem}-editor-native-75.png", "-filter_complex", filters, "-frames:v", "1", root / f"{stem}-editor-before-after.png")
        # Baseline remains a still; only the right-hand side is the actual new
        # 25 fps sequence. No interpolated or fabricated baseline animation.
        filters = f"[0:v]pad=iw:ih+26:0:26:black,{label}:text='1.10.0 BEFORE - still'[a];[1:v]pad=iw:ih+26:0:26:black,{label}:text='1.10.1 REAL PCM - 25 fps'[b];[a][b]hstack=shortest=1,trim=duration=5.92,split[s][p];[p]palettegen=max_colors=96[pal];[s][pal]paletteuse=dither=none:diff_mode=rectangle"
        run("-loop", "1", "-framerate", "25", "-t", "5.92", "-i", old_graph, "-i", root / f"{stem}-graph.gif", "-filter_complex", filters, "-t", "5.92", "-loop", "0", root / f"{stem}-graph-before-after.gif")

probe = shutil.which("ffprobe")
if not probe:
    raise SystemExit("ffprobe required to verify encoded frame count and duration")
for gif in sorted(root.glob("*.gif")):
    data = json.loads(subprocess.check_output([probe, "-v", "error", "-count_frames", "-show_entries", "stream=nb_read_frames,duration", "-of", "json", str(gif)]))
    stream = data["streams"][0]
    if int(stream["nb_read_frames"]) != 148 or abs(float(stream["duration"]) - 5.92) > .001:
        raise RuntimeError(f"Wrong encoded timing/frame count: {gif.name}: {stream}")
    if gif.stat().st_size > 8 * 1048576:
        raise RuntimeError(f"GIF exceeded 8 MiB review budget: {gif.name}")
print("Verified all 27 GIFs: 148 frames, 5.92 s, 25 fps, each below 8 MiB", flush=True)
