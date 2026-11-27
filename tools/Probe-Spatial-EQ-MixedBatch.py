"""Bounded headless mixed-line experiment; writes no audio or coefficients.

Fixture and prior observations: docs/Spatial-EQ-Reference-Measurements.md.
From the worktree root:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-MixedBatch.py
"""

import hashlib
from math import gcd
from pathlib import Path
from runpy import run_path
from time import perf_counter
from typing import NamedTuple


BASE = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-ComplexBeds.py")))
np, pb, amp, db, fold, measure, tone = (
    BASE[k] for k in ("np", "pb", "amp", "db", "fold", "measure", "tone")
)
RATE, ON, N, START, END, MODULE, BLOCK, LEAD, LEVELS, PROBE = (
    BASE[k] for k in ("RATE", "ON", "N", "START", "END", "MODULE", "BLOCK", "LEAD", "LEVELS", "PROBE")
)
PAIRS = ((BASE["F1"], BASE["F2"]), (1920, 2400), (1500, 2400))
# Columns are low/high difference images, then low/high sum images.
LABELS = ((1, -1), (-1, 1), (-1, -1), (1, 1))
STEP = RATE // N
REF_DB = BASE["BED_DB"]
MODELS = ("zero", "additive", "quadratic", "normalized")


class Case(NamedTuple):
    name: str
    group: str
    levels: tuple
    phases: tuple = (0, 0)  # degrees


class Result(NamedTuple):
    case: Case
    observed: np.ndarray
    predictions: np.ndarray
    control: np.ndarray


def cases(pair_index):
    equal = (REF_DB, REF_DB)
    # Change ratio by 12 dB without changing the steady-state two-tone RMS.
    ratio = amp(12)
    low = REF_DB + 10 * np.log10(2 / (1 + ratio ** 2))
    high = low + 12
    # Keep both isolated components nonzero at sample zero for the host onset check.
    turn = (45, 75)
    if pair_index:
        return (
            Case(f"equal-{PAIRS[pair_index][0]}", "spacing", equal),
            Case(f"crossed-{PAIRS[pair_index][0]}", "spacing+level+phase", (low - 3, high - 3), turn),
        )
    return (
        Case("anchor", "calibration", equal),
        Case("F1-strong", "ratio", (high, low)),
        Case("F2-strong", "ratio", (low, high)),
        Case("both-down", "co-scale", (REF_DB - 6, REF_DB - 6)),
        Case("both-up", "co-scale", (REF_DB + 6, REF_DB + 6)),
        Case("rotated", "phase", equal, turn),
    )



