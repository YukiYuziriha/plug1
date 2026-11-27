"""Headless black-box probe of the installed Spatial EQ VST3 (no editor).

From the repo root, in dedicated 64-bit PowerShell, install the local host:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -m pip --disable-pip-version-check --no-cache-dir install --only-binary=:all: --target .cache/spatial-eq-reference/deps pedalboard==0.9.26 numpy==2.5.5
Then run (append --inventory for controls or --fingerprint for the extended probe):
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python tools/Measure-Spatial-EQ.py
"""

import argparse
import hashlib
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
# Pedalboard 0.9.26 cannot scan this bundle root on Windows.
PLUGIN = Path(r"C:\Program Files\Common Files\VST3\Spatial EQ.vst3")
MODULE = PLUGIN / "Contents" / "x86_64-win" / "Spatial EQ.vst3"
sys.path.insert(0, str(ROOT / ".cache" / "spatial-eq-reference" / "deps"))

import numpy as np
import pedalboard as pb


def inventory(plugin):
    for key, p in plugin.parameters.items():
        print(f"{key}: {p.raw_value:.9g} ({p.string_value}); {p.range}")


RATE = 48_000
BLOCK = 512
LEAD = RATE // 10
SETTLE = RATE
ANALYZE = 2 * RATE
TONES = (80, 240, 800, 1600, 2400, 3600, 6400, 10000)


def db(value):
    return 20 * np.log10(value) if value > 0 else -np.inf


def rms(audio):
    return np.sqrt(np.mean(audio ** 2))


def peak(audio):
    return np.max(np.abs(audio))


def render(stereo, depth, *, gain_db=6, cut_depth=None, start=LEAD + SETTLE,
           report=False, report_depth=False):
    plugin = pb.load_plugin(str(MODULE))
    params = plugin.parameters
    settings = {f"band_{i}_active": 0.0 for i in range(1, 9)}

    def bell(i, gain, amount):
        # Normalized mappings from this build's controller for 2.40 kHz and Q=1.
        values = dict(active=1.0, enabled=1.0, frequency=0.587483108,
                      q=0.349599957, gain=0.5 + gain / 60, depth=amount,
                      shape=0.0, stereo=0.0, dynamic=0.0, width=0.5,
                      spread=0.0, sidechain=0.0)
        settings.update({f"band_{i}_{key}": value for key, value in values.items()})

    bell(3, gain_db, depth)
    if cut_depth is not None:
        bell(4, -6, cut_depth)
    settings.update(processing=0.0, output=0.5, bypass=0.0, spatial_bypass=0.0,
                    mono_audition=0.0, spatial_bass_protection=0.533953547)
    for key, value in settings.items():
        params[key].raw_value = value
        actual = params[key].raw_value
        if abs(actual - value) > 1e-6:
            raise RuntimeError(f"{key}: requested {value}, got {actual}")
        if report or (depth == 1.0 and key == "band_3_depth" and report_depth):
            print(f"  {key}={actual:.9g} ({params[key].string_value})")
    out = plugin.process(stereo.copy(), RATE, buffer_size=BLOCK)
    if out.shape != stereo.shape or not np.isfinite(out).all():
        raise RuntimeError(f"Unexpected stereo render: {out.shape}")
    # Verify direct arrival for this zero-latency fixture, not a general latency query.
    arrivals = np.flatnonzero(np.max(np.abs(out), axis=0) > 1e-7)
    if not len(arrivals) or arrivals[0] != LEAD:
        raise RuntimeError(f"Expected output onset at {LEAD}, got {arrivals[:1]}")
    return out[:, start:].astype(np.float64)


def multisine():
    t = np.arange(SETTLE + ANALYZE) / RATE
    mono = sum(np.cos(2 * np.pi * f * t + np.pi * i * (i - 1) / len(TONES))
               for i, f in enumerate(TONES)) * 10 ** (-32 / 20)
    return np.pad(mono.astype(np.float32), (LEAD, 0))


