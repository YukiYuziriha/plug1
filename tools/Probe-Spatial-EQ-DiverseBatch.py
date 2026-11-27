"""Headless factorial mixed-line probe. No audio or coefficients are written.

From the worktree root, using the existing reference fixture and dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-DiverseBatch.py
"""

import hashlib
from itertools import product
from math import gcd
from pathlib import Path
from runpy import run_path
from time import perf_counter
from typing import NamedTuple


MIXED = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-MixedBatch.py")))
BASE = MIXED["BASE"]
np, pb, amp, db, bed_tone, error, features = (
    MIXED[k] for k in ("np", "pb", "amp", "db", "bed_tone", "error", "features")
)
Case, LABELS, STEP, REF_DB = (MIXED[k] for k in ("Case", "LABELS", "STEP", "REF_DB"))
RATE, ON, N, START, END, MODULE, BLOCK, LEAD, LEVELS, PROBE = (
    MIXED[k] for k in ("RATE", "ON", "N", "START", "END", "MODULE", "BLOCK", "LEAD", "LEVELS", "PROBE")
)
centered, spectrum, tone, fold = (
    BASE["BASE"][k] for k in ("centered", "spectrum", "tone", "fold")
)

# Full cross: pair endpoints are center ± spacing/2, not a fixed lower/upper tone.
CENTERS = (1460, 2180, 2420, 3380)
SPACINGS = (280, 760, 1240, 1720)
CELLS = tuple(product(CENTERS, SPACINGS))
ANCHOR = (2200, 400)  # prior 2000/2400 Hz pair, separate from grid fitting
PROBES = (PROBE, 3972)
# Preregistered splits: six training cells, three withheld combinations of seen
# factor levels, and seven cells with a held-out center and/or spacing.
HELD_CENTER, HELD_SPACING = 2180, 760
HELD_COMBINATIONS = ((1460, 1240), (2420, 280), (3380, 1720))
SPLITS = ("combo", "new center", "new spacing", "both new")
FRESH_CELLS = (ANCHOR, (3380, 1720))
MODELS = ("zero", "additive", "prior", "local", "grid", "spacing", "center", "both")
LIMIT_SECONDS = 210
EQUAL = Case("equal", "factorial", (REF_DB, REF_DB))


class Lines(NamedTuple):
    delta: np.ndarray
    on: np.ndarray
    off: np.ndarray
    guard: np.ndarray
    drift: np.ndarray
    cross: np.ndarray


class Result(NamedTuple):
    cell: tuple
    case: Case
    joint: Lines
    additive: np.ndarray
    control: np.ndarray  # component × target, peaks across joint, singles and quiet
    x: np.ndarray


def split(cell):
    center, spacing = cell
    if center == HELD_CENTER:
        return "both new" if spacing == HELD_SPACING else "new center"
    if spacing == HELD_SPACING:
        return "new spacing"
    return "combo" if cell in HELD_COMBINATIONS else "train"


def challenges(cell):
    # Ratio shifts preserve total two-tone power; phases leave onset nonzero.
    low = REF_DB + 10 * np.log10(2 / (1 + amp(12) ** 2))
    high = low + 12
    if cell in ((1460, 280), (3380, 1720)):
        return (
            Case("lower strong", "ratio", (high, low)),
            Case("upper strong", "ratio", (low, high)),
            Case("both down", "co-scale", (REF_DB - 6, REF_DB - 6)),
            Case("both up", "co-scale", (REF_DB + 6, REF_DB + 6)),
            Case("rotated", "phase", EQUAL.levels, (45, 75)),
        )
    if cell == (2180, 1720):
        return (Case("lower strong/down/rotated", "joint challenge", (high - 3, low - 3), (45, 75)),)
    if cell == (3380, 760):
        return (Case("upper strong/up/rotated", "joint challenge", (low + 3, high + 3), (-60, 30)),)
    return ()


def pair(cell):
    center, spacing = cell
    return center - spacing // 2, center + spacing // 2


def bins(cell, probe):
    """Check the same exclusions as MixedBatch, independently for every cell/probe."""
    f1, f2 = pair(cell)
    signed = np.array([probe + m * f1 + n * f2 for m, n in LABELS])
    targets = abs(signed)
    guards = targets[:, None] + STEP * np.array([-1, 1])
    all_hz = np.concatenate((targets, guards.ravel()))
    if (RATE != N * STEP or START % N or cell[1] % 2 or not 0 < f1 < f2 < RATE // 2
            or any(f % STEP for f in (probe, f1, f2, *all_hz))
            or np.any((all_hz <= 0) | (all_hz >= RATE / 2)) or len(set(all_hz)) != len(all_hz)):
        raise RuntimeError(f"Noncoherent, colliding or wrapped bins: {cell}, {probe}")

    def overlaps(hz, base, lattice):
        return (hz - base) % lattice == 0 or (hz + base) % lattice == 0

    lattice = gcd(RATE, gcd(f1, f2))
    for hz in all_hz:
        if (hz == probe or overlaps(hz, 0, lattice) or overlaps(hz, 3 * probe, lattice)
                or any(overlaps(hz, probe, gcd(RATE, f)) for f in (f1, f2))):
            raise RuntimeError(f"Control lattice collision: {cell}, {probe}, {hz} Hz")
    if any(overlaps(hz, probe, lattice) for hz in guards.ravel()):
        raise RuntimeError(f"Guard on joint probe lattice: {cell}, {probe}")
    for label, target in zip(LABELS, targets):
        for m, n in product(range(-6, 7), repeat=2):
            if (abs(m) + abs(n) <= 6 and (m, n) != label
                    and fold(probe + m * f1 + n * f2) == target):
                raise RuntimeError(f"Joint collision: {cell}, {probe}, {label} vs {(m, n)}")
    return targets // STEP, guards // STEP, signed


