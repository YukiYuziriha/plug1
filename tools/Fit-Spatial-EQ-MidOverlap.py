"""Headless Mid overlap experiment. Audio and coefficients stay in memory.

From the research worktree root (same dependencies as Measure-Spatial-EQ.py):
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Fit-Spatial-EQ-MidOverlap.py
"""

from dataclasses import dataclass
from pathlib import Path
from runpy import run_path
from time import perf_counter


TOOLS = Path(__file__).resolve().parent
BASE = run_path(str(TOOLS / "Fit-Spatial-EQ-Candidate.py"))
LOCAL = run_path(str(TOOLS / "Probe-Spatial-EQ-LocalResponse.py"))
TAIL = run_path(str(TOOLS / "Probe-Spatial-EQ-TailState.py"))
BED = run_path(str(TOOLS / "Probe-Spatial-EQ-BedInteraction.py"))
np, capture, ms, amp, samples = (BED[k] for k in ("np", "capture", "ms", "amp", "samples"))
predict, energy = BASE["predict"], BASE["energy"]
RATE = BASE["RATE"]
DEPTHS = ((21, 58, 83), (33, 71, 96))
BANDS = (1150, 2380, 3920)
GATE_NAMES = ("occupancy", "power contrast", "in-phase", "quadrature")
LOCATIONS = ("post", "pre")
RIDGE = 0.02
GAIN_BUDGET = 4.0


@dataclass
class Fixture:
    name: str
    bed: np.ndarray | None
    hit: np.ndarray
    on: int
    end: int


def fixtures(holdout):
    # Each tuple fixes bed/hit frequency, level, duration, phase and routing.
    # Holdout adds chirped hits and a chopped AM bed (not used for fitting).
    specs = (
        ((1740, 1920, -23, -17, 7, 0.3, "steady", "M", 174),
         (2470, 2470, -29, -21, 12, 1.7, "steady", "L", 197),
         (3270, 2990, -19, -24, 9, 3.0, "release", "R", 182),
         (2210, 1530, -26, -19, 14, 4.2, "release", "M", 216)),
        ((2035, 2265, -25, -15, 10, 0.9, "steady", "R", 185),
         (2765, 2660, -21, -23, 16, 2.4, "release", "L", 209),
         (1475, 1755, -28, -18, 11, 3.8, "chopped", "M", 227),
         (3005, 3515, -27, -20, 6, 5.1, "steady", "L", 163)),
    )[holdout]
    cases = []
    for i, (f, g, db_a, db_b, dur, phase, gate, route, onset) in enumerate(specs):
        on, n = samples(onset), samples(dur)
        t = (np.arange(on + n + samples(80)) - on) / RATE
        rise = np.minimum(np.arange(t.size) / samples(9), 1) ** 2
        envelope = rise * np.clip((0.048 - t) / 0.009, 0, 1) ** 2
        if gate == "release":
            envelope *= np.clip((0.003 - t) / 0.012, 0, 1) ** 2
        if gate == "chopped":
            envelope *= 0.12 + 0.88 * np.maximum(np.cos(2 * np.pi * 31 * t), 0) ** 2
        carrier = 2 * np.pi * f * t
        mono = amp(db_a) * envelope * (0.8 * np.cos(carrier + 0.4)
                                       + 0.2 * np.sin(carrier * (0.71 if holdout else 0.81)))
        side = amp(db_a - 10) * envelope * np.sin(2 * np.pi * (f + 395) * t + phase) if i % 2 else 0
        a = np.stack((mono + side, mono - side)).astype(np.float32)
        # Match the shared host's direct-arrival assertion on long fade-ins.
        a[:, :np.flatnonzero(np.max(np.abs(a), axis=0) > 1e-6)[0]] = 0
        u = t[on:on + n]
        sweep = (400 if i % 2 == 0 else -310) * u * u / (2 * dur / 1000) if holdout else 0
        env = np.sin(np.pi * (np.arange(n) + 1) / (n + 1)) ** (1.4 if holdout else 2)
        packet = env * np.cos(2 * np.pi * (g * u + sweep) + phase)
        b = np.zeros_like(a)
        b[:, on:on + n] = (amp(db_b) * np.array(TAIL["ROUTE"][route])[:, None]
                           * packet / LOCAL["peak"](packet))
        # Both polarities, with opposite waveforms kept within the same split.
        for sign in (1, -1):
            cases.append(Fixture(f"{gate}-{i + 1}{'+' if sign > 0 else '-'}", a, sign * b, on, on + n))
        cases.append(Fixture(f"isolated {gate}-{i + 1}", None, b, on, on + n))

    # Also guard a quiet impulse and click-pair; do not fit holdout controls.
    on = samples(54 if holdout else 45)
    c = np.zeros((2, on + samples(80)), dtype=np.float32)
    c[:, on] = amp(-31 if holdout else -28) * np.array(TAIL["ROUTE"]["L" if holdout else "M"])
    pair = c.copy()
    pair[:, on + samples(17.3 if holdout else 11.4)] = -amp(-25 if holdout else -28) * np.array(TAIL["ROUTE"]["R"])
    cases.extend((Fixture("isolated click", None, c, on, on + samples(16)),
                  Fixture("isolated click pair", None, pair, on, on + samples(28))))
    return cases


