"""Headless control sweep of the installed reference VST3; all audio stays in memory.

From the repo root, with the isolated dependencies in Measure-Spatial-EQ.py:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Sweep-Spatial-EQ-Controls.py
"""

import hashlib
from pathlib import Path
import re
from runpy import run_path

HOST = run_path(str(Path(__file__).with_name("Measure-Spatial-EQ.py")))
np, pb = HOST["np"], HOST["pb"]
RATE, LEAD, BLOCK, SETTLE, TONES = (HOST[k] for k in ("RATE", "LEAD", "BLOCK", "SETTLE", "TONES"))
MODULE = HOST["MODULE"]
db, rms, peak = (HOST[k] for k in ("db", "rms", "peak"))
DEPTHS = (0, 10, 25, 50, 75, 90, 100)
BASE = dict(frequency=0.587483108, q=0.349599957, gain=0.6, width=0.5,
            spread=0.0, spatial_bass_protection=0.533953547)
FIELDS = {"frequency": "Hz", "q": "", "gain": "dB", "width": "%",
          "spread": "%", "spatial_bass_protection": "Hz", "depth": "%"}
VARIANTS = {"frequency": (240, 800, 6400), "q": (0.5, 2, 4),
            "gain": (-6, 0, 3, 12), "width": (0, 50, 150, 200),
            "spread": (25, 50, 100), "spatial_bass_protection": (20, 500)}


def key(field):
    return field if field == "spatial_bass_protection" else f"band_3_{field}"


def set_raw(param, value):
    param.raw_value = value
    if abs(param.raw_value - value) > 1e-6:
        raise RuntimeError(f"{param.name}: requested raw {value}, got {param.raw_value}")
    return param.string_value


def display_number(text, field):
    match = re.fullmatch(r"([+-]?[0-9]+(?:\.[0-9]+)?)\s*(.*)", text.strip())
    if not match:
        raise RuntimeError(f"Unexpected {field} display: {text!r}")
    value, unit = float(match[1]), match[2]
    if FIELDS[field] == "Hz" and unit == "kHz":
        return value * 1000
    if unit != FIELDS[field]:
        raise RuntimeError(f"Unexpected {field} unit: {text!r}")
    return value


def raw_for_display(param, field, target):
    # Center the raw value within the interval that rounds to the requested display.
    def edge(upper):
        lo, hi = 0.0, 1.0
        for _ in range(25):
            mid = (lo + hi) / 2
            value = display_number(set_raw(param, mid), field)
            if value < target or (upper and value == target):
                lo = mid
            else:
                hi = mid
        return (lo + hi) / 2

    for raw in (0.0, 1.0):
        if display_number(set_raw(param, raw), field) == target:
            return raw
    raw = (edge(False) + edge(True)) / 2
    if display_number(set_raw(param, raw), field) != target:
        raise RuntimeError(f"Cannot set {field} to displayed {target}")
    return raw


def render(audio, depth, controls):
    plugin = pb.load_plugin(str(MODULE))
    params = plugin.parameters
    settings = {f"band_{i}_active": 0.0 for i in range(1, 9)}
    settings.update({key(k): v for k, v in dict(active=1.0, enabled=1.0, shape=0.0,
                    stereo=0.0, dynamic=0.0, sidechain=0.0, depth=depth / 100,
                    **controls).items()})
    settings.update(processing=0.0, output=0.5, bypass=0.0, spatial_bypass=0.0,
                    mono_audition=0.0)
    for k, v in settings.items():
        set_raw(params[k], v)
    out = plugin.process(audio.copy(), RATE, buffer_size=BLOCK)
    if out.shape != audio.shape or not np.isfinite(out).all():
        raise RuntimeError(f"Unexpected stereo render: {out.shape}")
    arrivals = np.flatnonzero(np.max(np.abs(out), axis=0) > 1e-7)
    if not arrivals.size or arrivals[0] != LEAD:
        raise RuntimeError(f"Expected direct arrival at {LEAD}, got {arrivals[:1]}")
    return np.asarray(HOST["mid_side"](out[:, LEAD:].astype(np.float64)))


def capture(depth, controls, stimuli, *, measure_side=True):
    impulse, dual_mono, pure_side = stimuli
    return (render(impulse, depth, controls),
            render(dual_mono, depth, controls)[:, SETTLE:],
            render(pure_side, depth, controls)[:, SETTLE:] if measure_side else None)


def deltas(current, off):
    return current[0] - off[0], current[1] - off[1]


def scalar_fit(signal, ref):
    power = np.sum(ref ** 2)
    if not power or not peak(signal):
        return "--/--"
    scale = np.sum(signal * ref) / power
    return f"{scale:.4f}/{db(rms(signal - scale * ref) / rms(signal)):.1f}"


def timing(signal):
    hits = np.flatnonzero(np.abs(signal) > 1e-5)
    onset = str(hits[0]) if hits.size else "--"
    power = signal ** 2
    total = np.sum(power)
    if not total:
        return "--/--/--"
    return f"{onset}/{np.argmax(power)}/{100 * np.sum(power[RATE // 20:]) / total:.3f}"