def run_probe(output, probe_hz, grids, deadline):
    start = perf_counter()
    carrier = tone(probe_hz)
    carrier[:ON] = 0
    probe = np.asarray((1, 1 if output == 0 else -1))[:, None] * carrier
    strong, weak = LEVELS
    count = 0

    def capture(bed, grid, level=weak, *, fresh=False):
        nonlocal count
        if perf_counter() >= deadline:
            raise RuntimeError(f"{LIMIT_SECONDS}s batch budget exceeded before next capture")
        targets, guards, signed = grid
        off, delta = centered(np.broadcast_to(bed, (2, END)), probe, amp(level), fresh=fresh)
        first, second, d0 = spectrum(delta), spectrum(delta, START + N), spectrum(off)
        z = first[output, targets]
        on = z + d0[output, targets]
        lines = Lines(np.where(signed < 0, z.conj(), z), abs(on), abs(d0[output, targets]),
                      np.max(abs(first[output, guards]), axis=-1), abs(second[output, targets] - z),
                      abs(first[1 - output, targets]))
        if not all(np.all(np.isfinite(a)) for a in lines):
            raise RuntimeError("Nonfinite measurement")
        count += 1
        return lines

    # Measure quiet once for this route and probe; slice its FFT bins per cell.
    quiet_grid = tuple(np.concatenate(columns) for columns in zip(*grids.values()))
    quiet_all = capture(np.zeros(END), quiet_grid)
    results, selected = [], {}
    for index, (cell, grid) in enumerate(grids.items()):
        k = slice(index * len(LABELS), (index + 1) * len(LABELS))
        quiet = Lines(*(field[k] for field in quiet_all))
        singles = {}
        for case in (EQUAL, *challenges(cell)):
            keys = tuple(zip(pair(cell), case.levels, case.phases))
            beds = tuple(bed_tone(*key) for key in keys)
            joint_bed = sum(beds)
            joint = capture(joint_bed, grid)
            for key, bed in zip(keys, beds):
                if key not in singles:
                    singles[key] = capture(bed, grid)
            a, b = (singles[key] for key in keys)
            additive = a.delta + b.delta - quiet.delta
            controls = np.stack([abs(quiet.delta), *(np.maximum.reduce([getattr(r, attr)
                for r in (joint, a, b, quiet)]) for attr in ("off", "guard", "drift", "cross"))])
            results.append(Result(cell, case, joint, additive, controls, features(case)[1]))
            if case == EQUAL and cell in FRESH_CELLS:
                selected[cell] = joint_bed, joint

    for cell, (bed, joint) in selected.items():
        repeat = capture(bed, grids[cell])
        fresh = capture(bed, grids[cell], fresh=True)
        if not all(np.array_equal(a, b) and np.array_equal(a, c) for a, b, c in zip(joint, repeat, fresh)):
            raise RuntimeError(f"Fresh/reset mismatch: {output=}, {probe_hz=}, {cell=}")
        larger = capture(bed, grids[cell], strong)
        print(f"  {pair(cell)}: cached repeat and fresh exact at measured bins/controls; "
              f"probe step {strong}/{weak} dBFS error {error(joint.delta, larger.delta)} dBr")
    print(f"  {count} centered captures / {4 * count} renders in {perf_counter() - start:.2f}s")
    return results, count


def design(cells):
    center, spacing = np.asarray(cells).T
    return np.column_stack((np.ones_like(center), (spacing - 1000) / 1000, (center - 2200) / 1000))


def coefficient(row):
    return (row.joint.delta - row.additive) / row.x


