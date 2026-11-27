"""Offline polarity-even Mid tail engagement; no audio or coefficients saved.

From the repository root, with Measure-Spatial-EQ.py's local dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Fit-Spatial-EQ-Tail-Engagement.py
"""

import hashlib
from dataclasses import dataclass
from itertools import combinations
from pathlib import Path
from runpy import run_path


TAIL = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-TailState.py")))
CTX = TAIL["CTX"]
np, samples, window = (TAIL[k] for k in ("np", "samples", "window"))
RATE, ON, LEVEL = (TAIL[k] for k in ("RATE", "TRIGGER", "LEVEL"))
EARLY = window((0, 1.5))
LATE = window((6.25, 25))
SPECS = (("fixed", 0), ("BB12", 1), ("+recent", 2), ("+band", 3))

# Lists are (ms relative to B, route, amplitude vs -24 dBFS, optional shape).
DEV = {
    "spaced clicks": [(-25, "R", -.8), (-8, "M", 1)],
    "close clicks": [(-17, "M", .8), (-5.25, "R", -.65), (-.8, "L", 1)],
    "spaced bursts": [(-28, "R", .7, "attack"), (-8, "M", -.85, "short")],
    "close bursts": [(-20, "M", -.7, "short"), (-4.5, "L", 1, "attack")],
}
HOLD = {
    "mixed train": [(-21, "L", -.7, "short"), (-12.875, "R", .85), (-5.5, "M", -.6, "hann")],
    "matched sparse": [(-23, "L", .65), (-9.375, "M", .9)],
    "matched recent": [(-23, "R", -.65), (-1.75, "M", .9, "short")],
}


@dataclass
class Case:
    name: str
    group: str
    old: np.ndarray
    even: np.ndarray
    odd: np.ndarray
    ratios: np.ndarray
    off_peak: float
    pre_peak: float
    isolated_peak_db: np.ndarray
    side_onset_ms: float


def power(x):
    return np.mean(x.astype(float) ** 2, axis=0)


def ewma(x, tau):
    alpha = -np.expm1(-1 / (RATE * tau / 1000))
    y = np.empty_like(x)
    value = 0.0
    for i, v in enumerate(x):
        value += alpha * (v - value)
        y[i] = value
    return y


def bandpass(x):
    # Unity-peak causal biquad; intentionally a detector candidate, not a fitted EQ.
    omega = 2 * np.pi * 2400 / RATE
    alpha = np.sin(omega) / 2  # Q=1
    b = alpha / (1 + alpha)
    a1, a2 = -2 * np.cos(omega) / (1 + alpha), (1 - alpha) / (1 + alpha)
    y = np.empty_like(x, dtype=float)
    x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x.T.astype(float)):
        out = b * (v - x2) - a1 * y1 - a2 * y2
        y[:, i] = out
        x2, x1, y2, y1 = x1, v, y1, out
    return y


def ratios(a, b):
    # Known split A/B in this experiment; use only input samples through time t.
    # Freeze A's history at onset so the regressors don't need an AB detector.
    ap, bp = power(a[:, :ON]), power(b[:, EARLY])
    af, bf = power(bandpass(a[:, :ON])), power(bandpass(b[:, EARLY]))
    history = np.array([ap[-samples(12):].mean(), ewma(ap, 2)[-1], ewma(af, 2)[-1]])
    hit = ewma(bp, .45)
    return np.column_stack((hit, hit, ewma(bf, .45))) / np.maximum(history, 1e-12)


def capture_case(name, group, a, shape, db):
    b = np.zeros_like(a)
    TAIL["event"](b, 0, "M", 10 ** (db / 20), shape)
    assert not np.any(a[:, ON:]) and not np.any(b[:, :ON])
    pos, neg = [CTX["measure"](a, hit) for hit in (b, -b)]
    ep, en = [m.incremental - m.isolated for m in (pos, neg)]
    bpeak = np.max(np.abs(b))
    isolated = np.maximum(np.abs(pos.isolated), np.abs(neg.isolated))
    arrivals = np.flatnonzero(isolated[1, ON:] > 1e-5 * bpeak)
    ev, od = (ep + en) / 2, (ep - en) / 2
    return Case(name, group, pos.prior[0], ev, od, ratios(a, b),
                max(np.max(np.abs(m.e_off)) for m in (pos, neg)),
                max(np.max(np.abs(e[:, :ON])) for e in (ep, en)),
                CTX["db_ratio"](np.max(isolated[:, EARLY], axis=1), bpeak),
                1000 * arrivals[0] / RATE if arrivals.size else np.inf)


