"""Headless weak-probe test for complex periodic beds. Writes no data.

From the repository root (dependencies as in Measure-Spatial-EQ.py):
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-ComplexBeds.py
"""

import hashlib
from math import gcd
from pathlib import Path
from runpy import run_path
from typing import NamedTuple


BASE = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-Sidebands.py")))
np, pb, amp, centered, spectrum, tone, fold, db_ratio = (
    BASE[k] for k in ("np", "pb", "amp", "centered", "spectrum", "tone", "fold", "db_ratio")
)
RATE, ON, N, START, END, MODULE, BLOCK, LEAD, LEVELS = (
    BASE[k] for k in ("RATE", "ON", "N", "START", "END", "MODULE", "BLOCK", "LEAD", "LEVELS")
)
F1, F2, PROBE = 2000, 2400, 1732
BED_DB = -24
PHASE1, PHASE2 = np.pi / 4, np.pi / 2
PURE = tuple((m, 0) for m in (-4, -3, -2, -1, 1, 2, 3, 4)) + tuple(
    (0, n) for n in (-4, -3, -2, -1, 1, 2, 3, 4)
)
MIXED = tuple((m, n) for k in (1, 2) for m in (-k, k) for n in (-k, k))
LABELS = PURE + MIXED
F2_LINES = slice(8, len(PURE))
MIXED_LINES = slice(len(PURE), None)


class Lines(NamedTuple):
    delta: np.ndarray
    off: np.ndarray
    guard: np.ndarray
    drift: np.ndarray
    cross: float


def bins():
    step = RATE // N
    signed = np.array([PROBE + m * F1 + n * F2 for m, n in LABELS])
    if RATE % N or any(s % step for s in (F1, F2, PROBE)) or np.any(np.abs(signed) >= RATE / 2):
        raise RuntimeError("Expected a coherent grid with unwrapped signed target frequencies")
    targets = np.abs(signed)
    guards = targets[:, None] + step * np.array([-1, 1])
    all_hz = np.concatenate((targets, guards.ravel()))
    # Exclude collisions even with higher-order single-bed products and with
    # bed-only, carrier, and translated third-harmonic probe terms.
    lattice = gcd(RATE, gcd(F1, F2))
    def translated(base, spacing):
        return {fold(base + j) for j in range(0, RATE, spacing)}

    unwanted = translated(0, lattice) | translated(3 * PROBE, lattice) | {PROBE}
    f1_lines, f2_lines = (translated(PROBE, gcd(RATE, f)) for f in (F1, F2))
    if (len(set(all_hz)) != len(all_hz) or set(all_hz) & unwanted
            or set(guards.ravel()) & translated(PROBE, lattice)
            or np.any((all_hz <= 0) | (all_hz >= RATE / 2))):
        raise RuntimeError("Colliding, contaminated or out-of-range target/guard bins")
    for (m, n), hz in zip(LABELS, targets):
        if (hz in f1_lines) != (n == 0) or (hz in f2_lines) != (m == 0):
            raise RuntimeError(f"{(m, n)} overlaps the wrong single-bed lattice")
        for i in range(-6, 7):
            for j in range(-6, 7):
                if abs(i) + abs(j) <= 6 and (i, j) != (m, n) and fold(PROBE + i * F1 + j * F2) == hz:
                    raise RuntimeError(f"{(m, n)} collides with {(i, j)} through order six")
    return targets // step, guards // step, signed


def measure(bed, probe, eps, output, grid, *, fresh=False):
    targets, guards, signed = grid
    off, delta = centered(np.broadcast_to(bed, (2, END)), probe, eps, fresh=fresh)
    first, second, off_fft = spectrum(delta), spectrum(delta, START + N), spectrum(off)
    z = first[output, targets]
    return Lines(np.where(signed < 0, z.conj(), z), np.abs(off_fft[output, targets]),
                 np.max(np.abs(first[output, guards]), axis=-1),
                 np.abs(second[output, targets] - z), np.max(np.abs(first[1 - output, targets])))


