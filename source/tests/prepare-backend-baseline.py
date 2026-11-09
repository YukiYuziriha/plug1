"""Generate an isolated pre-routing DSP reference; never modifies tracked source."""
from pathlib import Path
import subprocess
import sys

# Pin the pre-routing engine: following HEAD would stop being a regression test
# as soon as the implementation is committed. This revision must exist locally.
reference = "f414c0ca60f183efb32550e77b17c03e554657d5"
root = Path(__file__).resolve().parents[2]
destination = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "source/build/backend-tests/baseline"
destination.mkdir(parents=True, exist_ok=True)
for name in ("VocalEngine.h", "VocalEngine.cpp", "DoublerSettings.h", "Metering.h", "Chorale.h"):
    content = subprocess.check_output(
        ["git", "--no-pager", "show", f"{reference}:source/src/{name}"], cwd=root
    ).decode("utf-8")
    (destination / name).write_text(content.replace("vocalstack", "baseline"), encoding="utf-8")
