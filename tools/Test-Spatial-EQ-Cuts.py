"""Measure one static Stereo Bell cut and boost/cut interactions in Spatial EQ 0.7.0.

From the research worktree root, with the Measure-Spatial-EQ.py dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'tools/Invoke-HalfCpu.ps1' -Executable python -CommandArguments @('-B','tools/Test-Spatial-EQ-Cuts.py')"
"""

import hashlib
from pathlib import Path
import re
from runpy import run_path


SWEEP = run_path(str(Path(__file__).with_name("Sweep-Spatial-EQ-Controls.py")))
HOST = SWEEP["HOST"]
np, pb, MODULE, RATE, BLOCK, LEAD = (HOST[k] for k in ("np", "pb", "MODULE", "RATE", "BLOCK", "LEAD"))
DEPTHS = (0.0, 0.5, 1.0)
CENTER, SEPARATED = 2400, 300
SETTLE = RATE // 2


def identify():
    manifest = (HOST["PLUGIN"] / "Contents" / "Resources" / "moduleinfo.json").read_text(encoding="utf-8")
    match = re.search(r'"Version"\s*:\s*"([^"]+)"', manifest)
    if not match or match[1] != "0.7.0":
        raise RuntimeError(f"Expected Spatial EQ 0.7.0: {MODULE}")
    with MODULE.open("rb") as binary:
        digest = hashlib.file_digest(binary, "sha256").hexdigest()
    print(f"{MODULE}\nVersion {match[1]} | SHA-256 {digest} | Pedalboard {pb.__version__} | NumPy {np.__version__}")


