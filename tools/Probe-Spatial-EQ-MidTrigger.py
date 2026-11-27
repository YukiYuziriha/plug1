"""Headless Mid trigger probe; all audio and fits stay in memory.

Run from the worktree root using the existing reference fixture/dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-MidTrigger.py
"""

import hashlib
from pathlib import Path
from runpy import run_path
from time import perf_counter


ENG = run_path(str(Path(__file__).with_name('Fit-Spatial-EQ-Tail-Engagement.py')))
CTX = ENG['CTX']
np, capture, samples, amp, relative = (
    CTX[k] for k in ('np', 'capture', 'ms_samples', 'amp', 'relative')
)
RATE, LEAD, BLOCK = (CTX[k] for k in ('RATE', 'LEAD', 'BLOCK'))
bandpass, ewma = ENG['bandpass'], ENG['ewma']
ON = samples(60) + 37
N = samples(1.25)
EARLY = slice(ON, ON + N)
END = ON + samples(16)
SPECS = (('near/on', 2400, .5, 0), ('near/off', 6000, .5, 0),
         ('far/on', 2400, 3.5, 0), ('far/off', 6000, 3.5, 0),
         ('near/on 90°', 2400, .5, np.pi / 2))
DEV = 2  # far/on: conditioning packet is already contributing to the old Mid tail
COARSE = np.arange(-66, 0, 3)
LIMIT_SECONDS = 100


def peak(x):
    return np.max(np.abs(x), initial=0)


def gain(y, old):
    return -np.dot(y, old) / np.dot(old, old)


def history(hz, gap_ms, phase):
    # A common earlier click supplies a measurable baseline tail. The packet
    # swappable part gets exactly the same energy per 1 ms slice, not just
    # over the whole burst. Instantaneous carrier power is not matched.
    mono = np.zeros(END, dtype=np.float32)
    mono[ON - samples(11)] = amp(-24)
    n = samples(4)
    phase_grid = np.arange(n) * (2 * np.pi * hz / RATE) + phase
    envelope = np.hanning(n + 2)[1:-1]
    packet = (envelope * np.cos(phase_grid)).reshape(4, -1)
    target = envelope.reshape(4, -1)
    packet *= np.linalg.norm(target, axis=1, keepdims=True) / np.linalg.norm(packet, axis=1, keepdims=True)
    stop = ON - samples(gap_ms)
    mono[stop - n:stop] = amp(-30) * packet.ravel()
    return np.stack((mono, mono))


class Host:
    def __init__(self, deadline):
        self.deadline = deadline
        self.count = 0
        self.pre = self.off = self.isolated = 0.
        self.arrival = np.inf

    def render(self, x, **kw):
        pair = []
        for depth in (0., 1.):
            if perf_counter() > self.deadline:
                raise RuntimeError(f'{LIMIT_SECONDS}s probe budget exhausted')
            pair.append(capture(x, depth, **kw).mean(axis=0))
            self.count += 1
        off, full = pair
        return off, full - off

    def click(self, level, **kw):
        b = np.zeros((2, END), dtype=np.float32)
        b[:, ON] = amp(level)
        isolated = [self.render(sign * b, **kw) for sign in (1, -1)]
        for _, delta in isolated:
            floor = 1e-5 * b[0, ON]
            arrivals = np.flatnonzero(np.abs(delta[ON:]) > floor)
            if not arrivals.size:
                raise RuntimeError('No measurable isolated Mid arrival')
            self.arrival = min(self.arrival, arrivals[0])
            self.isolated = max(self.isolated, peak(delta[EARLY]) / b[0, ON])
        return b, isolated

    def interaction(self, a, baseline, hit, **kw):
        b, isolated = hit
        aoff, old = baseline
        traces = []
        for sign, (boff, new) in zip((1, -1), isolated):
            off, delta = self.render(a + sign * b, **kw)
            e = delta - old - new
            self.pre = max(self.pre, peak(e[:ON]))
            self.off = max(self.off, peak(off - aoff - boff))
            traces.append(e[EARLY])
        plus, minus = traces
        return (plus + minus) / 2, (plus - minus) / 2


def power(x):
    return x[0].astype(float) ** 2  # dual mono, per channel


def detectors(a, b):
    ap, bp = power(a[:, :ON]), power(b[:, EARLY])
    af, bf = power(bandpass(a[:, :ON])), power(bandpass(b[:, EARLY]))
    recent = ewma(ap, 2)[-1]
    band = ewma(af, 2)[-1]
    prior = np.array([ap[-samples(12):].mean(), recent, band])
    triggered = np.stack([ewma(bp, .45), ewma(bp, .45), ewma(bf, .45)])
    return prior, 10 * np.log10(triggered / prior[:, None])