def report(results, prior):
    local = coefficient(results[0])  # same anchor pair with the current probe
    train = [r for r in results[1:] if r.case == EQUAL and split(r.cell) == "train"]
    x_train = design([r.cell for r in train])
    y_train = np.stack([coefficient(r) for r in train])
    rows = [r for r in results[1:] if r.case != EQUAL or split(r.cell) != "train"]
    x_test = design([r.cell for r in rows])
    fitted = []
    for cols in ((0,), (0, 1), (0, 2), (0, 1, 2)):
        weights, _, rank, _ = np.linalg.lstsq(x_train[:, cols], y_train, rcond=None)
        if rank != len(cols):
            raise RuntimeError("Rank-deficient training split")
        fitted.append(x_test[:, cols] @ weights)
    obs = np.stack([r.joint.delta for r in rows])
    add = np.stack([r.additive for r in rows])
    x = np.stack([r.x for r in rows])
    predictions = (np.zeros_like(obs), add, *(add + x * c for c in (prior, local, *fitted)))
    controls = np.stack([r.control for r in rows])
    floor = controls[:, :4].max(axis=1)  # opposite M/S is diagnostic, not a noise floor

    def group(label, mask, lines=slice(None)):
        z, ctrl = obs[mask, lines], floor[mask, lines]
        margin = np.count_nonzero(abs(z) > 2 * ctrl)
        mixed = np.count_nonzero(abs(z - add[mask, lines]) > 8 * ctrl)
        print(f"  {label:<30}" + "".join(f"{error(z, p[mask, lines]):>9}" for p in predictions)
              + f"  {margin}/{z.size}; {mixed}/{z.size}")

    equal = np.array([r.case == EQUAL for r in rows])
    splits = np.array([split(r.cell) for r in rows])
    print(f"  {'Holdout complex error (dBr)':<30}" + "".join(f"{m:>9}" for m in MODELS)
          + "  joint>2x; mixed>8x")
    for name in SPLITS:
        mask = equal & (splits == name)
        group(name + " / difference", mask, slice(0, 2))
        group(name + " / sum", mask, slice(2, 4))
    for center, spacing in HELD_COMBINATIONS:
        mask = equal & np.array([r.cell == (center, spacing) for r in rows])
        group(f"combo {center}/{spacing}", mask)
    for name in ("ratio", "co-scale", "phase", "joint challenge"):
        regimes = dict.fromkeys(r.cell for r in rows if r.case.group == name)
        for cell in regimes:
            mask = np.array([r.case.group == name and r.cell == cell for r in rows])
            group(f"{name} {cell[0]}/{cell[1]}", mask)
    group("factorial holdouts", equal)
    group("all challenge cases", ~equal)

    print("  Control peaks over all cases (dB re probe): joint D100/D0/Delta = "
          + "/".join(db(max(np.max(abs(getattr(r.joint, attr))) for r in results))
                     for attr in ("on", "off", "delta")))
    peaks = np.max([r.control for r in results], axis=(0, 2))
    print("    quiet Delta / D0 / adjacent Delta / window drift / opposite M/S Delta = "
          + " / ".join(map(db, peaks)))


def main():
    start = perf_counter()
    grids = {probe: {cell: bins(cell, probe) for cell in (ANCHOR, *CELLS)} for probe in PROBES}
    split_counts = {name: sum(split(cell) == name for cell in CELLS) for name in ("train", *SPLITS)}
    if tuple(split_counts.values()) != (6, 3, 3, 3, 1):
        raise RuntimeError(f"Unexpected factorial split: {split_counts}")
    with MODULE.open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB/2.4 kHz/Q=1 bell, stereo, dynamics off; {RATE} Hz float32, "
          f"block {BLOCK}, lead {LEAD}; no alignment shift. Mid-only beds, peak {REF_DB} dBFS/tone baseline.")
    print(f"{PROBES} Hz probes at {LEVELS[1]} dBFS, start {ON / RATE:g}s; two adjacent {N / RATE:g}s "
          f"FFT windows from {START / RATE:g}s; first scored. Labels {LABELS} (difference, then sum).")
    print("Centered plus/minus captures at Depth 100 and 0; Delta = J100 - J0. Negative bins conjugated. "
          "Targets and guards checked for alias/lattice collisions through joint order six; "
          "higher-order collisions and same-lattice probe nonlinearities may remain.")
    print(f"Center × spacing: {CENTERS} × {SPACINGS} Hz, checked at both probes. Splits: {split_counts}.")
    for cell in CELLS:
        print(f"  {cell[0]:4} / {cell[1]:4} Hz: tones {pair(cell)}, {split(cell)}")
    print("Challenges at listed center/spacing (levels dBFS peak, phases degrees):")
    for cell in CELLS:
        for case in challenges(cell):
            print(f"  {cell[0]}/{cell[1]} {case.name}: {case.levels[0]:.2f}/{case.levels[1]:.2f}, {case.phases}")
    print("Additive = measured singles minus quiet. Prior = normalized anchor recipe at 1732 Hz; "
          "local = same anchor recipe at current probe. Grid = constant from six equal training cells; "
          "spacing/center/both add complex linear slopes, fit per probe, route and signed line. "
          "Both has no interaction term. Scores use all lines (energy-weighted per row), no refits on holdouts.")
    print("Operational control = max(quiet Delta, Depth 0, adjacent Delta guard, window drift) per line. "
          "2x joint and 8x four-term mixed counts are diagnostics, not statistical confidence bounds.")
    count = 0
    for output, label in enumerate(("Mid", "Side")):
        prior = None
        for probe in PROBES:
            print(f"\n{label} -> {label}, {probe} Hz", flush=True)
            results, captures = run_probe(output, probe, grids[probe], start + LIMIT_SECONDS)
            if probe == PROBE:
                prior = coefficient(results[0])
            report(results, prior)
            count += captures
    print(f"\nComplete: {count} centered captures, {4 * count} renders in {perf_counter() - start:.2f}s.")


if __name__ == "__main__":
    main()
