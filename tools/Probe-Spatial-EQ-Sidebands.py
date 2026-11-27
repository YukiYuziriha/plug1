"""Headless centered weak-probe sideband measurement; no files generated.

From the repository root (dependencies as in Measure-Spatial-EQ.py):
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-Sidebands.py
"""

import hashlib
from itertools import product
from pathlib import Path
from runpy import run_path


CONTEXT = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-Context.py")))
np, pb, capture, ms, amp, db_ratio = (
    CONTEXT[k] for k in ("np", "pb", "capture", "ms", "amp", "db_ratio")
)
RATE, LEAD, BLOCK = (CONTEXT[k] for k in ("RATE", "LEAD", "BLOCK"))
MODULE = CONTEXT["HOST"]["MODULE"]
BED_HZ = 2400
PROBES = (1732, 2868, 3972)
LEVELS = (-48, -60)  # peak dBFS per comb tooth
ORDERS = (1, 2, 3, 4)
ON = RATE
N = RATE // 4
START = ON + RATE // 2
END = START + 2 * N


def fold(hz):
    # A negative-frequency image of a real signal appears at positive frequency.
    return abs((hz + RATE // 2) % RATE - RATE // 2)


def bins():
    families = [[fold(f + sign * order * BED_HZ) for f in PROBES for sign in (-1, 1)]
                for order in ORDERS]
    targets = [f for family in families for f in family]
    harmonics = {fold(k * BED_HZ) for k in range(RATE)}
    signed_probes = (0, *PROBES, *(-f for f in PROBES))
    probe_imd = {fold(sum(combo)) for combo in product(signed_probes, repeat=3)}
    step = RATE // N
    guards = [f + df for f in targets for df in (-step, step)]
    if (len(set(targets + guards)) != len(targets + guards)
            or set(targets + guards) & (harmonics | probe_imd)
            or any(not 0 < f < RATE // 2 or f % step for f in targets + guards)):
        raise RuntimeError("Noncoherent or colliding sideband/guard bins")
    return np.asarray(families) // step, np.asarray(guards) // step


def tone(hz):
    return np.cos(2 * np.pi * ((hz * np.arange(END)) % RATE) / RATE)


def centered(bed, probe, eps, *, fresh=True):
    signals = [(bed + sign * eps * probe).astype(np.float32) for sign in (1, -1)]
    derivatives = []
    for depth in (0.0, 1.0):
        # Capture resets before processing and returns float64 after hosting in float32.
        plus, minus = (capture(x, depth, fresh=fresh) for x in signals)
        derivatives.append(ms(plus - minus) / (2 * eps))
    off, full = derivatives
    return off, full - off


def spectrum(x, start=START):
    return 2 / N * np.fft.rfft(x[:, start:start + N], axis=-1)


def peak(z):
    return np.max(np.abs(z), axis=-1)


def pair(values):
    return "/".join(f"{value:.1f}" for value in values)


def db_peak(z):
    return pair(db_ratio(peak(z), 1))


def main():
    targets, guards = bins()
    with MODULE.open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB bell at 2.4 kHz, Q=1, stereo, dynamics off; {RATE} Hz float32, "
          f"{BLOCK}-sample blocks, {LEAD}-sample silent lead, no time shift.")
    print(f"Bed: -24 dBFS peak, Mid-only {BED_HZ} Hz. Probe after {ON / RATE:g} s, "
          f"analyzed from {START / RATE:g} s in two adjacent {N / RATE:g} s rectangular windows.")
    print(f"Probe comb: {PROBES} Hz, one M or S input route at a time (M/S=(L+-R)/2).")
    print("Jd=(Yd(bed+eps*p)-Yd(bed-eps*p))/(2*eps); Delta J=J100-J0.")
    print("Target bins by order, folded to positive Hz (probe order, minus then plus):")
    for order, row in zip(ORDERS, targets):
        print(f"  {order}: {(row * (RATE // N)).tolist()}")
    print("Tables show peak line per order in dB re per-tooth input amplitude; pairs are output M/S.")
    print("Drift is the peak complex line difference relative to the peak line in the current row.")

    bed = np.broadcast_to(amp(-24) * tone(BED_HZ), (2, END))
    bed32 = bed.astype(np.float32)
    for depth in (0.0, 1.0):
        out = ms(capture(bed32, depth, fresh=True))
        early = out[:, ON - N:ON]
        late = out[:, START + N:END]
        print(f"Bed only D{depth:.0%}: max pre-probe vs late window change "
              f"{np.max(np.abs(early - late)):.3g} FS; "
              f"target peak {db_peak(spectrum(out)[:, targets.ravel()])} dBFS.")

    comb = sum(map(tone, PROBES))
    comb[:ON] = 0
    quiet = np.zeros_like(bed)
    for label, route in (("M", (1, 1)), ("S", (1, -1))):
        probe = np.asarray(route)[:, None] * comb
        prev = None
        for level in LEVELS:
            eps = amp(level)
            off, delta = centered(bed, probe, eps)
            qoff, qdelta = centered(quiet, probe, eps)
            response, repeat, d0, q, q0 = (spectrum(x, t) for x, t in (
                (delta, START), (delta, START + N), (off, START),
                (qdelta, START), (qoff, START)))
            print(f"\n{label} input, {level} dBFS/tooth: adjacent-bin Delta J peak "
                  f"{db_peak(response[:, guards])} dB; quiet D0 target peak "
                  f"{db_peak(q0[:, targets.ravel()])} dB; pre-probe Delta J peak "
                  f"{np.max(np.abs(delta[:, :ON])):.3g} FS/eps")
            print("order       Delta J           bed D0        quiet Delta J     window drift      level drift")
            for order, k in zip(ORDERS, targets):
                ref = peak(response[:, k])
                window = pair(db_ratio(peak((repeat - response)[:, k]), ref))
                step = ("--" if prev is None else
                        pair(db_ratio(peak((prev - response)[:, k]), ref)))
                print(f"{order:5} {db_peak(response[:, k]):>16} {db_peak(d0[:, k]):>16}"
                      f" {db_peak(q[:, k]):>16} {window:>16} {step:>16}")
            k = targets.ravel()
            strongest = k[np.argmax(np.abs(response[:, k]), axis=-1)] * (RATE // N)
            print(f"Strongest target Hz M/S: {strongest[0]}/{strongest[1]}")
            prev = response


if __name__ == "__main__":
    main()