def measure():
    mono = multisine()
    stereo = np.stack((mono, mono))
    dry = mono[LEAD + SETTLE:].astype(np.float64)
    print(f"Stereo 2-in/2-out float32, {RATE} Hz, host buffer {BLOCK}; "
          f"lead {LEAD}, settle {SETTLE}, analyzed {ANALYZE} samples")
    print(f"Tones {TONES} Hz, -32 dBFS peak each; "
          f"input RMS {db(rms(dry)):.6f}, peak {db(peak(dry)):.6f} dBFS per channel")
    print("Depth=0 fixture (normalized, display):")
    off = render(stereo, 0.0, report=True)
    print("Depth=100 fixture (only changed control):")
    full = render(stereo, 1.0, report_depth=True)
    print(f"Both direct arrivals at sample {LEAD}; no alignment shift applied.")
    for label, out in (("0%", off), ("100%", full)):
        left, right = out
        mid = (left + right) / 2
        side = (left - right) / 2
        print(f"Depth {label:>4} | L/R/M/S RMS dBFS: "
              f"{db(rms(left)):.6f} {db(rms(right)):.6f} {db(rms(mid)):.6f} {db(rms(side)):.6f} "
              f"| L/R peak dBFS: {db(peak(left)):.6f} {db(peak(right)):.6f}")
    mid_off, mid_full = off.mean(axis=0), full.mean(axis=0)
    residual = mid_full - mid_off
    print(f"Mono residual RMS {db(rms(residual)):.6f} dBFS, "
          f"peak {db(peak(residual)):.6f} dBFS; "
          f"RMS relative to Depth 0 fold {db(rms(residual) / rms(mid_off)):.6f} dB")
    print("Tone Hz | Depth 0 Mid gain vs dry (dB) | Depth 100 vs 0 Mid magnitude (dB) | Depth 100 Side peak at bin (dBFS)")
    dry_fft, off_fft, full_fft, side_fft = (np.abs(np.fft.rfft(sig)) for sig in
                                           (dry, mid_off, mid_full, (full[0] - full[1]) / 2))
    for f in TONES:
        k = f * ANALYZE // RATE
        print(f"{f:7d} | {db(off_fft[k] / dry_fft[k]):+.6f} | "
              f"{db(full_fft[k] / off_fft[k]):+.6f} | {db(2 * side_fft[k] / ANALYZE):.6f}")
    for depth, first in ((0.0, off), (1.0, full)):
        again = render(stereo, depth)
        print(f"Fresh-instance repeat Depth {depth:.0%}: "
              f"max absolute analyzed L/R delta {peak(again - first):.9g}")


def mid_side(stereo):
    left, right = stereo
    return (left + right) / 2, (left - right) / 2


