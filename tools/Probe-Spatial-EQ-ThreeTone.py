"""Headless fixed-pair transfer probe; audio stays in memory.

From the worktree root with the existing reference fixture and dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-ThreeTone.py
"""

import hashlib
from itertools import product
from math import gcd
from pathlib import Path
from runpy import run_path
from time import perf_counter
from typing import NamedTuple


BASE = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-MixedBatch.py")))
np, pb, amp, db, tone, fold, measure, bed_tone, error = (
    BASE[k] for k in ("np", "pb", "amp", "db", "tone", "fold", "measure", "bed_tone", "error")
)
RATE, ON, N, START, END, MODULE, BLOCK, LEAD, LEVELS, PROBE, STEP, LABELS, REF_DB = (
    BASE[k] for k in ("RATE", "ON", "N", "START", "END", "MODULE", "BLOCK", "LEAD", "LEVELS",
                     "PROBE", "STEP", "LABELS", "REF_DB")
)
A, B = BASE["PAIRS"][0]
AB_PHASES = (25, -40)
C_HZ = (680, 3560)
C_LEVELS = (-30, -18)
C_PHASE = 35
RAMP = RATE // 20
ORDER = 6
LIMIT_SECONDS = 80


class Condition(NamedTuple):
    hz: int
    level: int
    phase: int = C_PHASE
    gated: bool = False


STEADY = tuple(Condition(hz, level) for hz, level in product(C_HZ, C_LEVELS))
CHALLENGE = STEADY[-1]
CONDITIONS = (*STEADY, CHALLENGE._replace(phase=125), CHALLENGE._replace(gated=True))