def fit_candidates(old, target, ratios):
    # Shape/center and optional exponential recovery are fitted on coarse
    # development levels only. Each amplitude has the same old-tail normalization.
    # The ramp has hard off/plateau shoulders; the Hill has neither.
    t = np.arange(N) * 1000 / RATE
    centers = np.arange(-54, 55, 1.)
    best = {}
    for detector, x in zip(('BB12', 'BB2', 'BP2'), ratios):
        z = x[None, :, :] - centers[:, None, None]
        for curve, widths in (('ratio', (1.,)), ('Hill', (.5, 1., 2., 4., 8.)),
                              ('clamp', (.25, .5, 1., 2., 4., 8., 16., 32.))):
            for width in widths:
                gate = transfer(z, curve, width)
                for tau in (np.inf, .2, .4, .75, 1.5, 3.):
                    # Recovery is tested separately from static transfer on the
                    # same detector; it need not describe a physical decay.
                    pred = -old * gate * np.exp(-t / tau)
                    dot = np.sum(pred * target, axis=(1, 2))
                    norm = np.sum(pred ** 2, axis=(1, 2))
                    scale = np.clip(np.divide(dot, norm, out=np.zeros_like(dot), where=norm > 0), 0, 1)
                    loss = scale * (scale * norm - 2 * dot)  # up to constant ||target||²
                    i = np.argmin(loss)
                    key = detector, curve, 'static' if np.isinf(tau) else 'recover'
                    if key not in best or loss[i] < best[key][0]:
                        best[key] = loss[i], centers[i], width, tau, scale[i]
    return best


def transfer(z, curve, width):
    if curve == 'clamp':
        return np.clip(.5 + z / width, 0, 1)
    return 1 / (1 + 10 ** (-z * width / 10))


def report_models(data, levels, fine):
    old, target, ratio = data[DEV]
    train = np.isin(levels, COARSE)
    interp = np.isin(levels, fine) & ~train
    models = fit_candidates(old, target[train], ratio[:, train])
    print('\nPredict even Mid E = -measured old Mid tail × gate. Fit far/on coarse only.')
    print('BB12: mean 12 ms prior; BB2/BP2: 2 ms broadband/2.4 kHz Q=1 prior EWMA; '
          'all use causal .45 ms hit EWMA in matching band. Ratio=Hill slope 1.')
    print('Scores: per-case old-tail normalized, residual/target dBr; lower is better.')
    print('det  curve  clock      center slope/width  tau ms  max %      fit    fine  near/on near/off  far/off     +90°')
    for (detector, curve, clock), (_, center, width, tau, scale) in models.items():
        index = ('BB12', 'BB2', 'BP2').index(detector)
        scores = []
        checks = [(data[DEV], train), (data[DEV], interp)]
        checks.extend((case, slice(None)) for i, case in enumerate(data) if i != DEV)
        for (tail, even, rat), mask in checks:
            pred = -tail * scale * transfer(rat[index, mask] - center, curve, width)
            pred *= np.exp(-np.arange(N) * 1000 / RATE / tau)
            scores.append(relative((even[mask] - pred).ravel(), even[mask].ravel()))
        print(f'{detector:4} {curve:6} {clock:7} {center:8.0f} {width:11.2f} {tau:7.2f} {100*scale:6.1f}'
              + ''.join(f' {value:8.2f}' for value in scores))


