"""Compare the standalone SpatialEqHarness executable with the offline Python models.

Requires NumPy in the selected interpreter; no plugin, host, or audio files.
Pass the absolute or relative path to SpatialEqHarness as the sole argument.
"""

import argparse
from pathlib import Path
from runpy import run_path
import subprocess

import numpy as np


TOOLS = Path(__file__).resolve().parent
STATIC = run_path(str(TOOLS / "Spatial-EQ-DelayModel.py"))["process_bands"]
AUTOMATED = run_path(str(TOOLS / "Spatial-EQ-AutomationModel.py"))["process_bands_components"]
SHAPES = ("bell", "low_shelf", "high_shelf")
FIELDS = ("frequency", "gain_db", "depth", "q", "shape")
PEAK_LIMIT = 2e-6  # Absolute full scale, on each L/R/M/S lane.
RELATIVE_LIMIT = -75  # RMS error relative to reference RMS.
RMS_FLOOR = 1e-8  # Avoid unstable relative scores on effectively silent lanes.


def fixture(rate, impulse=False):
    frames = rate // 12
    x = np.zeros((2, frames), dtype=np.float32)
    if impulse:
        x[0, 13] = 0.125
        x[1, round(0.00073 * rate)] = -0.0625
        return x
    t = np.arange(2 * frames // 3) / rate
    envelope = np.clip((t[-1] - t) / 0.008, 0, 1)
    left = np.cos(2 * np.pi * (101 * t + 53_000 * t**2))
    right = np.sin(2 * np.pi * (6_200 * t - 47_000 * t**2) + 0.4)
    x[0, :t.size] = envelope * (0.07 * left + 0.03 * np.sin(2 * np.pi * 523 * t))
    x[1, :t.size] = envelope * (0.04 * right + 0.04 * np.cos(2 * np.pi * 997 * t))
    return x


def render(exe, rate, x, bands, steps):
    settings = {slot: dict(zip(FIELDS, values), active=True, enabled=True)
                for slot, *values in bands}

    def snapshot():
        for slot in range(1, 9):
            b = settings.get(slot)
            if b is None:
                yield "0 1 0 1000 0 1 0"
            else:
                yield (f"{int(b['active'])} {int(b['enabled'])} {SHAPES.index(b['shape'])} "
                       f"{b['frequency']} {b['gain_db']} {b['q']} {b['depth']}")

    lines = [f"{rate} {x.shape[1]} {len(steps)}", *snapshot()]
    for sample, changes in steps:
        reset = False
        for slot, update in changes.items():
            reset |= update.get("reset", False)
            settings[slot].update(update)
        lines.extend((f"{sample} {int(reset)}", *snapshot()))
    lines.extend(f"{left:.9g} {right:.9g}" for left, right in x.T)
    result = subprocess.run([exe, "--render"], input="\n".join(lines) + "\n", text=True,
                            capture_output=True, timeout=30, check=False)
    if result.returncode:
        raise RuntimeError(f"C++ render exited {result.returncode}: {result.stderr.strip()}")
    # Parse through float32 to recover the exact in-place engine output before differencing.
    out = np.loadtxt(result.stdout.splitlines(), dtype=np.float32).T.astype(np.float64)
    if out.shape != x.shape or not np.isfinite(out).all():
        raise AssertionError(f"invalid C++ render: shape {out.shape}, expected {x.shape}, or non-finite values")
    return out


def views(lr):
    return np.vstack((lr, (lr[0] + lr[1]) * 0.5, (lr[0] - lr[1]) * 0.5))


def compare(label, expected, actual):
    if not np.isfinite(expected).all():
        raise AssertionError(f"{label}: non-finite Python reference")
    error = views(actual - expected)
    peak = np.max(np.abs(error), axis=1)
    rms = np.sqrt(np.mean(error**2, axis=1))
    ref_rms = np.sqrt(np.mean(views(expected)**2, axis=1))
    relative = np.full(4, -np.inf)
    score = (rms > RMS_FLOOR) & (ref_rms > RMS_FLOOR)
    relative[score] = 20 * np.log10(rms[score] / ref_rms[score])
    if np.any(peak > PEAK_LIMIT) or np.any(relative > RELATIVE_LIMIT):
        raise AssertionError(f"{label} (L/R/M/S): peak={peak} FS, RMS={rms} FS, relative={relative} dBr")
    return peak.max(), rms.max(), relative.max()


def depth_off(bands, steps):
    off_bands = tuple((slot, hz, gain, 0.0, q, shape) for slot, hz, gain, _, q, shape in bands)
    off_steps = []
    for sample, changes in steps:
        off_changes = {slot: {k: v for k, v in fields.items() if k != "depth"}
                       for slot, fields in changes.items()}
        off_changes = {slot: fields for slot, fields in off_changes.items() if fields}
        if off_changes:
            off_steps.append((sample, off_changes))
    return off_bands, off_steps


def run_case(exe, label, rate, x, bands, steps=(), window=slice(None), *, isolate_boost=False):
    def process(config, events):
        ref = (sum(AUTOMATED(x, config, steps=events, sample_rate=rate)) if steps
               else STATIC(x, config, sample_rate=rate))
        return ref, render(exe, rate, x, config, events)

    try:
        ref_on, got_on = process(bands, steps)
        ref_off, got_off = process(*depth_off(bands, steps))
        ref_delta, got_delta = ref_on - ref_off, got_on - got_off
        if np.max(np.abs(ref_delta[:, window])) < 1e-5:
            raise AssertionError("reference fixture does not exercise Depth")
        results = {
            "Depth off": compare(f"{label} Depth off", ref_off[:, window], got_off[:, window]),
            "full": compare(f"{label} full", ref_on[:, window], got_on[:, window]),
            "Depth delta": compare(f"{label} Depth delta", ref_delta[:, window], got_delta[:, window]),
        }
        if isolate_boost:
            # Retain cut Depth to make the wet-cut routing observable independently
            # of the larger cut restoration in the all-Depth delta.
            cuts_only = tuple((slot, hz, gain, depth if gain < 0 else 0.0, q, shape)
                              for slot, hz, gain, depth, q, shape in bands)
            ref_cuts, got_cuts = process(cuts_only, ())
            ref_boost = ref_on - ref_cuts
            if np.max(np.abs(ref_boost)) < 1e-5:
                raise AssertionError("reference fixture does not exercise the boost return through cuts")
            results["boost delta with cuts"] = compare(f"{label} boost delta with cuts", ref_boost,
                                                       got_on - got_cuts)
        return results
    except (AssertionError, RuntimeError, subprocess.TimeoutExpired, ValueError) as error:
        raise AssertionError(f"{label}: {error}") from error


def cases(exe):
    cuts = ((2, 900, -9, 0.75, 1.4, "bell"), (4, 360, -12, 0.5, 0.7, "low_shelf"),
            (7, 4100, -6, 1.0, 1.3, "high_shelf"))
    mixed = ((1, 360, 6, 0.8, 0.9, "bell"), (3, 510, 9, 0.65, 1.1, "bell"),
             (6, 3750, 6, 0.5, 0.8, "high_shelf"), *cuts)
    for rate in (44100, 48000, 96000):
        x, impulse = fixture(rate), fixture(rate, impulse=True)
        for band in cuts:
            for name, signal in (("sweep", x), ("impulse", impulse)):
                yield run_case(exe, f"{rate} negative {band[-1]} {name}", rate, signal, (band,))
        yield run_case(exe, f"{rate} mixed slots", rate, x, mixed, isolate_boost=True)
        yield run_case(exe, f"{rate} dual mono low shelf", rate, np.repeat(impulse[:1], 2, axis=0),
                       ((8, 220, 9, 0.75, 1.1, "low_shelf"),))

    for rate in (48000, 96000):
        x = fixture(rate)
        steps = ((129, {1: {"frequency": 660, "depth": 0.55}, 3: {"q": 2.1}}),
                 (519, {4: {"gain_db": -4, "enabled": False}}),
                 (947, {4: {"enabled": True}, 6: {"active": False}}),
                 (1331, {1: {"reset": True}, 6: {"active": True}}))
        yield run_case(exe, f"{rate} same-sign automation/reset", rate, x, mixed, steps)
        for (_, hz, _, _, q, shape) in cuts:
            for gain in (-6, 6):
                write = 171  # Not a coefficient tick, or the regular irregular-block boundary.
                steps = ((write, {2: {"gain_db": -gain}}),)
                bands = ((2, hz, gain, 0.8, q, shape), (6, 2750, 6, 0.64, 1.0, "bell"))
                # Measure the transition, not an RMS dominated by the settled pre-roll.
                yield run_case(exe, f"{rate} {shape} {gain:+} to {-gain:+}", rate, x, bands,
                               steps, window=slice(write, None))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    exe = args.executable.resolve(strict=True)
    all_results = {}
    for results in cases(exe):
        for key, row in results.items():
            all_results.setdefault(key, []).append(row)
    for key, rows in all_results.items():
        peak, rms, relative = np.max(rows, axis=0)
        score = f"{relative:.2f} dBr" if np.isfinite(relative) else "below scoring floor"
        print(f"{key}: {len(rows)} comparisons; worst L/R/M/S peak {peak:.3g} FS, "
              f"RMS {rms:.3g} FS, relative {score}")


if __name__ == "__main__":
    main()