def lag(x, n):
    return np.pad(x[..., :-n], [(0, 0)] * (x.ndim - 1) + [(n, 0)])


def pole(x, time_ms, hz=0):
    """Zero-initialized one-pole state, evaluated via causal convolution offline."""
    tau = time_ms * RATE / 1000
    j = np.arange(min(x.shape[-1], int(np.ceil(16 * tau))))
    p = np.exp(-j / tau) * (-np.expm1(-1 / tau))
    n = BASE["LTI"]["fft_length"](x.shape[-1] + j.size - 1)
    if hz or np.iscomplexobj(x):
        p = p * np.exp(2j * np.pi * hz * j / RATE)
        y = np.fft.ifft(np.fft.fft(x, n) * np.fft.fft(p, n), n)
    else:
        y = np.fft.irfft(np.fft.rfft(x, n) * np.fft.rfft(p, n), n)
    return y[..., :x.shape[-1]]


def states(x):
    # Stereo energy and complex coherence are detector inputs; correction is Mid only.
    # An 8 ms lookback suppresses most new-hit-only changes without onset labels.
    floor = amp(-66) ** 2
    delay = samples(8)
    for hz in BANDS:
        z = 2 * pole(x, 0.37, hz)
        power = np.mean(np.abs(z) ** 2, axis=0)
        fast = np.maximum(pole(power, 0.9), 0)
        old = np.maximum(lag(pole(power, 11), delay), 0)
        norm = fast + old + floor
        occupancy = old / norm
        cross = np.mean(z * lag(z, delay).conj(), axis=0)
        cross *= np.exp(-2j * np.pi * hz * delay / RATE)
        lag_power = np.maximum(pole(lag(power, delay), 0.9), 0)
        rho = pole(cross, 0.9) / np.sqrt(fast * lag_power + floor ** 2)
        yield z.real, (occupancy, occupancy * (fast - old) / norm,
                       occupancy * np.clip(rho.real, -1, 1), occupancy * np.clip(rho.imag, -1, 1))


def features(h, detectors):
    # Same band states in each competing location. Two post paths distinguish
    # modulation of all reflection lags from modulation only after 6 ms.
    late = h.copy()
    late[..., :samples(6)] = 0
    post, pre = [], []
    for band, gates in detectors:
        reflected = predict(h, band)[0]
        old = predict(late, band)[0]
        for gate in gates:
            post.extend((gate * reflected, gate * old))
            pre.append(predict(h, gate * band)[0])
    return {"post": np.column_stack(post), "pre": np.column_stack(pre)}


@dataclass
class Case:
    name: str
    depth: int
    on: int
    end: int
    # Arrays are stacked as mixture, bed, isolated hit for overlap cases.
    ref: np.ndarray
    lti: np.ndarray
    notch: dict
    columns: dict

    @property
    def overlap(self):
        return self.ref.shape[0] == 3


