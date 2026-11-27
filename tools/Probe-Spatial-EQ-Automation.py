"""Offline Depth step at Pedalboard process-call boundaries (not VST3 queue offsets).

From the research worktree root, with the Measure-Spatial-EQ.py dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'tools/Invoke-HalfCpu.ps1' -Executable python -CommandArguments @('-B','tools/Probe-Spatial-EQ-Automation.py')"
"""

import hashlib
from pathlib import Path
import re
from runpy import run_path


SWEEP = run_path(str(Path(__file__).with_name("Sweep-Spatial-EQ-Controls.py")))
HOST = SWEEP["HOST"]
np, pb, MODULE = (HOST[k] for k in ("np", "pb", "MODULE"))
LOW, HIGH = 0.33, 0.71
BLOCK, ALT_BLOCK = 512, 257
TICK = 32


def identify():
    manifest = (HOST["PLUGIN"] / "Contents" / "Resources" / "moduleinfo.json").read_text(encoding="utf-8")
    match = re.search(r'"Version"\s*:\s*"([^"]+)"', manifest)
    if not match or match[1] != "0.7.0":
        raise RuntimeError(f"Expected Spatial EQ 0.7.0: {MODULE}")
    with MODULE.open("rb") as binary:
        digest = hashlib.file_digest(binary, "sha256").hexdigest()
    print(f"{MODULE}\nVersion {match[1]} | SHA-256 {digest} | Pedalboard {pb.__version__} | NumPy {np.__version__}")