def basis(case, n, knee):
    if not n:
        return np.ones((case.ratios.shape[0], 1))
    r = case.ratios[:, :n]
    return r / (r + knee)


def engagement(case, model):
    n, knee, weights = model
    return np.clip(basis(case, n, knee) @ weights, 0, 1)


def predict(case, model):
    return -case.old[EARLY] * engagement(case, model)


def fit(train, n):
    # Scale each waveform by its old-tail norm so one loud tail cannot choose
    # the gate for the entire training set. For <=3 columns enumerate the
    # nonnegative active sets, and select by the clipped waveform objective.
    scales = [np.linalg.norm(c.old[EARLY]) for c in train]
    tail = np.concatenate([-c.old[EARLY] / s for c, s in zip(train, scales)])
    target = np.concatenate([c.even[0, EARLY] / s for c, s in zip(train, scales)])
    best = np.dot(target, target), (n, 1., np.zeros(max(1, n)))
    for knee in (np.logspace(-2, 2, 25) if n else [1.]):
        x = np.concatenate([basis(c, n, knee) for c in train])
        for count in range(1, x.shape[1] + 1):
            for cols in combinations(range(x.shape[1]), count):
                z = x[:, cols]
                w = np.linalg.lstsq(tail[:, None] * z, target, rcond=1e-8)[0]
                if np.any(w < 0):
                    continue
                residual = target - tail * np.clip(z @ w, 0, 1)
                loss = np.dot(residual, residual)
                if loss < best[0]:
                    weights = np.zeros(x.shape[1])
                    weights[list(cols)] = w
                    best = loss, (n, knee, weights)
    return best[1]


def oracle(case, w):
    # In-sample diagnostic: best single suppression factor in [0,1].
    old, y = case.old[w], case.even[0, w]
    gain = np.clip(-np.dot(y, old) / np.dot(old, old), 0, 1)
    return gain, -gain * old


def dbr(residuals, targets):
    return CTX["relative"](np.concatenate(residuals), np.concatenate(targets))


def odd_pct(cases, w, channel):
    odd = sum(np.sum(c.odd[channel, w] ** 2) for c in cases)
    even = sum(np.sum(c.even[channel, w] ** 2) for c in cases)
    return 100 * odd / (odd + even) if odd + even else np.nan


def report(label, cases, models):
    y = [c.even[0, EARLY] for c in cases]
    residuals = [dbr([t - predict(c, m) for c, t in zip(cases, y)], y) for m in models]
    bound = dbr([t - oracle(c, EARLY)[1] for c, t in zip(cases, y)], y)
    print(f"  {label:23} {len(y):2} {odd_pct(cases, EARLY, 0):9.3g} {bound:9.2f}"
          + "".join(f" {s:9.2f}" for s in residuals))
    return np.array(residuals)