def fixtures():
    impulse = np.zeros((2, LEAD + RATE // 5), dtype=np.float32)
    impulse[0, LEAD] = 10 ** (-24 / 20)
    impulse[1, LEAD + 35] = -impulse[0, LEAD] / 2
    yield "asymmetric impulse", impulse, slice(LEAD, None)

    t = np.arange(SETTLE + RATE) / RATE
    tone = sum(np.cos(2 * np.pi * hz * t + phase) for hz, phase in ((SEPARATED, 0), (CENTER, 0.7), (9600, 1.2)))
    mono = np.pad((tone * 10 ** (-32 / 20)).astype(np.float32), (LEAD, 0))
    yield "dual-mono multisine", np.stack((mono, mono)), slice(LEAD + SETTLE, None)


def make_capture():
    base = SWEEP["BASE"]
    probe = pb.load_plugin(str(MODULE))
    frequency = {CENTER: base["frequency"], SEPARATED: SWEEP["raw_for_display"](
        probe.parameters["band_3_frequency"], "frequency", SEPARATED)}

    def capture(x, bands):
        """Fresh render; bands are (index, Hz, gain dB, Depth fraction)."""
        plugin = pb.load_plugin(str(MODULE))
        params = plugin.parameters
        values = {f"band_{i}_active": 0.0 for i in range(1, 9)}
        expected = {}
        for index, hz, gain, depth in bands:
            prefix = f"band_{index}_"
            controls = dict(active=1.0, enabled=1.0, shape=0.0, stereo=0.0, dynamic=0.0,
                            sidechain=0.0, width=0.5, spread=0.0, q=base["q"],
                            frequency=frequency[hz], gain=0.5 + gain / 60, depth=depth)
            values.update({prefix + field: raw for field, raw in controls.items()})
            expected.update({prefix + field: (field, target) for field, target in
                             dict(frequency=hz, gain=gain, q=1.0, depth=100 * depth).items()})
        values.update(processing=0.0, output=0.5, bypass=0.0, spatial_bypass=0.0,
                      mono_audition=0.0, spatial_bass_protection=base["spatial_bass_protection"])
        for name, raw in values.items():
            text = SWEEP["set_raw"](params[name], raw)
            if name in expected:
                field, target = expected[name]
                if SWEEP["display_number"](text, field) != target:
                    raise RuntimeError(f"{name}: expected display {target}, got {text!r}")
        out = plugin.process(x.copy(), RATE, buffer_size=BLOCK)
        if out.shape != x.shape or out.dtype != np.float32 or not np.isfinite(out).all():
            raise RuntimeError(f"Invalid reference render: {out.shape}, {out.dtype}")
        arrivals = np.flatnonzero(np.max(np.abs(out), axis=0) > 1e-7)
        if not arrivals.size or arrivals[0] != LEAD:
            raise RuntimeError(f"Expected arrival at {LEAD}, got {arrivals[:1]}")
        return out.astype(np.float64)

    return capture


def collect(render, x, window):
    """Reusable matrix for a reference or candidate with signature render(x, bands)."""
    def run(*bands):
        return np.asarray(render(x, bands), dtype=np.float64)[:, window]

    cut = {d: run((3, CENTER, -6, d)) for d in DEPTHS}
    repeat = run((3, CENTER, -6, 0.0))
    boost = {i: run((i, CENTER, 6, 1.0)) - run((i, CENTER, 6, 0.0)) for i in (3, 4)}
    pairs = {(hz, i): {(b, d): run((i, CENTER, 6, b), (7 - i, hz, -6, d))
                       for b in (0.0, 1.0) for d in DEPTHS}
             for hz in (CENTER, SEPARATED) for i in (3, 4)}
    return cut, repeat, boost, pairs


def ms(stereo):
    return np.array(HOST["mid_side"](stereo))


def levels(stereo):
    """M, S and stereo-pooled L/R RMS; stereo peak is the max over both lanes."""
    folded = ms(stereo)
    return (np.r_[np.sqrt(np.mean(folded ** 2, axis=1)), np.sqrt(np.mean(stereo ** 2))],
            np.r_[np.max(np.abs(folded), axis=1), np.max(np.abs(stereo))])


def contrasts(pair, depth):
    """Keep total, cut-only and boost-only changes on independent off anchors."""
    both_on = pair[1.0, depth]
    boost_at_depth = both_on - pair[0.0, depth]
    return {"total delta": both_on - pair[0.0, 0.0],
            "cut delta | boost on": both_on - pair[1.0, 0.0],
            "boost delta | cut fixed": boost_at_depth,
            "Depth interaction": boost_at_depth - (pair[1.0, 0.0] - pair[0.0, 0.0])}


def report(label, stereo):
    rms, peak = levels(stereo)
    print(f"  {label:39}  " + " / ".join(f"{v:.3e}" for v in rms)
          + "    " + " / ".join(f"{v:.3e}" for v in peak))


def side_gain(stereo, anchor):
    # The scored tone slice is exactly one second, so its FFT bin index is Hz.
    numerator, denominator = (abs(np.fft.rfft(ms(a)[1])[CENTER]) for a in (stereo, anchor))
    if denominator == 0:
        raise RuntimeError("Missing isolated boost Side anchor at 2.4 kHz")
    return HOST["db"](numerator / denominator)


def measure(label, dry, cut, repeat, boost, pairs, *, steady):
    print(f"\n{label} | RMS M / S / L+R pooled          peak M / S / max(L,R) [FS]")
    report("cut direct (Depth 0 minus dry)", cut[0.0] - dry)
    report("cut fresh Depth 0 repeat", repeat - cut[0.0])
    # Compare a candidate rule constructed only from the measured off render and dry.
    mid_off = ms(cut[0.0])[0]
    direct_mid_deficit = ms(dry)[0] - mid_off
    for depth in DEPTHS[1:]:
        delta = cut[depth] - cut[0.0]
        report(f"isolated cut delta {depth:.0%}", delta)
        report(f"minus Mid-only dry blend {depth:.0%}", delta - depth * direct_mid_deficit)

    for hz in (CENTER, SEPARATED):
        for i in (3, 4):
            pair = pairs[hz, i]
            print(f"\n  Boost slot {i}, cut slot {7 - i} at {hz:g} Hz:")
            report("pair direct (both off minus dry)", pair[0.0, 0.0] - dry)
            for depth in DEPTHS:
                views = contrasts(pair, depth)
                for name, value in views.items():
                    if depth or name not in ("cut delta | boost on", "Depth interaction"):
                        report(f"{name} {depth:.0%}", value)
                if steady:
                    db = side_gain(views["boost delta | cut fixed"], boost[i])
                    print(f"  2.4 kHz generated Side vs isolated boost at cut Depth {depth:.0%}: {db:+.2f} dB")
        print(f"\n  Slot swap at cut {hz:g} Hz (boost slot 4 minus boost slot 3):")
        report("pair direct order residual", pairs[hz, 4][0.0, 0.0] - pairs[hz, 3][0.0, 0.0])
        for depth in DEPTHS:
            a, b = (contrasts(pairs[hz, i], depth) for i in (3, 4))
            report(f"total delta order residual {depth:.0%}", b["total delta"] - a["total delta"])


def verify(reference, model):
    comparisons = []

    def add(label, ref, got):
        comparisons.append((label, ref, got))

    cut, _, boost, pairs = reference
    model_cut, _, model_boost, model_pairs = model
    for depth in DEPTHS:
        add(f"isolated cut full {depth:.0%}", cut[depth], model_cut[depth])
        if depth:
            add(f"isolated cut delta {depth:.0%}", cut[depth] - cut[0], model_cut[depth] - model_cut[0])
    for slot in boost:
        add(f"boost anchor slot {slot}", boost[slot], model_boost[slot])
    for key, pair in pairs.items():
        candidate = model_pairs[key]
        for setting in pair:
            add(f"pair {key} full {setting}", pair[setting], candidate[setting])
        for depth in DEPTHS:
            expected, actual = contrasts(pair, depth), contrasts(candidate, depth)
            for name in expected:
                add(f"pair {key} {name} {depth:.0%}", expected[name], actual[name])

    worst_db, max_rms, max_peak = -np.inf, 0.0, 0.0
    for label, ref, got in comparisons:
        ref_rms, _ = levels(ref)
        rms, peak = levels(got - ref)
        db = np.full_like(rms, -np.inf)
        scored = (ref_rms > 0) & (rms > 1e-8)
        db[scored] = 20 * np.log10(rms[scored] / ref_rms[scored])
        worst_db = max(worst_db, db.max())
        max_rms, max_peak = max(max_rms, rms.max()), max(max_peak, peak.max())
        if not np.isfinite(got).all() or (db > -75).any() or ((ref_rms == 0) & (rms > 1e-8)).any() or peak.max() > 1e-6:
            raise AssertionError(f"{label}: relative error {db}, RMS {rms}, peak {peak}")
    print(f"\n  Model: {len(comparisons)} comparisons passed; worst scored {worst_db:.2f} dBr "
          f"(above 1e-8 FS RMS), max RMS {max_rms:.3e}, max peak {max_peak:.3e} FS.")


def main():
    identify()
    print(f"Stereo float32, {RATE} Hz / {BLOCK} samples, 100 ms silent lead. Bell Q=1, ±6 dB, "
          "width 100%, spread 0%, bass protection 120 Hz, zero latency. No alignment shift.")
    print("Impulse: 200 ms from onset, right delayed 35 samples and inverted at half level. "
          "Multisine: 300/2400/9600 Hz, -32 dBFS per tone; first 500 ms skipped, 1 s measured.")
    print("Depth interaction = change in the boost-only delta as cut Depth moves away from 0%. "
          "'Off' means Depth 0, not band bypass. The separated filters can still have overlapping skirts.")
    capture = make_capture()
    render = run_path(str(Path(__file__).with_name("Spatial-EQ-DelayModel.py")))["process_bands"]
    for label, x, window in fixtures():
        reference = collect(capture, x, window)
        measure(label, x[:, window].astype(np.float64), *reference, steady=(label == "dual-mono multisine"))
        verify(reference, collect(render, x, window))
    print("No audio saved or GUI used.")


if __name__ == "__main__":
    main()
