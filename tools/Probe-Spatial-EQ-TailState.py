"""Headless conditional tail prediction; all audio and fits stay in memory.

From the repository root, with Measure-Spatial-EQ.py's dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-TailState.py
"""

import hashlib
from dataclasses import dataclass
from pathlib import Path
from runpy import run_path


CTX = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-Context.py")))
np, measure, ms, samples = (CTX[k] for k in ("np", "measure", "ms", "ms_samples"))
HOST = CTX["HOST"]
RATE = CTX["RATE"]
ROUTE = dict(L=(1, 0), R=(0, 1), M=(1, 1), S=(1, -1))
LEVEL = 10 ** (-24 / 20)
EARLY = (0, 1.5)
LATE = (6.25, 25)
TRIGGER = samples(60)


def event(audio, time, route, gain=1, kind="click"):
    start = TRIGGER + samples(time)
    if kind == "click":
        wave = np.ones(1)
    else:
        duration, hz = {"hann": (4, 2400), "attack": (4, 1550), "short": (1, 2400)}[kind]
        t = np.arange(samples(duration)) / RATE
        envelope = (np.hanning(t.size + 2)[1:-1] if kind != "attack"
                    else np.cos(np.pi * t / (2 * duration / 1000)) ** 2)
        wave = envelope * np.cos(2 * np.pi * hz * t)
        wave /= np.max(np.abs(wave))
    audio[:, start:start + wave.size] += LEVEL * gain * np.array(ROUTE[route])[:, None] * wave


@dataclass
class Trial:
    name: str
    prior: np.ndarray
    new: np.ndarray
    error: np.ndarray
    d0_error: np.ndarray
    peak_b: float


def render_history(history):
    a = np.zeros((2, TRIGGER + samples(120)), dtype=np.float32)
    for args in history:
        event(a, *args)
    return a


def trial(name, history, trigger, sign=1, *, fresh=False, prior_scale=1):
    a = render_history(history) * np.float32(prior_scale)
    b = np.zeros_like(a)
    route, gain, kind = trigger
    event(b, 0, route, sign * gain, kind)
    out = measure(a, b, fresh=fresh)
    return Trial(name, out.prior, out.isolated, out.incremental - out.isolated,
                 ms(out.e_off), float(np.max(np.abs(b))))


def window(bounds):
    return slice(*(TRIGGER + samples(t) for t in bounds))


def dbr(error, target):
    return CTX["relative"](error, target)


def pair(values, fmt=".1f"):
    return "/".join(format(x, fmt) for x in values)


def basis(case, channel, kind):
    a = case.prior[channel]
    other = case.prior[1 - channel]
    # Causal differences/lags only, with no wrapping or samples from the new event.
    lag = np.pad(a[:-4], (4, 0))
    if kind == "gain":
        return a[:, None]
    x = np.column_stack((a, a - lag, other))
    if kind == "both":
        return np.column_stack((x, case.new[channel]))
    return x


def solve(x, y):
    return np.linalg.lstsq(x, y, rcond=1e-8)[0]


def local_fit(case, w, kind):
    y = case.error[:, w]
    xs = [basis(case, c, kind)[w] for c in range(2)]
    return np.stack([x @ solve(x, v) for x, v in zip(xs, y)])


def summed_score(errors, targets):
    return dbr(np.concatenate(errors, axis=-1), np.concatenate(targets, axis=-1))


def fit_clock(train, kind, step):
    """One set of weights per elapsed-time bin, pooled across development cases."""
    segments = [(lo, min(lo + step, EARLY[1])) for lo in np.arange(0, EARLY[1], step)]
    fits = []
    for bounds in segments:
        w = window(bounds)
        gains = [solve(np.concatenate([basis(t, c, kind)[w] for t in train]),
                       np.concatenate([t.error[c, w] for t in train])) for c in range(2)]
        fits.append((w, gains))
    return fits


def predict(case, kind, fits):
    return np.concatenate([np.stack([basis(case, c, kind)[w] @ g for c, g in enumerate(gains)])
                           for w, gains in fits], axis=-1)