def tone(signal, hz):
    return np.fft.rfft(signal)[hz * signal.size // RATE]


def summarize(label, current, off, ref, dry, amplitude, hz):
    _, tone_m, tone_s = current
    dh, dt = deltas(current, off)
    dh /= amplitude
    rdh, rdt = ref
    rdh = rdh / amplitude
    pairs = " ".join
    print(f"{label:<19} | {pairs(f'{db(rms(x)):7.2f}' for x in dh)} | "
          f"{pairs(f'{timing(x):>17}' for x in dh)} | "
          f"{pairs(f'{scalar_fit(x, r):>13}' for x, r in zip(dh, rdh))}")
    dry_bin = tone(dry, hz)
    gains = [db(abs(tone(x, hz) / dry_bin)) for x in (tone_m[0], tone_s[1])]
    bins = [db(abs(tone(tone_m[1], f) / tone(dry, f))) for f in (80, 240, 800, 2400, 6400)]
    return (f"{label:<19} | {pairs(f'{db(rms(x)):7.2f}' for x in dt)} | "
            f"{pairs(f'{scalar_fit(x, r):>13}' for x, r in zip(dt, rdt))} | "
            f"{hz:5d} {gains[0]:7.2f} {gains[1]:7.2f} | {pairs(f'{x:7.2f}' for x in bins)}")


def main():
    print(f"{MODULE}\nPedalboard {pb.__version__} | NumPy {np.__version__}")
    with MODULE.open("rb") as binary:
        print(f"SHA-256 {hashlib.file_digest(binary, 'sha256').hexdigest()}")
    params = pb.load_plugin(str(MODULE)).parameters
    values = {field: {n: raw_for_display(params[key(field)], field, n) for n in targets}
              for field, targets in VARIANTS.items()}
    print("Baseline display (raw): " + "; ".join(
        f"{field} {set_raw(params[key(field)], raw)} ({raw:.9g})" for field, raw in BASE.items()))
    print("Depth displays: " + ", ".join(set_raw(params[key("depth")], d / 100) for d in DEPTHS))
    for field, settings in values.items():
        print(f"{field} display (raw): " + ", ".join(
            f"{set_raw(params[key(field)], raw)} ({raw:.9g})" for raw in settings.values()))

    impulse = np.zeros((2, LEAD + RATE), dtype=np.float32)
    impulse[:, LEAD] = 10 ** (-12 / 20)
    amplitude = float(impulse[0, LEAD])
    mono = HOST["multisine"]()
    dry = mono[LEAD + SETTLE:].astype(np.float64)
    stimuli = impulse, np.stack((mono, mono)), np.stack((mono, -mono))
    cases = {d: capture(d, BASE, stimuli) for d in DEPTHS}
    ref = deltas(cases[100], cases[0])
    print(f"\nStereo float32 {RATE} Hz / {BLOCK} samples, direct arrival n={LEAD}; "
          "Zero Latency, Stereo Bell, dynamics off, trim 0 dB.")
    print("Dual-mono -12 dBFS impulse, 1 s window. Delta = depth minus local depth 0. "
          "Impulse dBr RMS is relative to input peak.")
    print("Timing: onset (> -100 dBr) / abs peak (samples after input) / energy after 50 ms (%).")
    print("Fit: scale / residual dB relative to actual delta RMS; signed least-squares fit to local full-depth reference.")
    print("For control variants the fit reference is the unvaried full-depth bell (at 240 Hz for low-bell bass rows).")
    print("\nSetting             | Impulse dBr M/S   | Onset/peak/tail% M/S                | Impulse fit M/S")
    rows = [summarize(f"Depth {d}%", cases[d], cases[0], ref, dry, amplitude, 2400) for d in DEPTHS]
    low_ref = None
    for field, settings in values.items():
        for target, raw in settings.items():
            controls = {**BASE, field: raw}
            off = capture(0, controls, stimuli, measure_side=False)
            full = capture(100, controls, stimuli)
            display = set_raw(params[key(field)], raw)
            label = f"{field.replace('spatial_bass_protection', 'Bass')} {display}"
            hz = target if field == "frequency" else 2400
            rows.append(summarize(label, full, off, ref, dry, amplitude, hz))
            if field == "frequency" and hz == 240:
                low_ref = deltas(full, off)
    for raw in values["spatial_bass_protection"].values():
        field = "spatial_bass_protection"
        controls = {**BASE, "frequency": values["frequency"][240], field: raw}
        off = capture(0, controls, stimuli, measure_side=False)
        full = capture(100, controls, stimuli)
        display = set_raw(params[key(field)], raw)
        rows.append(summarize(f"Low bell {display}", full, off, low_ref, dry, amplitude, 240))

    print(f"\nSteady: {TONES} Hz, -32 dBFS peak/tone, 1 s settle / 2 s analysis.")
    print("Delta M/S RMS and fit use dual mono. Center gains are M->M / S->S; generated Side bins are M->S. "
          "All bin magnitudes are relative to the corresponding input bin.")
    print("Setting             | Delta dBFS M/S    | Steady fit M/S                | Hz    M->M    S->S "
          "| M->S dB at 80 / 240 / 800 / 2400 / 6400 Hz")
    print("\n".join(rows))
    for depth in (0, 50, 100):
        repeat = capture(depth, BASE, stimuli)
        errors = [peak(a - b) for a, b in zip(repeat, cases[depth])]
        print(f"Fresh repeat Depth {depth}% max abs M/S error (impulse / dual-mono / pure-Side): "
              + " / ".join(f"{e:.9g}" for e in errors))


if __name__ == "__main__":
    main()
