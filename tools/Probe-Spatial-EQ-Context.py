"""Headless context probe for the installed Spatial EQ VST3; no files generated.

From the repository root, with Measure-Spatial-EQ.py's local dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-Context.py
"""

import hashlib
from pathlib import Path
from runpy import run_path
from types import FunctionType, SimpleNamespace


HOST = run_path(str(Path(__file__).with_name("Measure-Spatial-EQ.py")))
np, pb, render = (HOST[k] for k in ("np", "pb", "render"))
RATE, LEAD, BLOCK = (HOST[k] for k in ("RATE", "LEAD", "BLOCK"))
L, R, MONO = (np.array(route, dtype=np.float32) for route in ((1, 0), (0, 1), (1, 1)))
TAIL = RATE // 4
INSTANCES = {}


def ms(audio):
    return np.asarray(HOST["mid_side"](audio))


def amp(db):
    return 10 ** (db / 20)


def db_ratio(num, den):
    with np.errstate(divide="ignore", invalid="ignore"):
        return 20 * np.log10(np.divide(num, den))


def relative(residual, reference):
    return db_ratio(np.linalg.norm(residual, axis=-1), np.linalg.norm(reference, axis=-1))


def fmt(values):
    return "/".join("n/a" if np.isnan(v) else f"{v:.1f}" for v in np.atleast_1d(values))


def capture(x, depth, *, lead=LEAD, block=BLOCK, split=None, reset_between=False, fresh=False):
    """Use the exact shared parameter setup; only vary host scheduling/onset assertion."""
    onset = np.flatnonzero(np.any(x, axis=0))[0]

    def load(path):
        if fresh:
            plugin = pb.load_plugin(path)
        else:
            if depth not in INSTANCES:
                INSTANCES[depth] = pb.load_plugin(path)
            plugin = INSTANCES[depth]

        def process(audio, rate, *, buffer_size):
            if split is None:
                return plugin.process(audio, rate, buffer_size=buffer_size, reset=True)
            head = plugin.process(audio[:, :split].copy(), rate, buffer_size=buffer_size, reset=True)
            tail = plugin.process(audio[:, split:].copy(), rate, buffer_size=buffer_size,
                                  reset=reset_between)
            return np.concatenate((head, tail), axis=1)

        return SimpleNamespace(parameters=plugin.parameters, process=process)

    configured = FunctionType(render.__code__, dict(render.__globals__, LEAD=lead + onset,
                                                   BLOCK=block, pb=SimpleNamespace(load_plugin=load)),
                              argdefs=render.__defaults__)
    configured.__kwdefaults__ = render.__kwdefaults__
    return configured(np.pad(x, ((0, 0), (lead, 0))), depth, start=lead)


def measure(a, b, **host):
    # Subtract in float64 after float32 hosting. Each render starts with reset=True.
    ab = a + b
    off, off_a, off_b = (capture(x, 0.0, **host) for x in (ab, a, b))
    full, full_a, full_b = (capture(x, 1.0, **host) for x in (ab, a, b))
    return SimpleNamespace(off=off, full=full, delta=ms(full - off),
                           e_off=off - off_a - off_b, e_full=full - full_a - full_b,
                           prior=ms(full_a - off_a), isolated=ms(full_b - off_b),
                           incremental=ms((full - off) - (full_a - off_a)))


def pair(gap, route_a, route_b, level=-24, relative_db=0, sign=1):
    a = np.zeros((2, gap + TAIL), dtype=np.float32)
    b = np.zeros_like(a)
    a[:, 0] = route_a * amp(level)
    b[:, gap] = route_b * (sign * amp(level + relative_db))
    return a, b


def interaction(m):
    return m.incremental - m.isolated


def row(label, m, start, stop=None):
    w = slice(start, stop)
    err = interaction(m)[:, w]
    print(f"  {label:<25} {fmt(relative(m.e_off[:, w], m.off[:, w])):>17}"
          f" {fmt(relative(m.e_full[:, w], m.full[:, w])):>17}"
          f" {fmt(relative(err, m.delta[:, w])):>17}"
          f" {fmt(relative(err, m.isolated[:, w])):>17}")


def header():
    print(f"  {'case':<25} {'D0 L/R':>17} {'Full L/R':>17} {'Delta M/S':>17} {'vs B M/S':>17}")


def ms_samples(ms_time):
    return round(ms_time * RATE / 1000)


def compare(label, changed, base, *, scale=1):
    full = relative(changed.full / scale - base.full, base.full)
    err = interaction(base)
    contextual = relative(interaction(changed) / scale - err, err)
    print(f"  {label:<29} {fmt(full):>18} {fmt(contextual):>18}")