def db(z, ref=1):
    return f"{db_ratio(abs(z), abs(ref)):.1f}"


def rms(z):
    return np.sqrt(np.mean(np.abs(z) ** 2))


def phase(z, floor):
    return f"{np.angle(z, deg=True):+.1f}" if abs(z) > 2 * floor else "--"


def report(rows, rotation, signed, previous):
    a, b, joint, rotated, saw, quiet = rows
    # Subtract the quiet baseline once in the prediction, not once per Fourier bin.
    predicted = a.delta + b.delta - quiet.delta
    residual = joint.delta - predicted
    cases = (a, b, joint, rotated, quiet)
    floor = np.max([np.abs(quiet.delta), *(r.off for r in cases),
                    *(r.guard for r in cases), *(r.drift for r in cases)], axis=0)
    # A residual combines four measurements; use four times the largest control
    # as an operational scale, not a statistical noise bound.
    print("  Pure lines | joint and summed singles (mag dB, phase deg) | complex residual | control")
    print("     (m, n)   signed Hz     joint dB    deg      sum dB    deg     err dB   err dBr    ctrl dB")
    for k, key in enumerate(PURE):
        z, p, e, f = joint.delta[k], predicted[k], residual[k], floor[k]
        print(f"  {str(key):>9} {signed[k]:>9} {db(z):>12} {phase(z, f):>6}"
              f" {db(p):>11} {phase(p, f):>6} {db(e):>10} {db(e, z):>9} {db(f):>10}")
    for label, k in (("F1", slice(0, 8)), ("F2", F2_LINES)):
        print(f"    {label} complex RMS sum error {db(rms(residual[k]), rms(joint.delta[k]))} dBr;"
              f" residual {db(rms(residual[k]))} dB, control {db(rms(floor[k]))} dB.")

    print("  Mixed lines | residual phase and joint phase-rotation check")
    print("     (m, n)   signed Hz    joint dB     sum dB     err dB    err °   turn °   miss °  miss dBr    ctrl dB")
    for k, key in enumerate(MIXED, start=len(PURE)):
        z, r, e, f = joint.delta[k], rotated.delta[k], residual[k], floor[k]
        turn = rotation[k]
        miss = phase(r * (z * turn).conj(), 2 * f * f)
        if min(abs(z), abs(r)) <= 2 * f:
            miss = "--"
        print(f"  {str(key):>9} {signed[k]:>9} {db(z):>11} {db(predicted[k]):>10} {db(e):>10}"
              f" {phase(e, 4 * f):>8} {np.angle(turn, deg=True):>+8.1f} {miss:>8}"
              f" {db(r - z * turn, z):>9} {db(f):>10}")

    print("  F2 sine vs RMS-matched four-harmonic saw | complex saw minus sine")
    print("          n   signed Hz     sine dB    deg      saw dB    deg     diff dB  diff dBr    ctrl dB")
    saw_floor = np.maximum.reduce([floor, saw.off, saw.guard, saw.drift])
    for k in range(F2_LINES.start, F2_LINES.stop):
        z, s, f = b.delta[k], saw.delta[k], saw_floor[k]
        print(f"  {LABELS[k][1]:>9} {signed[k]:>9} {db(z):>12} {phase(z, f):>6}"
              f" {db(s):>11} {phase(s, f):>6} {db(s - z):>11} {db(s - z, z):>9} {db(f):>10}")
    k = F2_LINES
    print(f"    Saw/sine complex RMS difference {db(rms(saw.delta[k] - b.delta[k]), rms(b.delta[k]))} dBr.")
    print("  Case control peaks (dB):       D0    guard   window    cross")
    for label, r in zip(("F1", "F2", "joint", "rotated", "saw", "quiet"), rows):
        print(f"    {label:<18} " + " ".join(f"{db(np.max(v)):>8}" for v in (r.off, r.guard, r.drift, r.cross)))
    print(f"    Quiet Delta target peak {db(np.max(np.abs(quiet.delta)))} dB.")
    if previous is not None:
        k = MIXED_LINES
        drift = (
            ("joint pure", joint.delta[:len(PURE)], previous[2].delta[:len(PURE)]),
            ("mixed residual", residual[k], (previous[2].delta - previous[0].delta
                                             - previous[1].delta + previous[5].delta)[k]),
            ("saw", saw.delta[F2_LINES], previous[4].delta[F2_LINES]),
        )
        print("    Probe-level complex RMS drift (dBr): " + ", ".join(
            f"{label} {db(rms(x - y), rms(x))}" for label, x, y in drift))