def impulse_fingerprint():
    edges_ms = (0, 1, 5, 10, 20, 50, 100, 250, 500, 1000)
    edges = [ms * RATE // 1000 for ms in edges_ms]
    print(f"\n+6 dB bell: dual-mono impulses at n={LEAD}; {edges_ms[-1]} ms observation")
    print("Delta = Depth 100% - Depth 0%; onset strictly above -100 dB relative to input peak.")
    print("Energy = sum of squared samples; tail percentages use total observed delta energy.")
    normalized = []
    for input_db in (-36, -12):
        mono = np.zeros(LEAD + edges[-1], dtype=np.float32)
        mono[LEAD] = 10 ** (input_db / 20)
        amplitude = float(mono[LEAD])
        stereo = np.stack((mono, mono))
        off = render(stereo, 0.0, start=LEAD)
        full = render(stereo, 1.0, start=LEAD)
        delta = mid_side(full - off)
        normalized.append(np.asarray(delta) / amplitude)
        for label, signal in zip(("Mid", "Side"), delta):
            hits = np.flatnonzero(np.abs(signal) > amplitude * 1e-5)
            k_peak = np.argmax(np.abs(signal))
            onset = f"{hits[0]} samples ({hits[0] * 1000 / RATE:.3f} ms)" if hits.size else "none"
            power = signal ** 2
            total = np.sum(power)
            print(f"Input {input_db:+} dBFS | delta {label} onset {onset}; "
                  f"peak at {k_peak} samples ({k_peak * 1000 / RATE:.3f} ms), "
                  f"{db(peak(signal)):.3f} dBFS ({db(peak(signal) / amplitude):+.3f} dBr); "
                  f"energy/input {db(np.sqrt(total) / amplitude):+.3f} dB")
            print("  Window ms | energy in window (%) | remaining after window (%)")
            for lo, hi, a, b in zip(edges_ms, edges_ms[1:], edges, edges[1:]):
                window = 100 * np.sum(power[a:b]) / total if total else 0.0
                remaining = 100 * np.sum(power[b:]) / total if total else 0.0
                print(f"  {lo:3d}-{hi:<4d} | {window:12.6g} | {remaining:12.6g}")
    quiet, loud = normalized
    for label, a, b in zip(("Mid", "Side"), quiet, loud):
        print(f"Level-normalized {label} delta difference: "
              f"{db(rms(b - a) / rms(a)):.3f} dB relative to quiet delta RMS")


def tone_bin(audio, hz=2400):
    return np.fft.rfft(audio)[hz * len(audio) // RATE]


def bands_fingerprint():
    mono = multisine()
    dry = mono[LEAD + SETTLE:].astype(np.float64)
    dry_bin = tone_bin(dry)
    dual_mono = np.stack((mono, mono))
    pure_side = np.stack((mono, -mono))
    depths = (0.0, 0.5, 1.0)
    print(f"\nSteady multisine: {TONES} Hz at -32 dBFS peak each, settled {SETTLE} samples, "
          f"analyzed {ANALYZE} samples")
    print("Single bell: compare identical L/R and opposite-polarity L/R inputs, same level.")
    print("Gain | Depth | 2.4 kHz Mid gain (dual mono) | 2.4 kHz Side gain (pure Side) | "
          "generated Side RMS dBFS (dual mono) | Mid residual dBr vs same gain at Depth 0")
    cut = {}
    for gain in (6, 0, -6):
        for depth in depths:
            mid, generated = mid_side(render(dual_mono, depth, gain_db=gain))
            _, side = mid_side(render(pure_side, depth, gain_db=gain))
            if depth == 0:
                mid_off = mid
            if gain == -6:
                cut[depth] = (mid, side)
            print(f"{gain:+3d} dB | {depth:4.0%} | {db(abs(tone_bin(mid) / dry_bin)):+.6f} | "
                  f"{db(abs(tone_bin(side) / dry_bin)):+.6f} | {db(rms(generated)):.6f} | "
                  f"{db(rms(mid - mid_off) / rms(mid_off)):.6f}")

    cut_off, side_off = cut[0.0]
    cut_half, side_half = cut[0.5]
    cut_full, side_full = cut[1.0]
    print("Isolated -6 dB cut, max absolute sample differences (full scale = 1):")
    print(f"  Mid at 50% vs arithmetic mean of Mid at 0% and 100%: "
          f"{peak(cut_half - (cut_off + cut_full) / 2):.9g}")
    print(f"  Mid at 100% vs dry: {peak(cut_full - dry):.9g}")
    print(f"  Pure Side at 50% / 100% vs 0%: "
          f"{peak(side_half - side_off):.9g} / {peak(side_full - side_off):.9g}")

    print("\nOverlap: band 3 +6 dB / Depth 100%; band 4 -6 dB at the same frequency and Q.")
    print("Dual mono; all dB changes relative to band 3 alone.")
    print("Cut Depth | 2.4 kHz Mid magnitude | 2.4 kHz Side magnitude | Side RMS | Mid residual RMS dBr")
    ref_mid, ref_side = mid_side(render(dual_mono, 1.0))
    for depth in depths:
        mid, side = mid_side(render(dual_mono, 1.0, cut_depth=depth))
        print(f"{depth:4.0%} | {db(abs(tone_bin(mid) / tone_bin(ref_mid))):+.6f} | "
              f"{db(abs(tone_bin(side) / tone_bin(ref_side))):+.6f} | "
              f"{db(rms(side) / rms(ref_side)):+.6f} | {db(rms(mid - ref_mid) / rms(ref_mid)):.6f}")


def fingerprint():
    print(f"Stereo float32 at {RATE} Hz, host buffer {BLOCK}, {LEAD} samples of silence before input.")
    print("Bell(s): 2.40 kHz, Q 1.00, Stereo, dynamics off, Width 100%, Spread 0%. "
          "Zero Latency mode, trim 0 dB, bass protection 120 Hz, no bypass or mono audition.")
    impulse_fingerprint()
    bands_fingerprint()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--inventory", action="store_true")
    modes.add_argument("--fingerprint", action="store_true")
    args = parser.parse_args()
    print(f"{MODULE}\nPedalboard {pb.__version__} | NumPy {np.__version__}")
    # The installed module manifest uses trailing commas, so read the version field only.
    manifest = (PLUGIN / "Contents" / "Resources" / "moduleinfo.json").read_text(encoding="utf-8")
    version = re.search(r'"Version"\s*:\s*"([^\"]+)"', manifest)
    print(f"Bundle version {version[1] if version else 'not found'} (moduleinfo.json)")
    with MODULE.open("rb") as binary:
        print(f"SHA-256 {hashlib.file_digest(binary, 'sha256').hexdigest()}")
    if args.inventory:
        inventory(pb.load_plugin(str(MODULE)))
    elif args.fingerprint:
        fingerprint()
    else:
        measure()


if __name__ == "__main__":
    main()
