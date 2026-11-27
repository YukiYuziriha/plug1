"""Headless rate and block-size probe of the installed Spatial EQ reference.

From the worktree root, using the dependencies in Measure-Spatial-EQ.py:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-Rate.py
"""

import hashlib
from pathlib import Path
from runpy import run_path
from types import FunctionType, SimpleNamespace

HOST = run_path(str(Path(__file__).with_name("Measure-Spatial-EQ.py")))
np, pb, render = (HOST[key] for key in ("np", "pb", "render"))
RATES = (44_100, 48_000, 96_000)
BLOCKS = (64, 127, 256, 511, 1024)
BASE_BLOCK = HOST["BLOCK"]
THRESHOLDS = (-120, -100, -80)


def control_value(param, target, parse):
    # Use the midpoint of the controller's displayed-value interval.
    def edge(upper):
        lo, hi = 0.0, 1.0
        for _ in range(25):
            raw = (lo + hi) / 2
            param.raw_value = raw
            value = parse(param.string_value)
            if value < target or (upper and value == target):
                lo = raw
            else:
                hi = raw
        return (lo + hi) / 2

    raw = (edge(False) + edge(True)) / 2
    param.raw_value = raw
    if parse(param.string_value) != target:
        raise RuntimeError(f"Cannot set {param.name} to {target}: {param.string_value}")
    return raw


def hertz(text):
    value, unit = text.split()
    if unit not in ("Hz", "kHz"):
        raise ValueError(text)
    return float(value) * (1000 if unit == "kHz" else 1)


def capture(rate, block=BASE_BLOCK, *, overrides=None, lead_offset=0):
    lead = rate // 10 + lead_offset
    x = np.zeros((2, lead + rate), dtype=np.float32)
    x[:, lead] = 10 ** (-12 / 20)
    host_globals = dict(render.__globals__, RATE=rate, BLOCK=block, LEAD=lead)
    if overrides:
        # Apply only variant knobs after the helper sets and verifies its normal fixture.
        def load_plugin(path):
            plugin = pb.load_plugin(path)
            params = plugin.parameters

            def process(*args, **kwargs):
                for key, value in overrides.items():
                    params[key].raw_value = value
                    if abs(params[key].raw_value - value) > 1e-6:
                        raise RuntimeError(f"{key}: requested {value}, got {params[key].raw_value}")
                return plugin.process(*args, **kwargs)

            return SimpleNamespace(parameters=params, process=process)

        host_globals["pb"] = SimpleNamespace(load_plugin=load_plugin)
    host_render = FunctionType(render.__code__, host_globals)
    host_render.__kwdefaults__ = render.__kwdefaults__
    return np.stack([host_render(x, depth, start=lead) for depth in (0.0, 1.0)]), float(x[0, lead])


def report(case, rate, capture_result):
    (off, full), amplitude = capture_result
    for mode, signal in zip(("Mid", "Side"), HOST["mid_side"]((full - off) / amplitude)):
        def time(index):
            return f"{index:4d}/{1000 * index / rate:9.5f}"

        hits = [np.flatnonzero(np.abs(signal) > 10 ** (d / 20)) for d in THRESHOLDS]
        onset = hits[1][0]
        early_peak = onset + np.argmax(np.abs(signal[onset:onset + round(rate / 1000)]))
        peak = np.argmax(np.abs(signal))
        print(f"{case:19} {rate:5} {mode:4} | "
              + " | ".join(time(h[0]) if h.size else f"{'none':>14}" for h in hits)
              + f" | {time(early_peak)} | {time(peak)} | {HOST['db'](abs(signal[peak])):8.3f}")


def main():
    with HOST["MODULE"].open("rb") as module:
        identity = hashlib.file_digest(module, "sha256").hexdigest()
    print(f"Reference SHA-256 {identity}; Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print("One +6 dB stereo bell, nominal 2400 Hz / Q1 unless noted, dynamics off, zero latency.")
    print("-12 dBFS dual-mono float32 impulse, 100 ms lead, 1 s after input; no latency correction.")
    print("M/S of (Depth 100% minus Depth 0%) / actual input peak. Onsets use strict thresholds.")
    print("All times are samples/ms since input; early peak is max abs in 1 ms from -100 dBr onset.")
    print("Case                Rate  M/S  | Onset -120     | Onset -100     | Onset -80      "
          "| Early abs peak | Global abs peak | Peak dBr")
    baseline = {rate: capture(rate) for rate in RATES}
    for rate, result in baseline.items():
        report("Baseline", rate, result)

    controller = pb.load_plugin(str(HOST["MODULE"]))
    params = controller.parameters
    variants = [(rate, "q", q, float) for q in (0.5, 2.0) for rate in RATES]
    # The controller displays in 10 Hz steps here: 2200 is the nearest to 2205.
    for rate, hz in ((44_100, 2200), (96_000, 4800), (48_000, 800), (48_000, 6400)):
        variants.append((rate, "frequency", hz, hertz))
    for rate, field, target, parse in variants:
        key = f"band_3_{field}"
        raw = control_value(params[key], target, parse)
        prefix = "Q " if field == "q" else ""
        print(f"  {key} = {raw:.9g} ({params[key].string_value})")
        report(prefix + params[key].string_value, rate, capture(rate, overrides={key: raw}))

    print(f"\nBuffer check, baseline bell: max abs L/R error (full scale = 1) vs {BASE_BLOCK},"
          " depth 0% / 100%, and max abs depth-delta error.")
    print("Fixed input position unless noted; edge case moves input to the last sample of a 512 block.")
    for rate, (reference, _) in baseline.items():
        edge_offset = (BASE_BLOCK - 1 - rate // 10) % BASE_BLOCK
        for block, offset in [(b, 0) for b in BLOCKS] + [(BASE_BLOCK, edge_offset)]:
            current, _ = capture(rate, block, lead_offset=offset)
            error = current - reference
            off, full = np.max(np.abs(error), axis=(1, 2))
            delta = np.max(np.abs(error[1] - error[0]))
            print(f"{rate} Hz, {block:4} samples, lead offset {offset:3}: "
                  f"{off:.9g} / {full:.9g}; delta {delta:.9g}")


if __name__ == "__main__":
    main()