def scalar_fit(y, x, *, shared=False):
    if shared:
        y, x = y.ravel(), x.ravel()
    den = np.sum(x * x, axis=-1)
    gain = np.divide(np.sum(x * y, axis=-1), den, out=np.full_like(den, np.nan), where=den != 0)
    prediction = np.nan_to_num(gain)[..., None] * x
    return gain, prediction


def shape(label, m, start, *, detail=False):
    # Fit the incremental Depth response, not the total output or the error alone.
    y, x = m.incremental[:, start:], m.isolated[:, start:]
    gain, fixed = scalar_fit(y, x)
    local = np.zeros_like(y)
    shared = np.zeros_like(y)
    edges = list(range(0, x.shape[-1], ms_samples(2))) + [x.shape[-1]]
    for lo, hi in zip(edges, edges[1:]):
        w = slice(lo, hi)
        local[:, w] = scalar_fit(y[:, w], x[:, w])[1]
        shared[:, w] = scalar_fit(y[:, w], x[:, w], shared=True)[1].reshape(2, -1)
    print(f"  {label:<18} {fmt(relative(y - x, y)):>13} {'/'.join(f'{g:.3f}' for g in gain):>11}"
          f" {fmt(relative(y - fixed, y)):>13} {fmt(relative(y - shared, y)):>13}"
          f" {fmt(relative(y - local, y)):>13}")
    if detail:
        prior = m.prior[:, start:]
        err = y - x
        print("    Fits to interaction E using A's ongoing Delta tail; A gains are changes from unity.")
        print("    After B (ms) | B energy % M/S | E energy % M/S | A gain change M/S | A-fit leftover / E dBr")
        boundaries = (0, 1, 2, 5, 10, 20, 40, 80, 250)
        for lo, hi in zip(boundaries, boundaries[1:]):
            w = slice(ms_samples(lo), ms_samples(hi))
            gain_a, prediction = scalar_fit(err[:, w], prior[:, w])
            b_pct, e_pct = (100 * np.sum(v[:, w] ** 2, axis=-1) / np.sum(v ** 2, axis=-1)
                            for v in (x, err))
            print(f"    {lo:3}-{hi:<3} {fmt(b_pct):>16} {fmt(e_pct):>16}"
                  f" {'/'.join(f'{g:+.3f}' for g in gain_a):>19}"
                  f" {fmt(relative(err[:, w] - prediction, err[:, w])):>22}")


def bed_and_burst(route, sign):
    on, bed_end = ms_samples(350), ms_samples(550)
    a = np.zeros((2, bed_end + TAIL), dtype=np.float32)
    b = np.zeros_like(a)
    t = np.arange(bed_end) / RATE
    ramp = ms_samples(10)
    fade = np.sin(np.linspace(0, np.pi / 2, ramp, endpoint=False)) ** 2
    envelope = np.ones(bed_end)
    envelope[:ramp], envelope[-ramp:] = fade, fade[::-1]
    tone = 0.7 * np.cos(2 * np.pi * 2400 * t) + 0.3 * np.cos(2 * np.pi * 1740 * t)
    a[:, :bed_end] = MONO[:, None] * (amp(-24) * envelope * tone)
    n = ms_samples(8)
    burst = np.hanning(n + 2)[1:-1] * np.cos(2 * np.pi * 2400 * np.arange(n) / RATE)
    b[:, on:on + n] = route[:, None] * (sign * amp(-18) * burst)
    return a, b, on