def report_fit(cases):
    for bounds in (EARLY, LATE):
        w = window(bounds)
        print(f"\n{bounds[0]:g}–{bounds[1]:g} ms after B: descriptive fits (fit and score on same samples)")
        print("case                       E/A dBr     E energy %     gain change M/S      shared      M/S gain       +shape          +B")
        for t in cases:
            a, y = t.prior[:, w], t.error[:, w]
            gain, separate = CTX["scalar_fit"](y, a)
            _, shared = CTX["scalar_fit"](y, a, shared=True)
            energy = 100 * np.sum(y ** 2, axis=-1) / np.sum(t.error ** 2, axis=-1)
            print(f"{t.name:25} {pair(dbr(y, a)):>11} {pair(energy):>14} {pair(gain, '+.3f'):>19}"
                  f" {pair(dbr(y - shared.reshape(y.shape), y)):>12} {pair(dbr(y - separate, y)):>13}"
                  f" {pair(dbr(y - local_fit(t, w, 'shape'), y)):>12}"
                  f" {pair(dbr(y - local_fit(t, w, 'both'), y)) if bounds == LATE else '-':>12}")


def report_predictions(train, groups):
    print("\nEarly held-out prediction (errors relative to E; zero prediction = 0 dBr).")
    print("Only positive isolated click pairs train the models. Predictors use measured A and the B onset, not AB.")
    specs = (("gain", 1.5), ("gain", .25), ("shape", .25))
    models = [(kind, fit_clock(train, kind, step)) for kind, step in specs]

    print("  Clocked gains (M/S, 0.25 ms bins from onset): " + "  ".join(
        pair([g[0] for g in gains], "+.3f") for _, gains in models[1][1]))
    print("case                           fixed gain      clock gain     clock +shape")
    for label, cases in groups:
        targets = [t.error[:, window(EARLY)] for t in cases]
        scores = []
        for kind, fits in models:
            errors = [y - predict(t, kind, fits) for t, y in zip(cases, targets)]
            scores.append(summed_score(errors, targets))
        print(f"{label:28}" + "".join(f" {pair(score):>15}" for score in scores))


def early_b_peak(case):
    return CTX["db_ratio"](np.max(np.abs(case.new[:, window(EARLY)]), axis=-1), case.peak_b)


def report_controls(cases):
    w = window(EARLY)
    print("\nEarly isolation (largest across listed trials, per component):")
    for label, group in (("mixed routes", cases), ("mono B", [t for t in cases if t.name.startswith('mono B')])):
        print(f"  isolated B peak vs input B peak ({label}):",
              pair(np.max([early_b_peak(t) for t in group], axis=0)), "dB M/S")
    print("  Depth-off interaction peak:", pair(np.max([
        np.max(np.abs(t.d0_error[:, w]), axis=-1) for t in cases], axis=0), ".2e"), "FS")
    print("  Pre-B Depth interaction peak:", pair(np.max([
        np.max(np.abs(t.error[:, :TRIGGER]), axis=-1) for t in cases], axis=0), ".2e"), "FS")
    for t in (cases[0], next(t for t in cases if t.name.startswith('mono B'))):
        hits = [np.flatnonzero(np.abs(t.new[c, TRIGGER:]) > t.peak_b * 1e-5) for c in range(2)]
        print(f"  {t.name}: isolated B onsets (-100 dB vs B peak):",
              pair([1000 * h[0] / RATE if h.size else np.nan for h in hits], ".3f"), "ms M/S")
    print("\nB polarity control, same A and |B|: odd/even E energy ratio in dB (odd = (E+ - E-)/2).")
    print("  Windows (ms):                    0–1.5     1.75–5.75       6.25–25 (M/S)")
    paired = {t.name: t for t in cases}
    for name, pos in paired.items():
        if not name.endswith("+") or name[:-1] + "-" not in paired:
            continue
        neg = paired[name[:-1] + "-"]
        scores = [dbr((pos.error - neg.error)[:, window(bounds)],
                      (pos.error + neg.error)[:, window(bounds)])
                  for bounds in (EARLY, (1.75, 5.75), LATE)]
        print(f"  {name[:-1]:25}" + "".join(f" {pair(score):>13}" for score in scores))


def level_sweep(train, pulse, shaped, recent):
    histories = (("pulses", pulse), ("bursts", shaped), ("recent bursts", recent))
    energies = [np.linalg.norm(render_history(h)[:, window((-12, 0))].astype(np.float64))
                for _, h in histories]
    model = fit_clock(train, "gain", EARLY[1])
    w = window(EARLY)
    print("\nMono B clicks: match A's stereo energy over [-12,0) ms to the pulse train.")
    print("A gain adjusts all history events equally. Level predictor is the fixed click-pair gain with no gate.")
    print("history            A scale dB    B dBFS     E/A M/S dBr    gain change M/S    gain fit / E    prediction / E")
    for (label, history), norm in zip(histories, energies):
        scale = energies[0] / norm
        for level_db in (-12, 0, 12):
            t = trial(label, history, ("M", 10 ** (level_db / 20), "click"), prior_scale=scale)
            if np.any(early_b_peak(t) > -100):
                raise RuntimeError("Mono B isolation failed")
            y, a = t.error[:, w], t.prior[:, w]
            gain, fitted = CTX["scalar_fit"](y, a)
            score = dbr(y - predict(t, "gain", model), y)
            print(f"{label:16} {20 * np.log10(scale):+10.2f} {level_db - 24:9} {pair(dbr(y, a)):>15}"
                  f" {pair(gain, '+.3f'):>18} {pair(dbr(y - fitted, y)):>15} {pair(score):>17}")


