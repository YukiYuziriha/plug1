"""Offline Stereo / Zero Latency dynamics probe for installed Spatial EQ 0.7.0.

Uses Measure-Spatial-EQ.py's existing local dependencies. From the worktree:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'tools/Invoke-HalfCpu.ps1' -Executable python -CommandArguments @('-B','tools/Probe-Spatial-EQ-Dynamics.py')"
Only synthetic audio, in memory. Tests settled controls with Internal sidechain,
plus an External selection with no auxiliary audio supplied.
"""

import argparse
from hashlib import file_digest
from math import exp, log10
from pathlib import Path
from runpy import run_path

TOOLS = Path(__file__).resolve().parent
HOST = run_path(str(TOOLS / "Measure-Spatial-EQ.py"))
MODEL = run_path(str(TOOLS / "Spatial-EQ-DelayModel.py"))
AUTO = run_path(str(TOOLS / "Spatial-EQ-AutomationModel.py"))
np, pb, MODULE = (HOST[k] for k in ("np", "pb", "MODULE"))
matched, filter_stereo, boost_wet, wet_highpass = (
    MODEL[k] for k in ("_matched_pole", "_filter_stereo", "_boost_wet", "_wet_highpass"))
tick_filter = AUTO["_tick_filter"]
HZ, Q, TICK = 2400, 1.0, 32
IDENTITY = (1.0, 0.0, 0.0, 0.0, 0.0)
SHA256 = "058b89a2e97e79b6f5da4d825f78bdd1eda04a8d292f9bbfda9c58506ec64849"


def render(x, rate, mode, gain, depth=0, *, other_slot=None, other_depth=0,
           attack=15, release=150, threshold=-36, span=12, q=Q, shape="bell", external=False, block=512):
    plugin = pb.load_plugin(str(MODULE))
    p = plugin.parameters
    settings = {f"band_{i}_active": 0 for i in range(1, 9)}
    settings.update(processing=0, output=0.5, bypass=0, spatial_bypass=0,
                    mono_audition=0, spatial_bass_protection=0.533953547)

    def band(slot, gain, mode, depth, shape="bell"):
        settings.update({f"band_{slot}_{key}": raw for key, raw in dict(
            active=1, enabled=1, frequency=0.587483108, q={1.0: 0.349599957, 30.0: 1}[q],
            gain=0.5 + gain / 60, depth=depth, shape={"bell": 0, "low_shelf": 1 / 6}[shape],
            stereo=0, dynamic=mode / 2,
            threshold=(threshold + 72) / 72, range=span / 18,
            attack={0.1: 0, 15: 0.403023839, 200: 1}[attack],
            release={10: 0, 150: 0.394953758, 2000: 1}[release],
            width=0.5, spread=0, sidechain=int(external and slot == 3)).items()})

    band(3, gain, mode, depth, shape)
    if other_slot is not None:
        band(other_slot, 12, 0, other_depth)
    for key, raw in settings.items():
        p[key].raw_value = raw
        if abs(p[key].raw_value - raw) > 1e-6:
            raise RuntimeError(f"{key}: requested {raw}, got {p[key].raw_value}")
    expected = {"processing": "Zero Latency", "band_3_stereo": "Stereo",
                "band_3_dynamic": ("Off", "Downward", "Upward")[mode],
                "band_3_sidechain": "External" if external else "Internal",
                "band_3_shape": {"bell": "Bell", "low_shelf": "Low Shelf"}[shape]}
    for key, text in expected.items():
        if p[key].string_value != text:
            raise RuntimeError(f"{key}: expected {text}, got {p[key].string_value}")
    y = plugin.process(x.copy(), rate, buffer_size=block)
    if y.shape != x.shape or y.dtype != x.dtype or not np.isfinite(y).all():
        raise RuntimeError(f"Unexpected render: {y.shape}, {y.dtype}")
    arrivals = np.flatnonzero(np.max(np.abs(y), axis=0) > 1e-7)
    if not arrivals.size or arrivals[0] != rate // 10:
        raise RuntimeError("Expected zero-latency onset")
    return y.astype(np.float64)