def stimulus(rate):
    t = np.arange(5 * rate // 4) / rate
    left = 0.038 * np.cos(2 * np.pi * 2400 * t) + 0.028 * np.cos(2 * np.pi * 900 * t + 0.1)
    right = 0.025 * np.cos(2 * np.pi * 2400 * t + 0.8) + 0.028 * np.sin(2 * np.pi * 3900 * t + 0.3)
    return np.pad(np.array((left, right), dtype=np.float32), ((0, 0), (rate // 10, 0)))


def capture(x, rate, *, initial=LOW, step=None, split=None, block=BLOCK, reset_tail=False):
    plugin = pb.load_plugin(str(MODULE))
    params = plugin.parameters
    settings = {f"band_{i}_active": 0.0 for i in range(1, 9)}
    settings.update({SWEEP["key"](k): v for k, v in dict(
        SWEEP["BASE"], active=1.0, enabled=1.0, shape=0.0, stereo=0.0, dynamic=0.0, sidechain=0.0).items()})
    settings.update(processing=0.0, output=0.5, bypass=0.0, spatial_bypass=0.0, mono_audition=0.0)
    for name, raw in settings.items():
        SWEEP["set_raw"](params[name], raw)

    def set_depth(depth):
        text = SWEEP["set_raw"](params["band_3_depth"], depth)
        if SWEEP["display_number"](text, "depth") != round(100 * depth):
            raise RuntimeError(f"Expected Depth {depth:.0%}, got {text!r}")

    def process(part, reset):
        out = plugin.process(part.copy(), rate, buffer_size=block, reset=reset)
        if out.shape != part.shape or out.dtype != np.float32 or not np.isfinite(out).all():
            raise RuntimeError(f"Expected finite float32 stereo {part.shape}, got {out.shape}, {out.dtype}")
        return out.astype(np.float64)

    set_depth(initial)
    if split is None:
        return process(x, True)
    head = process(x[:, :split], True)
    if step is not None:
        set_depth(step)
    return np.concatenate((head, process(x[:, split:], reset_tail)), axis=1)


def views(x):
    return (*HOST["mid_side"](x), x)


def triplet(seq):
    return "/".join(f"{v:.3e}" for v in seq)


def report(label, x):
    lanes = views(x)
    print(f"  {label:34} {triplet(np.sqrt(np.mean(v ** 2)) for v in lanes):>28}"
          f"  {triplet(np.max(np.abs(v)) for v in lanes):>28}")


def onset(label, delta, rate, split):
    sample = np.max(np.abs(delta), axis=0)
    tick_wait = (-split) % TICK
    print(f"  {label}: boundary n={split} (mod 32={split % TICK}); next nominal n%32=0 at +{tick_wait} samples")
    print(f"    pre-boundary peak {np.max(sample[:split]):.3e} FS; first |L/R delta| above threshold:")
    for threshold in (0.0, 1e-8, 1e-7, 1e-6):
        hit = np.flatnonzero(sample[split:] > threshold)
        position = f"+{hit[0]} samples ({hit[0] * 1000 / rate:.3f} ms)" if hit.size else "none"
        print(f"      {threshold:.0e} FS: {position}")


def projection(delta, endpoint, rate):
    print("  Projection onto the static 71%-minus-33% waveform (M/S); residual RMS / endpoint RMS.")
    print("  Each row is a 5 ms window from the boundary. Nominal progress at the window midpoint:"
          " 12 ms alone / 18 ms alone / ideal continuous cascade. These are not fitted DSP parameters.")
    for start in (0, 5, 10, 15, 20, 30, 45, 60, 90, 150, 300):
        end = start + 5
        w = slice(round(start * rate / 1000), round(end * rate / 1000))
        y, ref = (np.asarray(HOST["mid_side"](v[:, w])) for v in (delta, endpoint))
        power = np.sum(ref ** 2, axis=1)
        scale = np.sum(y * ref, axis=1) / power
        residue = np.sqrt(np.sum((y - scale[:, None] * ref) ** 2, axis=1) / power)
        fast, slow = np.exp(-(start + 2.5) / np.array((12.0, 18.0)))
        cascade = 1 - (18 * slow - 12 * fast) / 6
        print(f"    {start:3}–{end:<3} ms  scale {scale[0]:+.4f}/{scale[1]:+.4f}  "
              f"resid {residue[0]:.3e}/{residue[1]:.3e}  nominal {1-fast:.4f}/{1-slow:.4f}/{cascade:.4f}")


def measure(rate):
    x = stimulus(rate)
    split = 3 * rate // 4 + 13  # Absolute DSP samples include the silent lead.
    before_tick, after_tick = split + 14, split + 22
    tail = slice(split, None)
    print(f"\n{rate} Hz | 100 ms silent lead; asymmetric 900/2400/3900 Hz tones; "
          f"buffer_size {BLOCK} vs {ALT_BLOCK}")
    unsplit = capture(x, rate)
    high = capture(x, rate, initial=HIGH)
    stay = capture(x, rate, split=split)
    step = capture(x, rate, split=split, step=HIGH)
    reset_stay = capture(x, rate, split=split, reset_tail=True)
    reset_step = capture(x, rate, split=split, step=HIGH, reset_tail=True)
    alt_stay = capture(x, rate, split=split, block=ALT_BLOCK)
    alt_step = capture(x, rate, split=split, step=HIGH, block=ALT_BLOCK)
    shifted = {n: capture(x, rate, split=n, step=HIGH) - capture(x, rate, split=n)
               for n in (before_tick, after_tick)}

    delta = step - stay
    alt_delta = alt_step - alt_stay
    reset_delta = reset_step - reset_stay
    endpoint = high - unsplit
    print(f"  {'waveform contrast':34} {'RMS M/S/stereo (FS)':>28}  {'peak M/S/stereo (FS)':>28}")
    report("no-op split minus unsplit", (stay - unsplit)[:, tail])
    report("step minus matched no-op", delta[:, tail])
    report("buffer-size effect, no-op", (alt_stay - stay)[:, tail])
    report("buffer-size effect, step delta", (alt_delta - delta)[:, tail])
    report("shift within same tick, step delta", (shifted[before_tick] - delta)[:, tail])
    report("explicit reset effect, no-op", (reset_stay - stay)[:, tail])
    report("explicit reset effect, step delta", (reset_delta - delta)[:, tail])
    last = slice(-rate // 10, None)
    report("last 100 ms: step minus static 71%", (step - high)[:, last])
    report("static 71% minus 33%, last 100 ms", endpoint[:, last])
    onset(f"step, buffer_size={BLOCK}", delta, rate, split)
    onset(f"step, buffer_size={ALT_BLOCK}", alt_delta, rate, split)
    onset("step, shifted before tick", shifted[before_tick], rate, before_tick)
    onset("step, shifted past tick", shifted[after_tick], rate, after_tick)
    onset("step with explicit reset", reset_delta, rate, split)
    projection(delta[:, tail], endpoint[:, tail], rate)


def main():
    identify()
    print("Static +6 dB Stereo Bell 2.40 kHz, Q=1, width 100%, spread 0%, zero latency. "
          "Depth 33% -> 71% on the same instance between calls; second call reset=False except as labeled.")
    for rate in (48_000, 96_000):
        measure(rate)
    print("\nWaveform timing and projections are black-box observations. The nominal tick assumes n=0 phase; "
          "the output does not directly expose the traced 18 ms tick-rate or 12 ms per-sample states. "
          "Pedalboard host-call changes only: no VST3 parameter-queue timestamps exercised. No files saved.")


if __name__ == "__main__":
    main()