def main():
    with HOST["MODULE"].open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB / 2.4 kHz / Q=1, stereo, dynamics off, 48 kHz float32, "
          f"{BLOCK}-sample blocks and {LEAD}-sample lead unless noted. No alignment shift.")
    print("D0/full = Depth 0/100%; Delta = full - D0; M/S = (L +/- R)/2.")
    print("Columns: 20log10(||AB - A - B|| / ||AB||) at D0 or full, or on Delta. "
          "'vs B' uses the same Delta interaction over isolated B Delta.")
    print("Norms per component and window; -inf = zero error, +inf = zero reference, n/a = 0/0. "
          "Mono means equal signals on a stereo bus, not a one-channel host.")

    print("\nFresh-instance controls: checking all A/B/AB renders at both Depths against reused/reset instances.")
    a, b = pair(512, L, R)
    bed, burst, _ = bed_and_burst(L, 1)
    for label, first, second in (("impulse", a, b), ("bed", bed, burst)):
        worst = max(np.max(np.abs(capture(x, depth) - capture(x, depth, fresh=True)))
                    for depth in (0.0, 1.0) for x in (first + second, first, second))
        print(f"  {label}: maximum absolute difference {worst:.3g} FS")
        if worst > 1e-7:
            raise RuntimeError("Reused/reset instance did not agree with the fresh control")

    print("\nImpulse gap sweep: A and B each -24 dBFS per active channel, positive. "
          "Windows start at B and span 250 ms; gaps in samples.")
    header()
    cases = {}
    for name, ra, rb in (("L>L", L, L), ("L>R", L, R), ("mono>mono", MONO, MONO), ("R>L", R, L)):
        for gap in (0, 1, 64, 240, 512, 960, 2400, 4800, 9600) if name != "R>L" else (512,):
            m = measure(*pair(gap, ra, rb))
            row(f"{name}, {gap}", m, gap)
            if gap == 512 or (name == "L>R" and gap == 64):
                cases[name, gap] = m

    print("\nSecond impulse relative level/polarity at gap 512, A = -24 dBFS:")
    header()
    for name, ra, rb in (("L>L", L, L), ("L>R", L, R), ("mono>mono", MONO, MONO)):
        for level, sign in ((-18, 1), (-18, -1), (0, -1), (18, 1), (18, -1)):
            row(f"{name}, {level:+} dB, {sign:+}", measure(*pair(512, ra, rb, relative_db=level, sign=sign)), 512)

    gap = 512
    a, b = pair(gap, L, R)
    base = cases["L>R", gap]
    print("\nScale/host controls on L>R, gap 512, equal level; all samples.")
    print("  Differences after undoing scale/sign, denominators = baseline full L/R or baseline Delta interaction M/S.")
    print(f"  {'variant':<29} {'Full L/R':>18} {'Interaction M/S':>18}")
    for level in (-60, -36, -6):
        compare(f"both {level:+} dBFS", measure(*pair(gap, L, R, level=level)), base, scale=amp(level + 24))
    compare("both polarity inverted", measure(-a, -b), base, scale=-1)
    compare("reset/reuse repeat", measure(a, b), base)
    compare("lead shifted +49 samples", measure(a, b, lead=LEAD + 49), base)
    compare("block 127", measure(a, b, block=127), base)
    compare("block 1024", measure(a, b, block=1024), base)
    split = (LEAD + gap) // BLOCK * BLOCK
    compare(f"split {split}, no reset", measure(a, b, split=split), base)
    forced = measure(a, b, split=split, reset_between=True)
    compare(f"split {split}, forced reset", forced, base)
    header()
    row("forced reset, after B", forced, gap)
    row("continuous, pre B", base, 0, gap)
    row("continuous, all samples", base, 0)
    row("continuous, first 1 ms", base, gap, gap + ms_samples(1))
    flipped = measure(a, -b)
    print(f"  Flip B only: E(-B) vs E(+B) {fmt(relative(interaction(flipped) - interaction(base), interaction(base)))} dBr; "
          f"vs -E(+B) {fmt(relative(interaction(flipped) + interaction(base), interaction(base)))} dBr (M/S).")

    print("\nShape test: incremental Delta = Delta(AB) - Delta(A), target for predicting from isolated Delta(B).")
    print("  LS signed gains are fitted in place (descriptive, not predictive); each error is relative to incremental Delta M/S.")
    print(f"  {'case':<18} {'Raw err':>13} {'Gain M/S':>11} {'Fixed err':>13}"
          f" {'2ms shared':>13} {'2ms per M/S':>13}")
    for name, gap in (("L>L", 512), ("L>R", 64), ("L>R", 512), ("mono>mono", 512)):
        shape(f"{name}, {gap}", cases[name, gap], gap, detail=(name, gap) == ("L>R", 512))

    print("\nSustained dual-mono -24 dBFS nominal peak two-tone bed; at 350 ms add 8 ms "
          "2.4 kHz Hann burst (-18 dBFS nominal peak), bed fades out at 550 ms.")
    header()
    for name, route, sign in (("mono+L", L, 1), ("mono-L", L, -1), ("mono+mono", MONO, 1)):
        a, b, on = bed_and_burst(route, sign)
        m = measure(a, b)
        if name == "mono+L":
            bed_base = m
        for lo, hi in ((-50, 0), (0, 8), (8, 30), (30, 100), (100, 200)):
            row(f"{name}, {lo}:{hi} ms", m, on + ms_samples(lo), on + ms_samples(hi))
    print("  Bed+L with both inputs down 12 dB: normalized full L/R and Delta interaction M/S differences:")
    a, b, _ = bed_and_burst(L, 1)
    scale = amp(-12)
    compare("bed and burst -12 dB", measure(a * scale, b * scale), bed_base, scale=scale)


if __name__ == "__main__":
    main()
