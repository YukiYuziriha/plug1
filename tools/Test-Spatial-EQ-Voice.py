"""Compare static multiband Spatial EQ 0.7.0 against the delay model on local audio.

Suggested fixture: librosa `libri1`, The Ashiel Mystery, read by Garth Comira
(LibriSpeech SLR12, CC BY 4.0). This is mono audiobook speech, not singing.
Download separately to your temp directory; this script never uses the network.
  https://librosa.org/data/audio/5703-47212-0000.ogg
  https://librosa.org/data/audio/5703-47212-0000.txt
Expected SHA-256: a284612b46af0535f7e1873758c4387bb8369f6dbbe192ffdec1f171108f98dd

Sung ensemble fixture: Harmonica sings Tourdion, recorded by Sofie Sigrinn
(CC BY-SA 4.0); use --seconds 40 for the full recording.
  https://commons.wikimedia.org/wiki/File:Harmonica_sings_Tourdion.flac
  https://upload.wikimedia.org/wikipedia/commons/5/5b/Harmonica_sings_Tourdion.flac
Expected SHA-1: bc21be19eed53791b0aae8701c3785a537020b8c

From the worktree root, using the same dependencies as Measure-Spatial-EQ.py:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Test-Spatial-EQ-Voice.py PATH_TO_AUDIO
Add --ab to write one RMS-matched stereo WAV pair to a new system temp directory.
"""

import argparse
import hashlib
import math
from pathlib import Path
from runpy import run_path
import tempfile