def main():
    grid = bins()
    with MODULE.open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB bell at 2.4 kHz, Q=1, stereo, dynamics off; {RATE} Hz float32, "
          f"{BLOCK}-sample blocks, {LEAD}-sample silent lead, no time shift.")
    print(f"Mid beds: each cosine {BED_DB} dBFS peak, F1={F1} Hz, F2={F2} Hz; "
          "joint is the unscaled sum. Rotated joint: F1 +45 deg, F2 +90 deg.")
    print("Saw: sum of sin(k*theta + pi/2*k)/k for k=1..4, normalized to sine RMS; "
          "fundamental phase matches the F2 cosine, not its level.")
    print(f"Cosine probe {PROBE} Hz, onset {ON / RATE:g} s. Two contiguous {N / RATE:g} s "
          f"rectangular FFT windows from {START / RATE:g} s; report first window.")
    print("Targets and guards unique on the FFT grid; targets checked against bed-only, translated "
          "probe third harmonics, wrong single-bed lattices, and other joint labels through order six. "
          "Higher-order coincidences and cubic terms on the probe lattice cannot be excluded.")
    print("Delta = J100-J0; Jd is the centered derivative. Phase at signed f=probe+m*F1+n*F2 "
          "(negative frequencies conjugated), referenced to sample zero. dB re input probe; "
          "dBr re observed joint or sine (level drift re weaker-probe result).")
    print("Controls: target Depth 0, adjacent Delta bins, complex window drift, and quiet Delta. "
          "ctrl is their per-line maximum; cross is the other M/S output. Phase suppressed "
          "within 2x control (8x for four-term residual), not a confidence interval.")

    def angle(hz):
        return 2 * np.pi * ((hz * np.arange(END)) % RATE) / RATE

    a, b = (amp(BED_DB) * tone(f) for f in (F1, F2))
    rotated = amp(BED_DB) * (np.cos(angle(F1) + PHASE1) + np.cos(angle(F2) + PHASE2))
    theta = angle(F2) + np.pi / 2
    harmonics = range(1, 5)
    saw = amp(BED_DB) * sum(np.sin(k * theta) / k for k in harmonics) / np.sqrt(
        sum(1 / k ** 2 for k in harmonics))
    assert np.isclose(rms(saw), rms(b), rtol=1e-12)
    beds = a, b, a + b, rotated, saw, np.zeros_like(a)
    rotation = np.exp(1j * np.array([m * PHASE1 + n * PHASE2 for m, n in LABELS]))
    carrier = tone(PROBE)
    carrier[:ON] = 0
    for output, (label, route) in enumerate((("M", (1, 1)), ("S", (1, -1)))):
        probe = np.array(route)[:, None] * carrier
        previous = None
        for level in LEVELS:
            print(f"\n{label}->{label}, probe {level} dBFS peak", flush=True)
            rows = [measure(bed, probe, amp(level), output, grid) for bed in beds]
            if previous is None:
                fresh = measure(a + b, probe, amp(level), output, grid, fresh=True)
                if not np.array_equal(fresh.delta, rows[2].delta):
                    raise RuntimeError("Fresh and reset joint renders differ at the measured bins")
                print("Fresh and reset joint captures match at all target bins.")
            report(rows, rotation, grid[2], previous)
            previous = rows


if __name__ == "__main__":
    main()