def measure(cases, depths, bank, method, gains):
    offsets = {w: 0.0 for w in ("pre-hit E", "Depth-0 E", "quantized AB - A - B", "fresh/reset")}
    for i, case in enumerate(cases):
        a, b = case.bed, case.hit
        signals = (a + b, a, b) if a is not None else (b,)
        off = tuple(capture(x, 0) for x in signals)
        detectors = tuple(list(states(x)) for x in signals)
        if a is not None:
            offsets["Depth-0 E"] = max(offsets["Depth-0 E"], LOCAL["peak"](ms(off[0] - off[1] - off[2])[0]))
            rounding = signals[0].astype(float) - a.astype(float) - b
            offsets["quantized AB - A - B"] = max(offsets["quantized AB - A - B"], LOCAL["peak"](rounding))
        for d in depths:
            renders = tuple(capture(x, d / 100) for x in signals)
            y = np.stack([ms(v - zero)[0] for v, zero in zip(renders, off)])
            if a is not None:
                offsets["pre-hit E"] = max(offsets["pre-hit E"], LOCAL["peak"](interaction(y)[:case.on]))
            if i == 0 and d == depths[-1]:
                offsets["fresh/reset"] = LOCAL["peak"](renders[0] - capture(signals[0], d / 100, fresh=True))
            h = BASE["kernel"](bank, d, method)
            lti = np.stack([predict(h[:1], x)[0] for x in signals])
            notch = {w: np.stack([BASE["context_correction"](x, d, h, gains, w)[0] for x in signals])
                     for w in (1, 3)}
            f = [features(h[:1], state) for state in detectors]
            yield Case(case.name, d, case.on, case.end, y, lti, notch,
                       {loc: np.stack([v[loc] for v in f]) for loc in LOCATIONS})
    print("  Render controls (max FS): " + ", ".join(f"{k} {v:.2g}" for k, v in offsets.items()))
    if max(offsets.values()) > 1e-6:
        raise RuntimeError("Reference control exceeded 1e-6 FS")


def interaction(x):
    return x[0] - x[1] - x[2]


def incremental(x):
    return x[0] - x[1]


def primary(case, x):
    w = slice(case.on, case.end)
    if case.overlap:
        yield x[0, w], 0.6
        yield incremental(x)[w], 0.3
    else:
        yield x[0, case.on:], 0.1


def fixed(case, notch):
    return case.lti + case.notch[notch] if notch else case.lti


def loss(cases, preds):
    groups = {0.6: [], 0.3: [], 0.1: []}
    for case, pred in zip(cases, preds):
        for (y, weight), (p, _) in zip(primary(case, case.ref), primary(case, pred)):
            groups[weight].append(energy(y - p) / energy(y))
    return sum(w * np.mean(v) for w, v in groups.items())


def fit(cases, location, notch):
    count = cases[0].columns[location].shape[-1]
    gram, rhs = np.zeros((count, count)), np.zeros(count)
    overlaps = sum(c.overlap for c in cases)
    for c in cases:
        for (x, w), (res, _), (y, _) in zip(primary(c, c.columns[location]),
                                             primary(c, c.ref - fixed(c, notch)), primary(c, c.ref)):
            scale = w / (overlaps if c.overlap else len(cases) - overlaps) / energy(y)
            gram += scale * (x.T @ x)
            rhs += scale * (x.T @ res)
    ridge = RIDGE * np.maximum(np.diag(gram), np.max(np.diag(gram)) * 1e-4)
    weights = np.linalg.solve(gram + np.diag(ridge), rhs)
    return weights * min(1.0, GAIN_BUDGET / max(np.sum(np.abs(weights)), 1e-12))


def report(cases, predictions, chosen):
    rows = {}
    for case, tracks in zip(cases, zip(*predictions.values())):
        w = slice(case.on, case.end)
        if case.overlap:
            post = slice(case.end, case.end + samples(25))
            pre = slice(case.on - samples(25), case.on)
            views = {
                "burst total": lambda x: x[0, w],
                "burst increment": lambda x: incremental(x)[w],
                "burst E only": lambda x: interaction(x)[w],
                "pre-hit bed": lambda x: x[0, pre],
                "post-hit 25 ms": lambda x: x[0, post],
            }
        else:
            label = "isolated clicks" if "click" in case.name else "isolated packets"
            views = {label: lambda x: x[0, case.on:]}
        for label, view in views.items():
            y = view(case.ref)
            row = [energy(y - view(p)) / energy(y) for p in tracks]
            rows.setdefault(label, []).append(row)
            if label == "burst total":
                delta = 10 * np.log10(row[list(predictions).index(chosen)] / row[1])
                print(f"  {case.name:14} {case.depth:2}%: selected burst change {delta:+.2f} dB")
    print(f"  {'Mid dBr, equal case power':25}" + "".join(f"{p:>10}" for p in predictions) + "  worst / >0.1 dB")
    for label, row in rows.items():
        powers = np.asarray(row)
        score = 10 * np.log10(powers.mean(axis=0))
        deltas = 10 * np.log10(powers[:, list(predictions).index(chosen)] / powers[:, 1])
        print(f"  {label:25}" + "".join(f"{v:10.2f}" for v in score)
              + f"  {max(deltas):+6.2f} / {np.sum(deltas > 0.1):2}/{len(row):2}")
    return {k: np.asarray(v) for k, v in rows.items()}