TOOLS = Path(__file__).resolve().parent
RATE = 48_000
# Body shelf, low-mid bell, presence bell, air shelf: slot, Hz, dB, Depth, Q, shape.
BANDS = ((1, 200, 3.0, 0.40, 0.8, "low_shelf"), (3, 780, 4.0, 0.65, 1.2),
         (5, 2300, 5.0, 0.85, 1.1), (8, 5700, 3.0, 0.55, 0.8, "high_shelf"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("audio", type=Path, help="local mono or stereo recording (e.g. OGG or WAV)")
    parser.add_argument("--seconds", type=float, default=15.0, help="maximum clip duration (default: 15 s)")
    parser.add_argument("--ab", action="store_true", help="export a local RMS-matched WAV pair")
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or not 0.001 <= args.seconds <= 60:
        parser.error("--seconds must be between 0.001 and 60")
    if not args.audio.is_file():
        parser.error(f"local recording not found: {args.audio}")

    harness = run_path(str(TOOLS / "Test-Spatial-EQ-Multiband.py"))
    np, pb = harness["np"], harness["pb"]
    shelves = harness["SHELVES"]
    from pedalboard.io import AudioFile

    with AudioFile(str(args.audio)) as source:
        channels, native_rate = source.num_channels, source.samplerate
        if channels not in (1, 2):
            parser.error(f"expected mono or stereo; got {channels} channels")
        if native_rate == RATE:
            clip = source.read(round(args.seconds * RATE))
        else:
            with source.resampled_to(RATE) as resampled:
                clip = resampled.read(round(args.seconds * RATE))
    if clip.shape[1] == 0 or not np.isfinite(clip).all() or not np.any(clip):
        parser.error("expected a non-empty, finite, non-silent recording")
    with args.audio.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()

    if channels == 1:
        clip = np.repeat(clip, 2, axis=0)
        layout = "mono duplicated to L/R for the stereo algorithm"
    elif np.array_equal(*clip):
        layout = "two identical input channels (dual mono, not stereo content)"
    else:
        layout = "two distinct input channels (native L/R preserved)"
    # Identical input headroom for both paths; leave fixed silence to capture the return tail.
    trim = 10 ** (-12 / 20) / np.max(np.abs(clip))
    lead, tail = RATE // 10, RATE // 5
    x = np.ascontiguousarray(np.pad(clip * trim, ((0, 0), (lead, tail))), dtype=np.float32)
    dry = x[:, lead:].astype(np.float64)
    print(f"{args.audio.name} | SHA-256 {digest}\n{layout} | {native_rate:g} -> {RATE} Hz | "
          f"{clip.shape[1] / RATE:.3f} s, input peak -12 dBFS (trim {20 * math.log10(trim):+.2f} dB)")
    print(f"Preset (slot, Hz, gain dB, Depth, Q, shape): {BANDS}")
    shelves["identify"]()
    values = harness["settings"](BANDS)
    validate = shelves["validated"]
    set_raw, display_number = (harness["SWEEP"][key] for key in ("set_raw", "display_number"))

    def reference(depth_on):
        plugin = pb.load_plugin(str(harness["MODULE"]))
        for name, raw in values.items():
            depth = name.endswith("_depth")
            raw = 0.0 if depth and not depth_on else raw
            text = set_raw(plugin.parameters[name], raw)
            if depth and display_number(text, "depth") != round(raw * 100):
                raise RuntimeError(f"{name}: expected {raw:.0%}; got {text!r}")
        audio = plugin.process(x.copy(), RATE, buffer_size=harness["BLOCK"])
        return validate(audio, x, np.float32, "reference")[:, lead:].astype(np.float64)

    model = run_path(str(TOOLS / "Spatial-EQ-DelayModel.py"))["process_bands"]
    off = tuple((slot, hz, gain, 0.0, *extra) for slot, hz, gain, _, *extra in BANDS)
    ref_off, ref_full = reference(False), reference(True)
    pred_off, pred_full = (validate(model(x, bands, sample_rate=RATE), x, np.float64, "model")[:, lead:]
                          for bands in (off, BANDS))
    rms, peak, db, mid_side = (shelves["HOST"][key] for key in ("rms", "peak", "db", "mid_side"))

    print("Stats include the 200 ms post-roll, not the 100 ms lead. No alignment or level correction in metrics.")
    print(f"Full output gain vs dry (pooled L/R RMS): ref {db(rms(ref_full) / rms(dry)):+.3f} dB; "
          f"model {db(rms(pred_full) / rms(dry)):+.3f} dB.")
    print("Levels/residual in dBFS; gain = model minus ref RMS in dB; M=(L+R)/2, S=(L-R)/2.")
    print("render       lane   ref RMS  ref peak  mdl RMS  mdl peak  err RMS  err peak       gain")
    for label, ref, pred in (("full", ref_full, pred_full), ("Depth off", ref_off, pred_off),
                             ("Depth added", ref_full - ref_off, pred_full - pred_off)):
        for lane, a, b in zip(("L/R", "M", "S"), (ref, *mid_side(ref)), (pred, *mid_side(pred))):
            ar, br = rms(a), rms(b)
            levels = (ar, peak(a), br, peak(b), rms(b - a), peak(b - a))
            gain = f"{db(br / ar):+.6f}" if ar > 0 else "n/a"
            print(f"{label:12} {lane:4} " + " ".join(f"{db(z):8.2f}" for z in levels) + f" {gain:>11}")

    if args.ab:
        if min(rms(ref_full), rms(pred_full)) == 0:
            parser.error("cannot RMS-match a silent render")
        match = rms(ref_full) / rms(pred_full)
        matched = pred_full * match
        pad = min(1.0, 10 ** (-1 / 20) / max(peak(ref_full), peak(matched)))
        directory = Path(tempfile.mkdtemp(prefix="spatial-eq-voice-ab-"))
        for name, signal in (("reference", ref_full), ("model", matched)):
            with AudioFile(str(directory / f"{name}.wav"), "w", RATE, 2, bit_depth=24) as output:
                output.write((signal * pad).astype(np.float32))
        print(f"A/B WAVs: {directory}\nModel match {db(match):+.3f} dB; shared trim {db(pad):+.3f} dB. "
              "RMS matched, not perceptual loudness matched. No listening performed.")


if __name__ == "__main__":
    main()
