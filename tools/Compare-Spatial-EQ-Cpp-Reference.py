"""Measure the standalone C++ SpatialEq against installed Spatial EQ 0.7.0.

Provide a local mono/stereo speech or singing file (no download or install).
Uses the same offline Pedalboard host and controls as the other reference probes.
This is a characterization probe, not a portable CTest or listening test.
"""

import argparse
import hashlib
import math
from pathlib import Path
from runpy import run_path


TOOLS = Path(__file__).resolve().parent
RATE = 48_000
CUTS = ((2, 850, -9, 0.75, 1.4, "bell"),
        (4, 280, -9, 0.50, 0.7, "low_shelf"),
        (7, 4200, -6, 1.00, 1.3, "high_shelf"))
BOOST = (6, 2500, 6, 0.64, 1.1, "bell")
RMS_FLOOR = 1e-8  # Normalized metrics are not informative on near-null lanes.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="built SpatialEqHarness executable")
    parser.add_argument("audio", type=Path, help="local mono/stereo recording")
    parser.add_argument("--seconds", default=2.5, type=float, help="clip duration cap (default 2.5, maximum 10 s)")
    parser.add_argument("--suite", choices=("static", "crossing", "both"), default="both")
    args = parser.parse_args()
    if not 0.8 <= args.seconds <= 10 or not args.audio.is_file() or not args.executable.is_file():
        parser.error("need a local executable and audio file, and --seconds in [0.8, 10]")
    exe = args.executable.resolve()

    shelves = run_path(str(TOOLS / "Test-Spatial-EQ-Shelves.py"))
    host = run_path(str(TOOLS / "Test-Spatial-EQ-MultibandAutomation.py"))
    cpp = run_path(str(TOOLS / "Test-Spatial-EQ-Cpp.py"))
    np, capture = host["np"], host["capture"]
    from pedalboard.io import AudioFile

    shelves["identify"]()
    with AudioFile(str(args.audio)) as source:
        channels, native_rate = source.num_channels, source.samplerate
        if channels not in (1, 2):
            parser.error(f"expected mono/stereo, got {channels} channels")
        if native_rate == RATE:
            clip = source.read(round(args.seconds * RATE))
        else:
            with source.resampled_to(RATE) as resampled:
                clip = resampled.read(round(args.seconds * RATE))
    if clip.shape[1] < 0.8 * RATE or not np.isfinite(clip).all() or not np.any(clip):
        parser.error("need at least 0.8 s of finite, non-silent input")
    with args.audio.open("rb") as audio:
        digest = hashlib.file_digest(audio, "sha256").hexdigest()
    stereo = np.repeat(clip, 2, axis=0) if channels == 1 else clip
    stereo = (stereo * (10 ** (-12 / 20) / np.max(np.abs(stereo)))).astype(np.float32)
    lead, tail = RATE // 10, RATE // 5
    x = np.pad(stereo, ((0, 0), (lead, tail)))
    layout = "dual mono" if np.array_equal(*stereo) else "distinct L/R"
    print(f"{args.audio.name} | SHA-256 {digest}\n{clip.shape[1] / RATE:.3f} s | {native_rate:g} -> {RATE} Hz |"
          f" {layout} | input peak -12 dBFS | 200 ms tail in whole-clip scores", flush=True)
    print(f"Static cuts (slot, Hz, dB, Depth, Q, shape): {CUTS}; mixed/transition companion: {BOOST}")
    print("No alignment or gain correction. Pooled stereo is L/R; M=(L+R)/2, S=(L-R)/2.")
    print("case,window,signal,lane,residual_dBr,error_peak_FS,gain_cpp_minus_ref_dB,crest_ref_dB,crest_cpp_dB", flush=True)

    def lanes(signal):
        return (signal, (signal[0] + signal[1]) / 2, (signal[0] - signal[1]) / 2)

    def rms(signal):
        return np.sqrt(np.mean(signal**2))

    def db(value):
        return f"{20 * math.log10(value):+.6f}" if value > 0 else "-inf"

    def report(label, interval, signal, ref, got):
        for lane, a, b in zip(("L/R", "M", "S"), lanes(ref), lanes(got)):
            ra, rb, err = rms(a), rms(b), b - a
            residual = db(rms(err) / ra) if ra > RMS_FLOOR else "n/a"
            gain = db(rb / ra) if ra > RMS_FLOOR else "n/a"
            cref = db(np.max(np.abs(a)) / ra) if ra > RMS_FLOOR else "n/a"
            ccpp = db(np.max(np.abs(b)) / rb) if rb > RMS_FLOOR else "n/a"
            print(f"{label},{interval},{signal},{lane},{residual},{np.max(np.abs(err)):.6g},{gain},{cref},{ccpp}")

    def compare(label, bands, steps=(), edge=None):
        def pair(config, events):
            return capture(x, RATE, config, events), cpp["render"](exe, RATE, x, config, events)

        ref_on, got_on = pair(bands, steps)
        ref_off, got_off = pair(*cpp["depth_off"](bands, steps))
        for signal, ref, got in (("full", ref_on, got_on), ("Depth off", ref_off, got_off),
                                 ("Depth delta", ref_on - ref_off, got_on - got_off)):
            report(label, "whole", signal, ref[:, lead:], got[:, lead:])
            if edge is not None:
                report(label, "write + 0..100 ms", signal, ref[:, edge:edge + RATE // 10],
                       got[:, edge:edge + RATE // 10])

    if args.suite in ("static", "both"):
        for band in CUTS:
            compare(f"static {band[-1]}", (band,))
        compare("static mixed", (*CUTS, BOOST))
    if args.suite in ("crossing", "both"):
        # Choose a jointly voiced pre/post interval without relying on a hand-picked
        # timestamp. Nudge to sample phase 11 so the write is between control ticks.
        width = RATE // 10
        candidates = range(lead + 2 * width + 11, x.shape[1] - tail - 2 * width, 3 * 32 * 5)
        write = max(candidates, key=lambda i: min(rms(x[:, i - width:i]), rms(x[:, i:i + width])))
        print(f"Gain write at input sample {write} including lead ({(write - lead) / RATE:.6f} s into clip);"
              f" post-write input RMS {db(rms(x[:, write:write + width]))} dBFS", flush=True)
        if rms(x[:, write - width:write]) <= RMS_FLOOR or rms(x[:, write:write + width]) <= RMS_FLOOR:
            parser.error("no gain-write interval with audio on both sides; choose a longer or different clip")
        for slot, hz, _, depth, q, shape in CUTS:
            for gain in (-6, 6):
                band = (slot, hz, gain, depth, q, shape)
                compare(f"{shape} {gain:+} to {-gain:+}", (band, BOOST),
                        ((write, {slot: {"gain_db": -gain}}),), edge=write)


if __name__ == "__main__":
    main()
