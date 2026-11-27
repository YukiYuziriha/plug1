"""Offline, black-box LTI probe; predictions and fitted coefficients stay in memory.

From the repository root (uses the measurement helper's existing local deps):
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-LTI.py
"""

import hashlib
from pathlib import Path
from runpy import run_path
from types import FunctionType


# Use the fingerprint's full render (start=LEAD), not the steady-state slice.
HOST = run_path(str(Path(__file__).with_name("Measure-Spatial-EQ.py")))
np, render = HOST["np"], HOST["render"]
RATE, LEAD, BLOCK = (HOST[key] for key in ("RATE", "LEAD", "BLOCK"))
TRAIN_LEVELS = (-36, -12)
TEST_LEVELS = (-36, -18, -6)
IR_LENGTH = RATE


def energy(x):
    return np.sum(np.square(x), axis=-1)


def db_power(x):
    with np.errstate(divide="ignore", invalid="ignore"):
        return 10 * np.log10(x)


def error_db(actual, predicted):
    with np.errstate(divide="ignore", invalid="ignore"):
        return db_power(energy(actual - predicted) / energy(actual))


def ms(audio):
    return np.asarray(HOST["mid_side"](audio))


def ms_matrix(h):
    # h[output, input, time]; inverse of the 1/2-scaled M/S encoder is 2*encoder.
    return ms(ms(h.swapaxes(0, 1)).swapaxes(0, 1)) * 2


def fft_length(n):
    return 1 << (n - 1).bit_length()


def predict(h, x):
    n = fft_length(h.shape[-1] + x.shape[-1] - 1)
    spectra = np.fft.rfft(h, n) * np.fft.rfft(x, n)[None, :, :]
    return np.fft.irfft(spectra.sum(axis=1), n)[:, :x.shape[-1]]


def capture(audio, depth, *, onset=0):
    host_render = render
    if onset:
        # Adjust only the helper's onset assertion for this isolated delayed
        # input. Keep the same host settings and return the original time axis.
        host_render = FunctionType(render.__code__, dict(render.__globals__, LEAD=LEAD + onset))
        host_render.__kwdefaults__ = render.__kwdefaults__
    return host_render(np.pad(audio, ((0, 0), (LEAD, 0))), depth, start=LEAD)


def impulses(level, depth, *, shifted=False):
    # With a second impulse just before a host-block boundary, test timing while
    # preserving the helper's initial onset check. Leave >1 s for the first tail.
    delay = IR_LENGTH
    delay += (BLOCK - 1 - LEAD - delay) % BLOCK
    start = delay if shifted else 0
    columns = []
    for channel in range(2):
        x = np.zeros((2, start + IR_LENGTH), dtype=np.float32)
        x[channel, 0] = x[channel, start] = 10 ** (level / 20)
        columns.append(capture(x, depth)[:, start:] / float(x[channel, start]))
    return np.stack(columns, axis=1)