def main():
    start = perf_counter()
    host = Host(start + LIMIT_SECONDS)
    with CTX['HOST']['MODULE'].open('rb') as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {CTX['pb'].__version__}, NumPy {np.__version__}")
    print(f'{RATE} Hz, +6 dB / 2.4 kHz / Q=1, dynamics off, {BLOCK}-sample blocks, '
          f'{LEAD}-sample lead; B sample {ON} (block offset {(LEAD + ON) % BLOCK}).')
    print('Both inputs dual mono, Mid only. A: -24 dBFS click at -11 ms + four 1 ms '
          'energy-matched Hann packet slices; B: signed single sample, per-channel peak dBFS.')
    print('E±=Delta(A±B)-Delta(A)-Delta(±B), Delta=Depth100-Depth0; score [0,1.25) ms.')
    histories = [history(*spec[1:]) for spec in SPECS]
    base = [host.render(a) for a in histories]
    hits = {level: host.click(level) for level in COARSE}
    pairs = [{level: host.interaction(a, prior, hit) for level, hit in hits.items()}
             for a, prior in zip(histories, base)]
    # Refine where the coarse reference trace changes fastest. The resulting
    # interleaved levels are withheld from curve fits (not independent histories).
    old = base[DEV][1][EARLY]
    coarse_g = np.array([gain(pairs[DEV][level][0], old) for level in COARSE])
    i = np.argmax(np.abs(np.diff(coarse_g)))
    fine = np.arange(COARSE[i] - 1, COARSE[i + 1] + 1.01, .25)
    levels = np.union1d(COARSE, fine)
    print(f'\nFine grid: {fine[0]:.2f} to {fine[-1]:.2f} dBFS by 0.25, around largest far/on coarse step.')
    for level in np.setdiff1d(fine, COARSE):
        hits[level] = host.click(level)
        for a, prior, traces in zip(histories, base, pairs):
            traces[level] = host.interaction(a, prior, hits[level])

    # Both timings have the same energies at their own slice timestamps.
    energies, data, gains = [], [], []
    print('\nHistory       Hz gap ms  1 ms slice energy (FS²-samples)                 prior BB12 / BB2 / BP2 dBFS')
    for (name, hz, gap, _), a, (_, delta), traces in zip(SPECS, histories, base, pairs):
        stop = ON - samples(gap)
        energies.append(power(a[:, stop - samples(4):stop]).reshape(4, -1).sum(axis=1))
        prior, unit_ratio = detectors(a, hits[levels[0]][0])
        ratios = unit_ratio[:, None, :] + (levels - levels[0])[None, :, None]
        print(f'{name:12} {hz:4} {gap:6.2f}  ' + ' '.join(f'{e:.8f}' for e in energies[-1])
              + '       ' + ' / '.join(f'{p:6.2f}' for p in 10 * np.log10(prior)))
        old = delta[EARLY]
        norm = np.linalg.norm(old)
        if norm <= 1e-6:
            raise RuntimeError('Prior Mid tail below usable level')
        even, odd = np.array([traces[level] for level in levels]).transpose(1, 0, 2)
        gains.append(np.array([gain(y, old) for y in even]))
        data.append((old / norm, even / norm, ratios))
        print(f'  old-tail RMS {20*np.log10(norm / np.sqrt(N)):.1f} dBFS, '
              f'pooled odd/even {relative(odd.ravel(), even.ravel()):.1f} dBr, '
              f'constant gain fit/even {relative((even + gains[-1][:, None] * old).ravel(), even.ravel()):.1f} dBr')
    if not np.allclose(energies, energies[0], rtol=1e-6, atol=0):
        raise RuntimeError('Slice energy match failed')

    print('\nUnclipped effective suppression (%) by history; positive means a smaller old tail.')
    print('B dBFS' + ''.join(f'{spec[0]:>13}' for spec in SPECS))
    for level, row in zip(levels, np.array(gains).T):
        print(f'{level:6.2f}' + ''.join(f'{100*g:13.3f}' for g in row))
    print('\nTime-resolved suppression (%), far/on; successive 10-sample bins (~0.208 ms).')
    diagnostic_levels = (fine[0], COARSE[i], COARSE[i + 1], -24, COARSE[-1])
    for level in dict.fromkeys(diagnostic_levels):
        old, _, _ = data[DEV]
        y = pairs[DEV][level][0] / np.linalg.norm(base[DEV][1][EARLY])
        print(f'{level:6.2f}' + ''.join(f'{100*gain(y[j:j+10], old[j:j+10]):10.3f}' for j in range(0, N, 10)))
    print()
    print("Low-level far/off check (even interaction vs numerical/control floor):")
    for level in (-60, -54, -48):
        even, odd = pairs[3][level]
        print(f'  {level:3} dBFS: peak even {peak(even):.3g} FS; peak odd {peak(odd):.3g} FS')
    report_models(data, levels, fine)

    print('\nRepeat/scale controls at two far/on knee levels: max even/odd FS difference after undoing scale.')
    for level in COARSE[i:i + 2]:
        a = histories[DEV]
        for name, kw, shift in (('reset', {}, 0), ('fresh', {'fresh': True}, 0),
                                ('lead+49/block127', {'lead': LEAD + 49, 'block': 127}, 0),
                                ('joint -12 dB', {}, -12)):
            aa = a * np.float32(amp(shift))
            e = host.interaction(aa, host.render(aa, **kw), host.click(level + shift, **kw), **kw)
            diff = peak(np.asarray(e) / amp(shift) - pairs[DEV][level])
            print(f'  {level:6.1f} {name:18} {diff:.3g} FS')
            if shift == 0 and diff > 1e-7:
                raise RuntimeError('Repeat/host scheduling control disagreed')
    print('Fresh repeat at -24 dBFS for each history and at -60 dBFS for the low-level far/off check:')
    for j, level in [(j, -24) for j in range(len(SPECS))] + [(3, -60)]:
        a = histories[j]
        e = host.interaction(a, host.render(a, fresh=True), host.click(level, fresh=True), fresh=True)
        diff = peak(np.asarray(e) - pairs[j][level])
        print(f'  {SPECS[j][0]:12} {level:3} dBFS: {diff:.3g} FS')
        if diff > 1e-8:
            raise RuntimeError('Fresh history repeat disagreed')
    print(f'Controls: earliest isolated Mid > -100 dB re click: {1000*host.arrival/RATE:.3f} ms; '
          f'early isolated peak / click {host.isolated:.3g}; pre-hit E {host.pre:.3g} FS; '
          f'Depth-0 E {host.off:.3g} FS. {host.count} renders in {perf_counter()-start:.1f}s.')
    if host.arrival < N or host.isolated > 1e-5 or max(host.pre, host.off) > 1e-6:
        raise RuntimeError('Mid isolation or superposition control failed')


if __name__ == '__main__':
    main()