def bins(pair):
    f1, f2 = pair
    signed = tuple(PROBE + m * f1 + n * f2 for m, n in LABELS)
    targets = tuple(abs(f) for f in signed)
    guards = tuple((f - STEP, f + STEP) for f in targets)
    all_hz = (*targets, *(f for row in guards for f in row))
    if (STEP != 4 or RATE != N * STEP or START % N or not 0 < f1 < f2 < RATE // 2
            or any(f % STEP for f in (PROBE, *pair, *all_hz))
            or any(not 0 < f < RATE // 2 for f in all_hz)
            or len(set(all_hz)) != len(all_hz)):
        raise RuntimeError(f"Noncoherent, colliding or wrapped target/guard bins: {pair}")

    def translated(base, lattice):
        return {fold(base + f) for f in range(0, RATE, lattice)}

    lattice = gcd(RATE, gcd(f1, f2))
    unwanted = translated(0, lattice) | translated(3 * PROBE, lattice) | {PROBE}
    singles = translated(PROBE, gcd(RATE, f1)) | translated(PROBE, gcd(RATE, f2))
    if (set(all_hz) & (unwanted | singles)
            or set(all_hz[len(targets):]) & translated(PROBE, lattice)):
        raise RuntimeError(f"Target/guard overlaps a control lattice: {pair}")
    for label, target in zip(LABELS, targets):
        for m in range(-6, 7):
            for n in range(-6, 7):
                if ((m, n) != label and abs(m) + abs(n) <= 6
                        and fold(PROBE + m * f1 + n * f2) == target):
                    raise RuntimeError(f"{pair}: {label} collides with {(m, n)} through order six")
    return np.asarray(targets) // STEP, np.asarray(guards) // STEP, np.asarray(signed)


def bed_tone(hz, level, phase):
    theta = 2 * np.pi * ((hz * np.arange(END)) % RATE) / RATE
    return amp(level) * np.cos(theta + np.deg2rad(phase))


def features(case):
    a, b = map(amp, case.levels)
    turn = np.exp(1j * (np.asarray(LABELS) @ np.deg2rad(case.phases)))
    return np.asarray([a * b * turn, a * b / (a * a + b * b) * turn])


def error(observed, predicted):
    return db(np.linalg.norm(observed - predicted), np.linalg.norm(observed))


def scores(observed, predictions):
    return " ".join(f"{error(observed, p):>10}" for p in predictions)


def run_route(output, route, grids):
    carrier = tone(PROBE)
    carrier[:ON] = 0
    probe = np.asarray(route)[:, None] * carrier
    strong, weak = LEVELS
    count = 0

    def capture(bed, grid, level=weak, *, fresh=False):
        nonlocal count
        count += 1
        lines = measure(bed, probe, amp(level), output, grid, fresh=fresh)
        if not all(np.all(np.isfinite(field)) for field in lines):
            raise RuntimeError("Nonfinite measurement")
        return lines

    coefficient = None
    results = []
    print(f"\n{'Mid' if output == 0 else 'Side'} → same output, {weak} dBFS probe", flush=True)
    for index, (pair, grid) in enumerate(zip(PAIRS, grids)):
        print(f"  Bed {pair} Hz (gap {pair[1] - pair[0]} Hz), signed target Hz {grid[2].tolist()}", flush=True)
        quiet = capture(np.zeros(END), grid)
        singles = {}
        for case_index, case in enumerate(cases(index)):
            tones = [bed_tone(*key) for key in zip(pair, case.levels, case.phases)]
            joint_bed = tones[0] + tones[1]
            joint = capture(joint_bed, grid)
            if case_index == 0:
                check_bed, check_joint = joint_bed, joint
            isolated = []
            for key, bed in zip(zip(pair, case.levels, case.phases), tones):
                if key not in singles:
                    singles[key] = capture(bed, grid)
                isolated.append(singles[key])
            additive = isolated[0].delta + isolated[1].delta - quiet.delta
            control = np.maximum.reduce([np.abs(quiet.delta), *(
                field for line in (quiet, joint, *isolated) for field in (line.off, line.guard, line.drift)
            )])
            x = features(case)
            if case.group == "calibration":
                coefficient = (joint.delta - additive) / x
            predictions = np.vstack((np.zeros_like(additive), additive, additive + coefficient * x))
            results.append(Result(case, joint.delta, predictions, control))
            magnitudes = "/".join(db(v) for v in joint.delta)
            margins = np.divide(np.abs(joint.delta), control,
                                out=np.full_like(control, np.inf), where=control > 0)
            print(f"    {case.name:<12} bed dBFS {case.levels[0]:6.2f}/{case.levels[1]:6.2f}"
                  f"  phase {case.phases[0]:2}/{case.phases[1]:2}°  observed dB {magnitudes}"
                  f"  min line/control {db(np.min(margins))} dB")

        # Compare after other stimuli have run through the cached hosts.
        repeat = capture(check_bed, grid)
        fresh = capture(check_bed, grid, fresh=True)
        if not all(np.array_equal(a, c) and np.array_equal(b, c)
                   for a, b, c in zip(check_joint, repeat, fresh)):
            raise RuntimeError(f"Fresh/reset captures differ at {pair}, route {route}")
        print("    Fresh/reset first joint and repeat: exact match at measured bins and controls.")
        if index == 0:
            larger = capture(check_bed, grid, strong)
            print(f"    Anchor probe-step drift ({strong} vs {weak} dBFS): "
                  f"{error(check_joint.delta, larger.delta)} dBr")

    holdouts = [r for r in results if r.case.group != "calibration"]
    print("  Holdout complex error (dBr, negative beats zero); all lines scored:")
    print(f"    {'group / case':<36}" + " ".join(f"{name:>10}" for name in MODELS))
    for result in holdouts:
        label = f"{result.case.group}/{result.case.name}"
        print(f"    {label:<36}{scores(result.observed, result.predictions)}")
    print("  Pooled holdouts (energy-weighted within each row):")
    for group in dict.fromkeys(r.case.group for r in holdouts):
        report_pool(group, [r for r in holdouts if r.case.group == group])
    report_pool("all / difference", holdouts, slice(0, 2))
    report_pool("all / sum", holdouts, slice(2, 4))
    report_pool("all", holdouts)
    print(f"  {count} centered measurements × 4 plugin renders.")
    return count


def report_pool(label, rows, lines=slice(None)):
    observed = np.stack([r.observed[lines] for r in rows])
    control = np.stack([r.control[lines] for r in rows])
    predictions = np.stack([r.predictions[:, lines] for r in rows], axis=1)
    above = np.count_nonzero(np.abs(observed) > 2 * control)
    print(f"    {label:<36}{scores(observed, predictions)}  >2x control {above}/{observed.size}")


def main():
    start = perf_counter()
    grids = tuple(map(bins, PAIRS))
    with MODULE.open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}, "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB/2.4 kHz/Q=1 bell, stereo, dynamics off; {RATE} Hz float32, "
          f"block {BLOCK}, lead {LEAD}; {PROBE} Hz probe starts at {ON / RATE:g} s.")
    print(f"Mid beds; two adjacent {N / RATE:g} s FFT windows from {START / RATE:g} s, "
          f"{STEP} Hz bins; first window scored. m,n = {LABELS}.")
    print("Delta = centered Depth 100 minus Depth 0; signed negative bins conjugated, "
          "phase referenced to sample zero. Magnitudes in dB re input probe peak.")
    print("Targets avoid single-bed, bed-only, translated third-probe-harmonic and other "
          "joint labels through order six; higher-order and same-lattice terms may remain.")
    print("Controls: max of quiet Delta, Depth 0, adjacent Delta guards and complex window drift "
          "over joint/singles/quiet (not a statistical noise bound).")
    print("Models add the phase-rotated A1*A2 or A1*A2/(A1²+A2²) term to measured singles minus quiet. "
          "Each uses one complex coefficient per signed line and route, calibrated only "
          "on the anchor residual; no refit on held-out cases or frequencies.")
    count = sum(run_route(output, route, grids) for output, route in enumerate(((1, 1), (1, -1))))
    print(f"\nComplete: {count} centered measurements, {4 * count} plugin renders, {perf_counter() - start:.2f} s.")


if __name__ == "__main__":
    main()