def checked_grid(c):
    signed = np.array([PROBE + m * A + n * B for m, n in LABELS])
    targets = abs(signed)
    guards = targets[:, None] + STEP * np.array([-1, 1])
    bins = (*targets, *guards.ravel())
    if (RATE != N * STEP or START % N or END != START + 2 * N or ON + RAMP > START
            or len({A, B, c, PROBE}) != 4
            or any(not 0 < f < RATE // 2 or f % STEP for f in (A, B, c, PROBE, *bins))
            or len(set(bins)) != len(bins)):
        raise RuntimeError(f"Invalid coherent grid for C={c}")

    spacing = gcd(RATE, A, B, c)

    def lattice(base):
        return {fold(base + f) for f in range(0, RATE, spacing)}

    if (set(bins) & ({PROBE} | lattice(0) | lattice(3 * PROBE))
            or set(guards.ravel()) & lattice(PROBE)):
        raise RuntimeError(f"Control or guard collision for C={c}")
    images = {hz: [] for hz in targets}
    for m, n, k in product(range(-ORDER, ORDER + 1), repeat=3):
        if abs(m) + abs(n) + abs(k) <= ORDER:
            hz = fold(PROBE + m * A + n * B + k * c)
            if hz in images:
                images[hz].append((m, n, k))
    for hz, (m, n) in zip(targets, LABELS):
        if images[hz] != [(m, n, 0)]:
            raise RuntimeError(f"C={c}: AB target at {hz} Hz collides with {images[hz]}")
    return targets // STEP, guards // STEP, signed


def tone_c(case):
    c = bed_tone(case.hz, case.level, case.phase)
    if case.gated:
        c[:ON] = 0
        c[ON:ON + RAMP] *= (1 - np.cos(np.pi * np.arange(RAMP) / RAMP)) / 2
    return c


def peak_control(rows):
    return np.maximum.reduce([field for r in rows for field in (r.off, r.guard, r.drift)])


def isolated(rows):
    q, a, b, ab = rows
    return (ab.delta - a.delta) - (b.delta - q.delta)


def conditional(rows):
    c, ac, bc, abc = rows
    return (abc.delta - ac.delta) - (bc.delta - c.delta)


def run_route(output, grids, deadline):
    carrier = tone(PROBE)
    carrier[:ON] = 0
    probe = np.asarray((1, 1 if output == 0 else -1))[:, None] * carrier
    a, b = (bed_tone(hz, REF_DB, phase) for hz, phase in zip((A, B), AB_PHASES))
    ab = a + b
    ab_beds = (np.zeros(END), a, b, ab)
    p_ab = amp(REF_DB) ** 2  # two equal cosine powers, per channel
    count = 0
    strong, weak = LEVELS
    grid = grids[C_HZ[0]]  # all C choices use the same target and guard bins

    def capture(bed, level=weak, *, fresh=False):
        nonlocal count
        if perf_counter() >= deadline:
            raise RuntimeError(f"{LIMIT_SECONDS}s budget exceeded before next capture")
        lines = measure(bed, probe, amp(level), output, grid, fresh=fresh)
        if not all(np.all(np.isfinite(field)) for field in lines):
            raise RuntimeError("Nonfinite measurement")
        count += 1
        return lines

    def c_beds(case):
        c = tone_c(case)
        return c, a + c, b + c, ab + c

    label = ("Mid", "Side")[output]
    print(f"\n{label} probe -> {label} output, {weak} dBFS", flush=True)
    base = tuple(map(capture, ab_beds))
    ab_line = isolated(base)
    base_floor = np.maximum(peak_control(base), np.abs(base[0].delta))
    print("  Isolated AB (dB re probe): " + "/".join(map(db, ab_line))
          + "; control: " + "/".join(map(db, base_floor)))
    print("  C Hz   dBFS  deg  onset   scale  fixed    power   AB|C/AB dB (by line)"
          "             phase shift deg (by line)   AB|C>8x / change>16x  nuisance dB")
    observed, fixed, power = [], [], []
    steady_challenge = None
    all_rows = list(base)
    for case in CONDITIONS:
        rows = tuple(map(capture, c_beds(case)))
        all_rows.extend(rows)
        result = conditional(rows)
        scale = p_ab / (p_ab + amp(case.level) ** 2 / 2)
        normalized = scale * ab_line
        floor = np.maximum(base_floor, peak_control(rows))
        usable = (np.abs(result) > 8 * floor) & (np.abs(ab_line) > 8 * floor)
        change = np.count_nonzero(np.abs(result - ab_line) > 16 * floor)
        nuisance = np.max(np.abs(np.stack([r.delta for r in rows[:3]])))
        mags = "/".join(db(x, y) if ok else "--" for x, y, ok in zip(result, ab_line, usable))
        angles = "/".join(f"{angle:+.1f}" if ok else "--" for angle, ok in
                          zip(np.angle(result * ab_line.conj(), deg=True), usable))
        print(f"  {case.hz:4} {case.level:6} {case.phase:4} "
              f"{'gate' if case.gated else 'steady':>6}  {scale:.3f}  "
              f"{error(result, ab_line):>6}  {error(result, normalized):>7}   "
              f"{mags:<30} {angles:<27} {np.count_nonzero(usable):>3}/4  {change}/4  {db(nuisance):>8}",
              flush=True)
        if case in STEADY:
            observed.append(result)
            fixed.append(ab_line)
            power.append(normalized)
        if case == CHALLENGE:
            steady_challenge = result
        elif case.hz == CHALLENGE.hz and case.level == CHALLENGE.level:
            print(f"    {'Gate' if case.gated else 'Phase'} vs matched steady C: "
                  f"{error(steady_challenge, result)} dBr re steady AB|C.")

    obs, unchanged, attenuated = map(np.stack, (observed, fixed, power))
    for name, selection in (("difference", slice(0, 2)), ("sum", slice(2, 4)), ("all", slice(None))):
        z, x, y = (values[:, selection] for values in (obs, unchanged, attenuated))
        print(f"  Stationary grid / {name}: fixed {error(z, x)}, power {error(z, y)} dBr.")
    fresh_ab = capture(ab, fresh=True)
    if not all(np.array_equal(x, y) for x, y in zip(base[-1], fresh_ab)):
        raise RuntimeError(f"Reset/fresh AB mismatch on {label}")
    strong_ab = isolated(tuple(capture(bed, strong) for bed in ab_beds))
    strong_cond = conditional(tuple(capture(bed, strong) for bed in c_beds(CHALLENGE)))
    print(f"  Fresh AB exact; {strong}/{weak} dBFS step drift: AB {error(ab_line, strong_ab)}, "
          f"AB|C at {CHALLENGE.hz} Hz/{CHALLENGE.level} dBFS {error(steady_challenge, strong_cond)} dBr.")
    print("  Peak D0 / adjacent / window drift / opposite output (dB re probe): "
          + " / ".join(db(max(np.max(getattr(r, field)) for r in all_rows))
                       for field in ("off", "guard", "drift", "cross")))
    return count


def main():
    start = perf_counter()
    grids = {hz: checked_grid(hz) for hz in C_HZ}
    if not all(np.array_equal(x, y) for x, y in zip(*grids.values())):
        raise RuntimeError("Expected identical target/guard bins across C frequencies")
    with MODULE.open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB/2.4 kHz/Q=1 bell, stereo, dynamics off; {RATE} Hz float32, block {BLOCK}, lead {LEAD}.")
    print(f"Mid-only AB: {A}/{B} Hz at {REF_DB} dBFS peak each, phases {AB_PHASES} deg. "
          f"Probe {PROBE} Hz at {ON / RATE:g}s. Signed targets {grids[C_HZ[0]][2].tolist()} Hz "
          "(difference pair, then sum pair).")
    print(f"Two {N / RATE:g}s coherent FFT windows from {START / RATE:g}s; first scored, second checks drift. "
          f"Gate onset {ON / RATE:g}s, {RAMP / RATE:g}s cosine fade; both windows fully on.")
    print(f"Targets uniquely match (+probe, m, n, 0) through bed order {ORDER} including folding; "
          "guards off the translated probe lattice; bed-only and third-harmonic lattices excluded. "
          "Higher-order and same-bin nonlinear terms may remain.")
    print("Each capture is centered +/- probe, Depth 100 minus 0; conjugate negative signed lines. "
          "AB = D(AB)-D(A)-D(B)+D(quiet); AB|C = D(ABC)-D(AC)-D(BC)+D(C).")
    print("Fixed predicts AB unchanged; power predicts AB * P_AB/P_ABC (steady input power); no fit on C. "
          "Errors are complex norm ratios vs observed (dBr; lower is better; zero predictor = 0 dBr).")
    print("Control = max(quiet Delta, D0, adjacent Delta, window drift); 8x for both four-term "
          "lines, 16x for change (operational checks, not confidence bounds). Nuisance = max C/AC/BC Delta.")
    count = sum(run_route(output, grids, start + LIMIT_SECONDS) for output in range(2))
    print(f"\nComplete: {count} centered captures, {4 * count} renders in {perf_counter() - start:.2f}s.")


if __name__ == "__main__":
    main()
