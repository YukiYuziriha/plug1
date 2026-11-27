"""Measure history dependence of a centered, finite-step Spatial EQ response.

From the repository root, with Measure-Spatial-EQ.py's local dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-MemorySweep.py

At fixed suffix length, vary only the position or frequency of an equal-energy
Mid conditioning packet on a quiet bed. The same ramp then joins the exact
phase-0 LocalResponse bed. All cases share probe time, phase, and continuation.
The longest suffix with a resolved difference gives a lower bound for this
fixture, not a fitted time constant. Frequency contrasts probe dependence on
spectral history, not the identity of an internal detector or state variable.

Reference run (SHA-256 058b89a2e97e79b6f5da4d825f78bdd1eda04a8d292f9bbfda9c58506ec64849):
At -60 dBFS, 2.1 kHz near vs far scored -10.53/-26.06 dBr (M/S) at
100 ms and -43.45/-60.59 dBr at 250 ms. The 250 ms difference exceeded
final-output rounding but was step-sensitive; 500 ms was unresolved against
that bound. At 100 ms, near 2.1 vs 6 kHz scored -10.80/-26.40 dBr.
Even at the same 260 ms packet-end age, the 100 vs 250 ms continuation
scored about -6.7/-21.3 dBr for either frequency: age alone is insufficient.
Matched energy differed by at most 3.07e-9 dB L/R; Depth-0 differences,
pre-probe residuals and the tested fresh-vs-reset differences were zero.
"""

import hashlib
from itertools import product
from pathlib import Path
from runpy import run_path


LOCAL = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-LocalResponse.py")))
np, pb, amp, rms, db, fmt, peak, error = (LOCAL[k] for k in
                                      ("np", "pb", "amp", "rms", "db", "fmt", "peak", "error"))
RATE, ON, WINDOW, MODULE, BLOCK, LEAD = (LOCAL[k] for k in
                                      ("RATE", "ON", "WINDOW", "MODULE", "BLOCK", "LEAD"))
bed_signal, probes, rendered, steps = (LOCAL[k] for k in
                                       ("bed_signal", "probes", "rendered", "steps"))
SUFFIXES = (100, 250, 500)
FREQUENCIES = (2100, 6000)
GAPS = (0, 150)  # Additional gap before the common 10 ms ramp.
LEVELS = (-60, -54)
PACKET_MS = 80
RAMP_MS = 10


def samples(ms):
    return RATE * ms // 1000


def fixture(bed, shared_ms, freq, gap_ms):
    join = ON - samples(shared_ms)
    ramp = join - samples(RAMP_MS)
    stop = ramp - samples(gap_ms)
    start = stop - samples(PACKET_MS)
    x = bed.copy()
    x[:, :ramp] *= amp(-12)
    x[:, ramp:join] *= np.linspace(amp(-12), 1, join - ramp, endpoint=False)
    t = np.arange(stop - start) / RATE
    wave = np.hanning(t.size + 1)[:-1] * np.sin(2 * np.pi * freq * t)
    # Neither packet tone overlaps a bed tone or its periodic Hann side bins,
    # so packet energy adds without a cross term in either input channel.
    x[:, start:stop] += (amp(-30) / rms(wave) * wave).astype(np.float32)
    if not np.array_equal(x[:, join:], bed[:, join:]):
        raise RuntimeError("Conditioning altered the common input suffix")
    return x


def measure(x, p, **host):
    base = rendered(x, **host)
    return base, [steps(x, p, amp(level), base, **host) for level in LEVELS]


def contrast(a, b):
    base_a, (lo_a, hi_a) = a
    base_b, (lo_b, hi_b) = b
    diff = lo_a.j - lo_b.j
    diff_hi = hi_a.j - hi_b.j
    # Triangle bound for final-output float32 rounding, in the same units as J.
    round_rms = sum(amp(step.floor) * rms(step.j) for step in (lo_a, lo_b))
    return (db(rms(diff)), db(rms(diff), rms(lo_a.j)), db(rms(diff_hi), rms(hi_a.j)),
            error(diff, diff_hi), db(round_rms, rms(diff)),
            error(base_a.delta[:, WINDOW], base_b.delta[:, WINDOW]),
            peak(lo_a.off_j - lo_b.off_j), peak(base_a.off[:, WINDOW] - base_b.off[:, WINDOW]))