def main():
    start = perf_counter()
    BASE["identify"]()
    assert not set(DEPTHS[0]) & set(DEPTHS[1])
    assert not set(BASE["ANCHORS"]) & set(sum(DEPTHS, ()))
    print(f"Mid only. Development Depth {DEPTHS[0]}%; holdout {DEPTHS[1]}%. Depth 0 subtracted per render.")
    print(f"Causal detectors: {BANDS} Hz; 0.37 ms complex poles, 0.9/11 ms power poles, 8 ms lookback; "
          f"{', '.join(GATE_NAMES)}. No A/B split used for any individual prediction.")
    print("E = Delta(AB)-Delta(A)-Delta(B). Separate scoring for total, increment, E, and isolated controls.")
    off = BASE["matrix"](0, BASE["LEVELS"])
    bank = np.stack([BASE["matrix"](d, BASE["LEVELS"]) - off if d else np.zeros_like(off)
                     for d in BASE["ANCHORS"]])
    method, _ = BASE["ir_scores"](bank, off, BASE["DEV_DEPTHS"], -24)
    gains = BASE["fit_context"](bank)
    print("Development renders:")
    dev = list(measure(fixtures(False), DEPTHS[0], bank, method, gains))
    notch = min((0, 1, 3), key=lambda w: loss(dev, [fixed(c, w) for c in dev]))
    weights = {loc: fit(dev, loc, notch) for loc in LOCATIONS}

    def predictions(cases):
        out = {"LTI": [c.lti for c in cases], "fixed": [fixed(c, notch) for c in cases]}
        for loc in LOCATIONS:
            out[loc] = [p + c.columns[loc] @ weights[loc] for c, p in zip(cases, out["fixed"])]
        return out

    pred = predictions(dev)
    costs = {loc: loss(dev, p) for loc, p in pred.items() if loc != "LTI"}
    chosen = min(LOCATIONS, key=costs.get)
    print(f"Locked: {method}, fixed baseline {notch} click-notch windows; challenger {chosen} (dev only).")
    print("Dev objective (60% burst total, 30% increment, 10% isolated normalized power): "
          + ", ".join(f"{k} {10 * np.log10(v):.2f} dBr" for k, v in costs.items()))
    del dev, pred

    print("Holdout renders (no fitting):")
    hold = list(measure(fixtures(True), DEPTHS[1], bank, method, gains))
    preds = predictions(hold)
    rows = report(hold, preds, chosen)
    print("  Worst / regressions compare the dev-selected challenger against fixed, not against LTI.")
    j = list(preds).index(chosen)
    change = lambda row: 10 * np.log10(row[:, j].mean() / row[:, 1].mean())
    guarded = ("burst increment", "pre-hit bed", "post-hit 25 ms", "isolated packets", "isolated clicks")
    passes = (change(rows["burst total"]) <= -0.5
              and all(change(rows[k]) <= 0.1 for k in guarded)
              and all(np.max(10 * np.log10(v[:, j] / v[:, 1])) <= 0.5 for k, v in rows.items()
                      if k != "burst E only"))
    print(f"Guard: burst mean improves >=0.5 dB, guarded means regress <=0.1 dB, "
          f"no non-E case regresses >0.5 dB. {'PASS (research only)' if passes else 'FAIL — keep baseline'}.")
    print(f"Elapsed {perf_counter() - start:.1f} s. No audio or fitted coefficients saved; no listening test.")


if __name__ == "__main__":
    main()