def main():
    with HOST["MODULE"].open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {HOST['pb'].__version__}, NumPy {np.__version__}")
    print(f"{RATE} Hz, {CTX['BLOCK']}-sample blocks, reset renders, +6 dB / 2.4 kHz / Q=1 bell.")
    print("E = Delta(AB) - Delta(A) - Delta(B), Delta = Depth100 - Depth0. A is the measured prior Delta.")
    print("Descriptive residuals / E in dBr; +shape adds a four-sample backward difference and other channel's prior M/S.")
    print("Late +B adds the same channel's isolated new Delta. Neither fit identifies a physical path.")
    print("Mixed-route early Side can include an immediate B Delta; use the mono B rows for clean early M/S.")
    print("B is at 60 ms; event specs are (relative ms, route, linear gain vs -24 dBFS, optional shape).")

    # Fix the development/holdout split before comparing models.
    train = [trial(f"dev {gap} {a}>{b}", [(-gap, a)], (b, 1, "click"))
             for gap, a, b in ((5.5, "L", "R"), (8, "M", "M"), (10.667, "L", "R"),
                               (13.25, "R", "L"), (17.5, "M", "L"), (22, "L", "M"))]
    pulse = [(-32, "R", .75), (-18.5, "L", -.9), (-9.375, "M", .8)]
    shaped = [(-32, "R", .85, "attack"), (-19, "M", -.95, "hann"), (-7.5, "L", .7, "short")]
    recent = [(-23, "L", .7, "hann"), (-10, "R", .85, "attack"), (-2.125, "M", .9, "short")]
    conditions = [
        ("pair 7.25 M>R", [(-7.25, "M")], ("R", 1, "click")),
        ("pair 15.35 R>S", [(-15.35, "R")], ("S", .5, "click")),
        ("pair 24.75 L>M", [(-24.75, "L")], ("M", 2, "click")),
        ("pulse train > click", pulse, ("R", 1, "click")),
        ("burst train > click", shaped, ("L", 1, "click")),
        ("recent bursts > click", recent, ("M", 1, "click")),
        ("pulse train > Hann", pulse, ("R", 1, "hann")),
        ("burst train > Hann", shaped, ("L", 1, "hann")),
        ("recent bursts > attack", recent, ("M", 1, "attack")),
        ("mono B click, pulses", pulse, ("M", 1, "click")),
        ("mono B click, bursts", shaped, ("M", 1, "click")),
        ("mono B Hann, bursts", shaped, ("M", 1, "hann")),
    ]
    cases = []
    for i, (name, history, trigger) in enumerate(conditions):
        print(f"  {name}: {history} -> {trigger}")
        result = trial(name + "+", history, trigger)
        cases.append(result)
        if i in (3, 8):
            fresh = trial(name, history, trigger, fresh=True)
            worst = max(np.max(np.abs(getattr(result, key) - getattr(fresh, key)))
                        for key in ("prior", "new", "error", "d0_error"))
            print(f"    fresh-instance difference (worst over A, B, E and D0 E): {worst:.2e} FS")
            if worst > 1e-8:
                raise RuntimeError("Fresh and reset/reused instances disagree")
        if i in (0, 3, 4, 6, 7, 8, 11):
            cases.append(trial(name + "-", history, trigger, -1))
    report_controls(cases)
    report_fit(cases)
    groups = [(t.name, [t]) for t in cases]
    pooled = (("click pairs", ("pair",)),
              ("trains > click", tuple(c[0] for c in conditions[3:6])),
              ("trains > burst", tuple(c[0] for c in conditions[6:9])))
    groups.extend((label + " (pooled)", [t for t in cases if t.name.startswith(prefixes)])
                  for label, prefixes in pooled)
    report_predictions(train, groups)
    level_sweep(train, pulse, shaped, recent)


if __name__ == "__main__":
    main()