def bandpass(q):
    """Unit-L2 atom: standard 2.4 kHz constant-peak-gain digital bandpass."""
    w = 2 * np.pi * 2400 / RATE
    alpha = np.sin(w) / (2 * q)
    a1, a2 = -2 * np.cos(w) / (1 + alpha), (1 - alpha) / (1 + alpha)
    b = alpha / (1 + alpha)
    atom = np.zeros(RATE * 32 // 1000)
    atom[0] = b
    for i in range(1, len(atom)):
        atom[i] = -a1 * atom[i - 1] - (a2 * atom[i - 2] if i >= 2 else 0)
        if i == 2:
            atom[i] -= b
    return atom / np.linalg.norm(atom)


def sparse_delta(delta_ms):
    """Look for a few delayed direct/bandpass atoms per Depth-delta M/S route."""
    atom_bank = np.stack([bandpass(q) for q in (0.5, 1.0, 2.0)])
    direct = np.zeros((1, atom_bank.shape[-1]))
    direct[0, 0] = 1
    atoms = np.concatenate((direct, atom_bank))
    delay_count = RATE * 150 // 1000 + 1
    support = delay_count + atoms.shape[-1] - 1
    n = fft_length(support + atoms.shape[-1] - 1)
    matched = np.fft.rfft(atoms, n).conj()
    model = np.zeros_like(delta_ms)
    for route in np.ndindex(delta_ms.shape[:2]):
        target = delta_ms[route][:support]
        residual = target.copy()
        chosen, columns = [], []
        for _ in range(8):
            score = np.abs(np.fft.irfft(matched * np.fft.rfft(residual, n), n)[:, :delay_count])
            for atom, delay in chosen:
                score[atom, delay] = 0
            atom, delay = np.unravel_index(np.argmax(score), score.shape)
            if score[atom, delay] == 0:
                break
            chosen.append((atom, delay))
            columns.append(np.pad(atoms[atom], (delay, support - delay - atoms.shape[-1])))
            design = np.column_stack(columns)
            fitted = design @ np.linalg.lstsq(design, target, rcond=None)[0]
            residual = target - fitted
            model[route][:support] = fitted
    return model


def speech_like():
    t = np.arange(2 * RATE) / RATE
    rng = np.random.default_rng(19073)
    voice = np.zeros_like(t)
    for start, duration, f0, formants in (
        (0.00, 0.25, 131, (700, 1250, 2550)),
        (0.34, 0.32, 162, (390, 1950, 2690)),
        (0.79, 0.22, 142, (540, 910, 2410)),
        (1.17, 0.38, 153, (480, 1550, 2850)),
        (1.66, 0.23, 121, (610, 1050, 2530)),
    ):
        begin, length = round(start * RATE), round(duration * RATE)
        local = t[:length]
        phase = 2 * np.pi * (f0 * local + 11 * local ** 2)
        syllable = np.zeros(length)
        for k in range(1, 32):
            weight = sum(np.exp(-0.5 * ((k * f0 - f) / w) ** 2)
                         for f, w in zip(formants, (140, 240, 330)))
            syllable += (0.15 + weight) / np.sqrt(k) * np.cos(k * phase)
        noise = rng.standard_normal(length + 1)
        syllable += 0.12 * np.diff(noise)
        attack = 0.001 if begin == 0 else 0.014
        gate = np.sin(np.pi / 2 * np.minimum((local + 1 / RATE) / attack, 1)) ** 2
        gate *= np.minimum((duration - local) / 0.045, 1) ** 2
        voice[begin:begin + length] = gate * syllable
    return np.stack((voice, voice))


def decorrelated():
    rng = np.random.default_rng(72313)
    t = np.arange(2 * RATE) / RATE
    f = np.fft.rfftfreq(len(t), 1 / RATE)
    channels = []
    for rate, low, high, tone in ((2.9, 90, 8500, 330), (3.7, 170, 11500, 523)):
        spectrum = np.fft.rfft(rng.standard_normal(len(t)))
        spectrum *= (f / (f + low)) / np.sqrt(1 + (f / 650) ** 2) / (1 + (f / high) ** 4)
        body = np.fft.irfft(spectrum, len(t))
        body += 0.17 * np.cos(2 * np.pi * tone * t + 1.2 * np.sin(2 * np.pi * 1.3 * t))
        gate = np.minimum((t + 1 / RATE) / 0.001, 1) * np.minimum((2 - t) / 0.04, 1)
        channels.append(body * gate * (0.18 + 0.82 * np.cos(np.pi * rate * t) ** 4))
    return np.stack(channels)


def pair(values):
    return "/".join("n/a" if np.isnan(v) else f"{v:.2f}" for v in values)


def check_impulse_superposition(h):
    print("\nAdditional impulse-only checks (not fit targets), full Depth, errors M/S:")
    a = np.float32(10 ** (-24 / 20))
    for name, right_start, right_gain in (
        ("Simultaneous L=R", 0, a),
        ("Simultaneous L=-R", 0, -a),
        ("Staggered L then R", 64, a),
        ("Staggered L then R", BLOCK, a),
    ):
        x = np.zeros((2, IR_LENGTH + right_start), dtype=np.float32)
        x[0, 0], x[1, right_start] = a, right_gain
        actual = ms(capture(x, 1.0))
        print(f"  {name}, gap {right_start:3d} samples: "
              f"{pair(error_db(actual, ms(predict(h, x))))} dBr vs fixed IR")
        if right_start:
            left, right = x.copy(), x.copy()
            left[1] = right[0] = 0
            isolated_right = ms(capture(right, 1.0, onset=right_start))
            shifted_error = error_db(isolated_right, ms(predict(h, right)))
            additive_error = error_db(actual, ms(capture(left, 1.0)) + isolated_right)
            print(f"    Isolated R vs fixed IR: {pair(shifted_error)} dBr; "
                  f"pair vs sum of isolated renders: {pair(additive_error)} dBr")
    x = np.zeros((2, IR_LENGTH), dtype=np.float32)
    x[:, 0] = (a, 0)
    print(f"  Single-L polarity inversion: "
          f"{pair(error_db(ms(capture(-x, 1.0)), -ms(capture(x, 1.0))))} dBr")


def score(name, dry, depth0, full, sparse_ms):
    print(f"\n{name}; L/R input correlation {np.corrcoef(dry)[0, 1]:+.4f}")
    print("Peak   Input M/S   Output M/S   Observed E/input E  | Full error M/S       | Depth-delta error M/S")
    print("dBFS   RMS dBFS    RMS dBFS     M/S (dB)             | single -> pooled    | pooled / sparse model")
    normalized_reference = None
    for level in TEST_LEVELS:
        x = (dry * (10 ** (level / 20) / np.max(np.abs(dry)))).astype(np.float32)
        x = np.pad(x, ((0, 0), (0, IR_LENGTH)))
        measured = capture(x, 1.0)
        reference = ms(measured)
        off = ms(capture(x, 0.0))
        delta = reference - off
        normalized = reference * 10 ** (-level / 20)
        if normalized_reference is None:
            normalized_reference = normalized
        single, pooled = (ms(predict(h, x)) for h in full)
        predicted_delta = ms(predict(full[1] - depth0, x))
        sparse_prediction = predict(sparse_ms, ms(x))
        print(f"{level:+4}   {pair(db_power(energy(ms(x)) / x.shape[-1])):>13}   "
              f"{pair(db_power(energy(reference) / x.shape[-1])):>13}   "
              f"{pair(db_power(energy(reference) / energy(ms(x)).sum())):>13}   | "
              f"{pair(error_db(reference, single))} -> {pair(error_db(reference, pooled))}   | "
              f"{pair(error_db(delta, predicted_delta))} / {pair(error_db(delta, sparse_prediction))}")
        if level == TEST_LEVELS[1]:
            zero_depth_error = error_db(off, ms(predict(depth0, x)))
            print(f"       Depth 0 impulse model error {pair(zero_depth_error)} dBr (M/S).")
            repeated = ms(capture(x, 1.0))
            end = dry.shape[-1]
            print(f"       Fresh-instance repeat {pair(error_db(reference, repeated))} dBr; "
                  f"pooled error by region: body {pair(error_db(reference[:, :end], pooled[:, :end]))}, "
                  f"tail {pair(error_db(reference[:, end:], pooled[:, end:]))} dBr (M/S).")
        if level == TEST_LEVELS[-1]:
            print(f"       Level-normalized output difference, {level} vs {TEST_LEVELS[0]} dBFS: "
                  f"{pair(error_db(normalized_reference, normalized))} dBr (M/S).")


def main():
    with HOST["MODULE"].open("rb") as module:
        identity = hashlib.file_digest(module, "sha256").hexdigest()
    print(f"Reference SHA-256 {identity}; Pedalboard {HOST['pb'].__version__}, NumPy {np.__version__}")
    print(f"One stereo bell: +6 dB, 2.4 kHz, Q=1, full Depth, dynamics off; "
          f"{RATE} Hz, {BLOCK}-sample blocks, {LEAD}-sample lead, no alignment shift.")
    train = [impulses(level, 1.0) for level in TRAIN_LEVELS]
    depth0 = np.mean([impulses(level, 0.0) for level in TRAIN_LEVELS], axis=0)
    pooled = np.mean(train, axis=0)
    delta_ms = ms_matrix(pooled - depth0)
    sparse_ms = sparse_delta(delta_ms)
    print(f"One-second L/R delta-basis IRs: single {TRAIN_LEVELS[0]} dBFS; "
          f"pooled equal-weight normalized {TRAIN_LEVELS} dBFS. Holdout audio never used for fitting.")
    print("M/S = (L +/- R)/2. Energy = sum of squares; output/input energy uses total input M+S.")
    print("Errors are dBr = 20 log10(RMS error / RMS reference); "
          "-inf means zero error, n/a means zero error and reference.")
    print("Depth delta is Depth 100% minus Depth 0%. Sparse model fits only this delta: "
          "up to eight delayed direct or 2.4 kHz bandpass atoms per M/S route, delays 0-150 ms.")
    print("Impulse diagnostics, output rows M/S, input columns M/S (each entry dB):")
    holdout = impulses(-24, 1.0)
    for label, actual, model in (
        ("Held-out -24 dBFS vs pooled", holdout, pooled),
        ("Late/block-edge -24 dBFS vs same-level onset", impulses(-24, 1.0, shifted=True), holdout),
    ):
        errors = error_db(ms_matrix(actual), ms_matrix(model))
        print(f"  {label}: {pair(errors[0])}; {pair(errors[1])} dBr")
    errors = error_db(delta_ms, sparse_ms)
    power = db_power(energy(delta_ms))
    print(f"  Depth-delta normalized energy: {pair(power[0])}; {pair(power[1])} dB")
    print(f"  Sparse fit residual on impulse delta: {pair(errors[0])}; {pair(errors[1])} dBr")
    check_impulse_superposition(pooled)
    print("\nHeld-out scores cover 2 s of source plus 1 s of silence (tail).")
    for name, dry in (("Dual-mono speech-like bursts", speech_like()), ("Decorrelated stereo", decorrelated())):
        score(name, dry, depth0, (train[0], pooled), sparse_ms)


if __name__ == "__main__":
    main()