def main():
    host = TAIL["HOST"]
    with host["MODULE"].open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {host['pb'].__version__}, NumPy {np.__version__}")
    print(f"{RATE} Hz / {CTX['BLOCK']} samples, reset, +6 dB / 2.4 kHz / Q=1; dual-mono B at 60 ms.")
    print("E± = Delta(A±B) - Delta(A) - Delta(±B); Delta = Depth100 - Depth0.")
    print("Predict even Mid E from measured A-only Delta times negative engagement; no AB input to model.")
    print("Fixed scalar vs causal .45 ms hit power / 12 ms prior, plus 2 ms recent, then 2.4 kHz recent band.")
    print("Gate r/(r+k), shared k fitted on development only; nonnegative mix clipped to [0,1].")

    render = TAIL["render_history"]
    train, hold = [], []
    for name, history in DEV.items():
        a = render(history)
        for shape in ("click", "attack"):
            for db in (-12, 0, 12):
                train.append(capture_case(f"{name}/{shape}/{db:+}", name, a, shape, db))
    match = np.linalg.norm(render(HOLD["matched sparse"])[:, window((-12, 0))].astype(float))
    for name, history in HOLD.items():
        a = render(history)
        if name.startswith("matched"):
            a *= np.float32(match / np.linalg.norm(a[:, window((-12, 0))].astype(float)))
            measured = np.linalg.norm(a[:, window((-12, 0))].astype(float))
            if not np.isclose(measured, match, rtol=1e-6, atol=0):
                raise RuntimeError("12 ms energy match failed")
            print(f"  {name}: prior 12 ms stereo energy {measured**2:.10g} FS^2-samples")
        for shape in ("click", "hann") if name == "mixed train" else ("click",):
            group = f"{name}/{shape}"
            for db in (-12, 0, 12):
                hold.append(capture_case(f"{group}/{db:+}", group, a, shape, db))

    cases = train + hold
    worst = np.max([c.isolated_peak_db for c in cases], axis=0)
    off, pre = (max(getattr(c, attr) for c in cases) for attr in ("off_peak", "pre_peak"))
    print(f"Controls: isolated early B peak rel input M/S {worst[0]:.1f}/{worst[1]:.1f} dB; "
          f"earliest Side > -100 dB {min(c.side_onset_ms for c in cases):.3f} ms; "
          f"max pre-B E / all-time Depth0 E {pre:.3g}/{off:.3g} FS.")
    if max(worst) > -100 or max(pre, off) > 1e-6:
        raise RuntimeError("Isolation or superposition control failed")

    models = [fit(train, n) for _, n in SPECS]
    print("\nEarly [0,1.5) ms: pooled residual/even Mid E in dBr (zero prediction = 0); "
          "odd % is odd/(odd+even) energy; oracle is descriptive single gain, not a prediction.")
    print(f"  {'group':23} {'N':>2} {'odd %':>9} {'oracle':>9}"
          + "".join(f" {name:>9}" for name, _ in SPECS))
    for name in DEV:
        report(name + " (fit)", [c for c in train if c.group == name], models)
    report("development pooled", train, models)
    groups = {name: [c for c in hold if c.group == name] for name in dict.fromkeys(c.group for c in hold)}
    scores = [report(name, subset, models) for name, subset in groups.items()]
    report("holdout pooled", hold, models)
    balanced = 10 * np.log10(np.mean(10 ** (np.array(scores) / 10), axis=0))
    print("  Equal-group mean of residual energy ratios (dBr): "
          + ", ".join(f"{name} {s:.2f}" for (name, _), s in zip(SPECS, balanced)))

    sparse, recent = (groups[f"matched {name}/click"] for name in ("sparse", "recent"))
    difference = max(np.max(np.abs(engagement(a, models[1]) - engagement(b, models[1])))
                     for a, b in zip(sparse, recent))
    if difference > 1e-6:
        raise RuntimeError("Matched 12 ms cases did not produce the same BB12 gate trace")
    print(f"\nMatched 12 ms energy & identical B clicks: largest BB12 gate difference {difference:.3g}.")
    print("Effective suppression (%): project onto each case's old tail; differing tail shapes weight the same gate differently.")
    print(f"  {'case (B dB rel -24)':29} {'observed':>9}" + "".join(f" {name:>9}" for name, _ in SPECS))
    for c in hold:
        if c.group.startswith("matched"):
            old = c.old[EARLY]
            estimates = [-np.dot(predict(c, m), old) / np.dot(old, old) for m in models]
            observed = -np.dot(c.even[0, EARLY], old) / np.dot(old, old)
            print(f"  {c.name:29} {100 * observed:9.2f}" + "".join(f" {100*g:9.2f}" for g in estimates))

    print("\nLate [6.25,25) ms: holdout only, descriptive; no early-model extension or late fit training.")
    for group, subset in groups.items():
        y = [c.even[0, LATE] for c in subset]
        score = dbr([t - oracle(c, LATE)[1] for c, t in zip(subset, y)], y)
        print(f"  {group:23} odd energy M/S {odd_pct(subset, LATE, 0):.2f}%/"
              f"{odd_pct(subset, LATE, 1):.2f}%; single suppression residual (even M) {score:.2f} dBr")


if __name__ == "__main__":
    main()