def fixture(rate):
    # Deliberately off the 32-sample cadence; asymmetric channel energy and phase.
    n = np.arange(rate * 2)
    s = n / rate
    edges = [rate // 10, *(int(rate * t) + 13 for t in (0.36, 0.61, 0.94, 1.3, 1.53))]
    levels = np.array([-np.inf, -41, -31, -15, -40, -24, -49])
    scale = 10 ** (levels[np.searchsorted(edges, n, side="right")] / 20)
    lead = n >= edges[0]
    return np.array((scale * np.cos(2 * np.pi * HZ * s) + lead * 0.10 * np.cos(2 * np.pi * 430 * s),
                     -0.28 * scale * np.cos(2 * np.pi * HZ * s + 0.4)), dtype=np.float32)


def gain_ticks(x, rate, mode, threshold, span, attack, release, q, alternative):
    if alternative != "wideband detector":
        x = filter_stereo(x, matched(rate, HZ, Q if alternative == "fixed detector Q" else q, "bandpass"))
    power = x.mean(axis=0) ** 2 if alternative == "mono detector" else np.mean(x * x, axis=0)
    attack, release = (exp(-1000 / (rate * ms)) for ms in (attack, release))
    step = 1 - exp(-TICK / (rate * 0.018))
    env = gain = 0.0
    for i, p in enumerate(power.tolist()):
        if i % TICK == 0:
            level = 10 * log10(max(env, 1e-12))
            excess = level - threshold
            if alternative == "below threshold":
                excess = -excess
            target = max(0.0, excess * (1 if alternative == "unity slope" else 0.5))
            if alternative != "unlimited range":
                target = min(span, target)
            if mode == 1:
                target = -target
            elif not mode:
                target = 0.0
            gain += (target - gain) * (1 if alternative == "no gain slew" else step)
            yield gain
        p = min(p, 1e4)
        env = p + (env - p) * (attack if p > env else release)


def predict(x, rate, mode, gain, depth=0, *, other_slot=None, other_depth=0,
            attack=15, release=150, threshold=-36, span=12, q=Q, shape="bell", alternative=None):
    def bell(db, resonance=q):
        return matched(rate, HZ, resonance, "bell", db) if abs(db) > 1e-9 else IDENTITY

    raw = x
    if other_slot is not None and other_slot < 3:
        x = filter_stereo(x, bell(12))
    static_coeffs = matched(rate, HZ, q, shape, gain) if gain else IDENTITY
    static = filter_stereo(x, static_coeffs)
    detector = {"post-EQ detector": static, "raw detector": raw}.get(alternative, x)
    gains = list(gain_ticks(detector, rate, mode, threshold, span, attack, release, q, alternative))
    frames = [min(TICK, x.shape[1] - i) for i in range(0, x.shape[1], TICK)]
    if alternative == "one bell":
        direct = tick_filter(x, [bell(gain + db) for db in gains], frames)
    else:
        dynamic_q = Q if alternative == "fixed dynamic Q" else q
        direct = tick_filter(static, [bell(db, dynamic_q) for db in gains], frames)
    if depth and gain < 0:
        correction_from = static if alternative == "static-only Mid restore" else direct
        direct = direct + depth * (x - correction_from).mean(axis=0)
    if other_slot is not None and other_slot > 3:
        direct = filter_stereo(direct, bell(12))
    wet = None
    if depth and gain > 0:
        feed = static if alternative == "pre-dynamics wet feed" else direct
        wet = boost_wet(feed, depth, rate, HZ, gain, q, shape)
    if other_depth:
        if wet is not None:
            raise ValueError("Two concurrent wet boosts are not modeled by this fixture")
        wet = boost_wet(direct, other_depth, rate, HZ, 12, Q, "bell")
    if wet is not None and depth and gain < 0:
        wet_cut = filter_stereo(wet, static_coeffs)
        if alternative != "static-only wet cut":
            wet_cut = tick_filter(wet_cut, [bell(db if alternative == "signed wet dynamics" else min(0, db))
                                            for db in gains], frames)
        wet = wet + depth * (wet_cut - wet)
    return direct if wet is None else direct + wet_highpass(wet, rate)


def rms(x):
    return np.sqrt(np.mean(x ** 2))


def compare(label, observed, predicted, alternatives=()):
    scale = rms(observed)
    if scale < 1e-6:
        raise AssertionError(f"{label}: fixture has no measurable effect")
    error = rms(observed - predicted) / scale
    print(f"{label:<28} {20 * np.log10(error):7.2f} dBr | peak {np.max(np.abs(observed - predicted)):.3g} FS")
    if error >= 10 ** (-65 / 20):
        raise AssertionError(f"{label}: model error too large")
    for name, rival in alternatives:
        mismatch = rms(observed - rival) / scale
        print(f"  vs {name:<24} {20 * np.log10(mismatch):7.2f} dBr")
        if mismatch < max(error * 10, 0.001):
            raise AssertionError(f"{label}: fixture does not distinguish {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rate", type=int, choices=(44100, 48000), help="Run only one sample rate")
    args = parser.parse_args()
    with MODULE.open("rb") as binary:
        digest = file_digest(binary, "sha256").hexdigest()
    if digest != SHA256:
        raise RuntimeError(f"Unexpected Spatial EQ binary SHA-256: {digest}")
    plugin = pb.load_plugin(str(MODULE))
    for field in ("dynamic", "threshold", "range", "attack", "release", "sidechain"):
        p = plugin.parameters[f"band_3_{field}"]
        values = p.valid_values if field in ("dynamic", "sidechain") else p.range
        for slot in range(1, 9):
            other = plugin.parameters[f"band_{slot}_{field}"]
            if (other.raw_value, other.string_value, other.range) != (p.raw_value, p.string_value, p.range):
                raise AssertionError(f"Band {slot}: unexpected {field} inventory")
        print(f"{field}: {values}; default {p.string_value} (raw {p.raw_value:.9g})")
    for rate, block in ((48000, 512), (44100, 257)):
        if args.rate is not None and args.rate != rate:
            continue
        print(f"\n{rate} Hz, {block}-sample buffer, error relative to observed isolated effect")
        x = fixture(rate)

        def direct(label, mode, gain, rivals=(), **kw):
            off = render(x, rate, 0, gain, block=block, **kw)
            on = render(x, rate, mode, gain, block=block, **kw)
            baseline = predict(x, rate, 0, gain, **kw)
            compare(label, on - off, predict(x, rate, mode, gain, **kw) - baseline,
                    [(r, predict(x, rate, mode, gain, alternative=r, **kw) - baseline) for r in rivals])

        direct("Downward, +6 dB static", 1, 6,
               ("post-EQ detector", "wideband detector", "mono detector", "one bell", "no gain slew"))
        direct("Upward, 0 dB static", 2, 0,
               ("below threshold", "unity slope", "unlimited range"), threshold=-34, span=4)
        direct("Earlier band +12 dB", 1, 6, ("raw detector",), other_slot=2)
        direct("Later band +12 dB", 1, 6, other_slot=4)
        direct("Downward, +6 dB shelf", 1, 6, ("post-EQ detector",), shape="low_shelf")
        direct("Downward, Q 30", 1, 6, ("fixed detector Q", "fixed dynamic Q"), q=30.0)
        direct("Fast attack/release", 1, 6, attack=0.1, release=10)
        direct("Slow attack/release", 1, 6, attack=200, release=2000)
        for mode, gain, rival in ((1, 6, "pre-dynamics wet feed"), (2, -6, "static-only Mid restore"),
                                  (1, -6, "static-only Mid restore")):
            d = 0.75
            ref = render(x, rate, mode, gain, d, block=block) - render(x, rate, mode, gain, block=block)
            base = predict(x, rate, mode, gain)
            compare(f"Depth: mode {mode}, gain {gain:+} dB", ref, predict(x, rate, mode, gain, d) - base,
                    [(rival, predict(x, rate, mode, gain, d, alternative=rival) - base)])
        for mode, rival in ((1, "static-only wet cut"), (2, "signed wet dynamics")):
            kw = dict(other_slot=2)
            ref = (render(x, rate, mode, -6, 0.75, other_depth=0.50, block=block, **kw)
                   - render(x, rate, mode, -6, 0.75, block=block, **kw))
            base = predict(x, rate, mode, -6, 0.75, **kw)
            compare(f"Overlap wet: cut mode {mode}", ref,
                    predict(x, rate, mode, -6, 0.75, other_depth=0.50, **kw) - base,
                    [(rival, predict(x, rate, mode, -6, 0.75, other_depth=0.50, alternative=rival, **kw) - base)])
        flat = render(x, rate, 2, 0, block=block)
        if not np.array_equal(flat, render(x, rate, 2, 0, 1, block=127)):
            raise AssertionError("Zero static gain: Depth and host block size should not change output")
        if not np.array_equal(x, render(x, rate, 2, 0, span=0, block=block)):
            raise AssertionError("Zero static gain and Range zero should be exact dry")
        print("Zero static gain: Depth 0/100 and alternate block size sample-identical; Range 0 exact dry")
        for mode in (1, 2):
            if not np.array_equal(x, render(x, rate, mode, 0, external=True, block=block)):
                raise AssertionError("External selection without auxiliary audio should give no gain change")
        print("External selected, no auxiliary audio supplied: both modes exact dry at static gain 0")


if __name__ == "__main__":
    main()