def main():
    with MODULE.open("rb") as module:
        identity = hashlib.file_digest(module, "sha256").hexdigest()
    print(f"Reference SHA-256 {identity}; Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB bell at 2.4 kHz, Q=1, stereo, dynamics off; {RATE} Hz float32, "
          f"block {BLOCK}, lead {LEAD}, probe at render sample {LEAD + ON}; no shift.")
    print(f"History: {PACKET_MS} ms Mid Hann packet (-30 dBFS RMS during packet), on a -12 dB "
          f"copy of the phase-0 bed; {RAMP_MS} ms ramp to the exact shared bed. "
          "Near/far packet ends 10/160 ms before suffix start.")
    print(f"Probe: 8 ms Mid 2.4 kHz Hann at {LEVELS[0]}/{LEVELS[1]} dBFS peak L/R; "
          "score 0-200 ms after onset with the bed continuing.")
    print("F=Y100-Y0; J=(F(b+eps*p)-F(b-eps*p))/(2*eps); E=(F(+)+F(-))/2-F(b).")
    print("All pairs are M/S except D0 peaks and input quantization (L/R). "
          "dBr = 20 log10(RMS ratio); -inf = zero numerator, n/a = 0/0.")
    print("First case in each contrast is the reference. dJ = Jref-Jother. "
          "Step = ||dJ(-60)-dJ(-54)|| / ||dJ(-60)||; round = final float32 "
          "output-rounding bound / ||dJ(-60)|| (not a DSP arithmetic bound).")

    bed, p = bed_signal(0), probes()[0]
    results = {}
    energy_spread = 0.0
    for shared_ms in SUFFIXES:
        energies = []
        for freq, gap in product(FREQUENCIES, GAPS):
            x = fixture(bed, shared_ms, freq, gap)
            energies.append(rms(x[:, :ON].astype(np.float64)))
            results[shared_ms, freq, gap] = measure(x, p)
        spread = peak(db(energies, energies[0]))
        energy_spread = max(energy_spread, spread)
        if spread > 1e-5:
            raise RuntimeError(f"Unmatched pre-probe L/R energy at {shared_ms} ms: {spread:g} dB")

    print("\nSuffix/contrast                     | dJ RMS FS/eps, dB | dJ/J -60 dBr | dJ/J -54 dBr "
          "| step dBr | round dBr | bed F diff dBr | D0 dJ peak | D0 bed peak")

    def row(label, a, b):
        values = contrast(results[a], results[b])
        print(f"{label:<35} | " + " | ".join(f"{fmt(v):>14}" for v in values[:6])
              + f" | {values[6]:.3g} | {values[7]:.3g}")

    for suffix in SUFFIXES:
        for freq in FREQUENCIES:
            row(f"{suffix} ms  {freq} Hz near / far", (suffix, freq, 0), (suffix, freq, 150))
        for gap, name in zip(GAPS, ("near", "far")):
            near_band, far_band = FREQUENCIES
            row(f"{suffix} ms  {name} {near_band} / {far_band} Hz",
                (suffix, near_band, gap), (suffix, far_band, gap))
    for freq in FREQUENCIES:
        row(f"260 ms end age  {freq} Hz 100 / 250", (100, freq, 150), (250, freq, 0))
    print("Matched-end-age rows change the amount of time on the quiet vs full bed, "
          "not packet age. D0 columns are absolute peaks in FS/eps and FS, respectively.")

    all_steps = [step for _, pair in results.values() for step in pair]
    pre = max(step.pre for step in all_steps)
    quant = max(np.max(step.quant) for step in all_steps)
    even = max(np.max(db(rms(step.even), amp(level) * rms(step.j)))
               for _, pair in results.values() for level, step in zip(LEVELS, pair))
    key = (SUFFIXES[0], FREQUENCIES[0], GAPS[0])
    ref, (lo, _) = results[key]
    x = fixture(bed, *key)
    fresh = rendered(x, fresh=True)
    repeated = steps(x, p, amp(LEVELS[0]), fresh, fresh=True)
    print(f"\nControls: max pre-probe |odd/even| {pre:.3g} FS; worst input quant {quant:.2f} dBr; "
          f"worst E/odd {even:.2f} dBr; max matched-energy spread {energy_spread:.3g} dB L/R.")
    print(f"Fresh vs reset/reused ({key[0]} ms, {key[1]} Hz near): bed D0/D100 peak "
          f"{peak(fresh.off - ref.off):.3g}/{peak(fresh.full - ref.full):.3g} FS; "
          f"J peak {peak(repeated.j - lo.j):.3g} FS/eps.")
    if pre > 1e-6:
        raise RuntimeError("Pre-probe control exceeded 1e-6 FS")


if __name__ == "__main__":
    main()
