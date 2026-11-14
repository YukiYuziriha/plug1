"""Encode actual 1.11.0 component-capture sequences. Run through HalfCPU.
No baseline animation, synthetic motion, network, or package installation.
"""
import json
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
frames_directory = root / "after"
ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
if not ffmpeg or not ffprobe:
    raise SystemExit("Existing ffmpeg and ffprobe required; nothing installed")
manifest = []
for first in sorted(frames_directory.glob("*-000.png")):
    stem = first.name.removesuffix("-000.png")
    frames = sorted(frames_directory.glob(stem + "-[0-9][0-9][0-9].png"))
    expected_count = 120 if stem.startswith("spectral-morph-") else 148
    if len(frames) != expected_count:
        raise RuntimeError(f"Incomplete real frame sequence {stem}: {len(frames)}")
    for i, image in enumerate(frames):
        if image.name != f"{stem}-{i:03d}.png":
            raise RuntimeError(f"Gap in {stem}: {image.name}")
    palette = root / f"{stem}-palette.png"
    gif = root / f"{stem}.gif"
    common = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-threads", "2"]
    subprocess.run(common + ["-framerate", "25", "-i", str(frames_directory / (stem + "-%03d.png")),
                            "-vf", "palettegen=max_colors=64:stats_mode=diff", "-frames:v", "1", str(palette)], check=True)
    subprocess.run(common + ["-framerate", "25", "-i", str(frames_directory / (stem + "-%03d.png")),
                            "-i", str(palette), "-lavfi", "paletteuse=dither=none:diff_mode=rectangle",
                            "-loop", "0", str(gif)], check=True)
    data = json.loads(subprocess.check_output([ffprobe, "-v", "error", "-count_frames", "-show_entries",
        "stream=nb_read_frames,duration,width,height", "-of", "json", str(gif)]))["streams"][0]
    if int(data["nb_read_frames"]) != expected_count or abs(float(data["duration"]) - expected_count / 25) > .001:
        raise RuntimeError(f"Encoded timing mismatch: {gif}: {data}")
    size = gif.stat().st_size
    if size > 8 * 1048576:
        raise RuntimeError(f"Review GIF exceeds 8 MiB: {gif}: {size}")
    manifest.append({"file": gif.name, "bytes": size, **data})
    print(f"{gif.name}: {data['nb_read_frames']} frames, {data['duration']} s, {size / 1048576:.2f} MiB", flush=True)
expected = 33 if any(frames_directory.glob('*-stack-row-000.png')) else 24
if len(manifest) != expected:
    raise RuntimeError(f"Expected {expected} complete graph/panel/rack GIFs, got {len(manifest)}")
(root / "gif-manifest.json").write_text(json.dumps(manifest, indent=2))
print(f"Verified {len(manifest)} actual-motion GIFs; native component snapshots, not screen recordings or DAW audio", flush=True)
