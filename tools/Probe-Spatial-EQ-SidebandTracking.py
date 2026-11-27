"""Headless sideband tracking of the installed reference; prints metrics only.

From the worktree root (dependencies as in Measure-Spatial-EQ.py):
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-SidebandTracking.py
"""

import hashlib
from itertools import product
from math import gcd
from pathlib import Path
from runpy import run_path
from types import FunctionType, SimpleNamespace


SB = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-Sidebands.py")))
np, pb, capture, centered, ms, amp, db_ratio, fold, tone, spectrum = (
    SB[k] for k in ("np", "pb", "capture", "centered", "ms", "amp", "db_ratio", "fold", "tone", "spectrum")
)
RATE, LEAD, BLOCK, ON, N, START, END, MODULE, LEVELS = (
    SB[k] for k in ("RATE", "LEAD", "BLOCK", "ON", "N", "START", "END", "MODULE", "LEVELS")
)
BED_HZ = (1500, 2000, 2400, 3500)
PROBES = (1712, 2828, 4044)
STEP = RATE // N
ROUTES = (("M", (1, 1)), ("S", (1, -1)))


def plan(f):
    moving = {f"{i}F": i * f for i in SB["ORDERS"]}
    offsets = dict(moving)
    aliases = {}
    for fixed in (4800, 9600):
        alias = next((k for k, v in moving.items() if v == fixed), None)
        if alias:
            aliases[str(fixed)] = alias
        else:
            offsets[str(fixed)] = fixed

    period = gcd(f, RATE)
    signed = (0, *PROBES, *(-p for p in PROBES))
    probe_imd = {fold(sum(mix)) for mix in product(signed, repeat=3)}
    seen = set()
    families, guards = {}, []
    for label, offset in offsets.items():
        hz = []
        for p, sign in product(PROBES, (-1, 1)):
            target = fold(p + sign * offset)
            for at in (target, target - STEP, target + STEP):
                # Residues identify any first-order probe image shifted by any
                # integer bed order, including above Nyquist and the negative branch.
                images = {q for q in PROBES if (at - q) % period == 0 or (at + q) % period == 0}
                expected = {p} if label in moving and at == target else set()
                if (not 0 < at < RATE // 2 or at % STEP or at in seen or at in probe_imd
                        or at % period == 0 or images != expected):
                    raise ValueError(f"F={f}, {label}, probe={p}: contaminated bin at {at} Hz")
                seen.add(at)
            hz.append(target // STEP)
            guards.extend((target // STEP - 1, target // STEP + 1))
        families[label] = np.array(hz)
    return families, np.array(guards), aliases


def processing_modes():
    plugin = pb.load_plugin(str(MODULE))
    param = plugin.parameters["processing"]
    modes = {}
    for raw in (0.0, 0.5, 1.0, 0.25, 0.75):
        param.raw_value = raw
        modes.setdefault(param.string_value, raw)
    if modes.get("Zero Latency") != 0.0:
        raise RuntimeError(f"Unexpected processing modes: {modes}")
    return modes


def mode_capture(name, raw):
    if raw == 0.0:
        return capture
    render = SB["CONTEXT"]["HOST"]["render"]

    def alternate(x, depth, *, fresh):
        if not fresh:
            raise ValueError("Mode probe requires fresh instances")
        host = dict(render.__globals__, LEAD=LEAD)

        def load(path):
            plugin = pb.load_plugin(path)
            params = plugin.parameters

            def process(audio, rate, *, buffer_size):
                params["processing"].raw_value = raw
                if params["processing"].string_value != name:
                    raise RuntimeError(f"Processing mode did not select {name}")
                out = plugin.process(audio, rate, buffer_size=buffer_size, reset=True)
                if params["processing"].string_value != name:
                    raise RuntimeError(f"Processing mode readback changed from {name} during render")
                # The shared fixture's direct-arrival assertion is specific to
                # Zero Latency. Keep its shape/finite checks, without requiring
                # direct arrival or shifting the alternate-mode audio.
                arrivals = np.flatnonzero(np.max(np.abs(out), axis=0) > 1e-7)
                if arrivals.size:
                    host["LEAD"] = int(arrivals[0])
                return out

            return SimpleNamespace(parameters=params, process=process)

        host["pb"] = SimpleNamespace(load_plugin=load)
        configured = FunctionType(render.__code__, host)
        configured.__kwdefaults__ = render.__kwdefaults__
        # Continue the coherent tones beyond the analysis interval so that
        # linear-phase pre-ringing from their final cutoff cannot reach it.
        padded = np.concatenate((np.pad(x, ((0, 0), (LEAD, 0))), x[:, -N:]), axis=1)
        return configured(padded, depth, start=LEAD)[:, :END]

    return alternate


def peak_db(x):
    return float(db_ratio(np.max(np.abs(x)), 1))


def windows(x):
    return np.stack([spectrum(x, start) for start in (START, START + N)])


def main():
    plans = {f: plan(f) for f in BED_HZ}
    with MODULE.open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB bell, 2400 Hz, Q=1, stereo, dynamics off. {RATE} Hz float32, "
          f"{BLOCK} samples/block, {LEAD} samples silent lead; no alignment shift.")
    print(f"Mid-only bed: -24 dBFS peak. Comb {PROBES} Hz starts at {ON / RATE:g} s; "
          f"two adjacent {N / RATE:g} s windows from {START / RATE:g} s ({STEP} Hz bins).")
    print("Each family has six distinct bins (tooth order, minus then plus, folded to positive Hz).")
    for f, (families, _, aliases) in plans.items():
        print(f"  F={f}: " + "; ".join(f"{k}={(bins * STEP).tolist()}" for k, bins in families.items())
              + (f"; fixed aliases {aliases}" if aliases else ""))
    print("J = (Y(bed+eps*comb)-Y(bed-eps*comb))/(2*eps). Delta = J100-J0; E = Delta(bed)-Delta(quiet).")
    print("Family columns show first-window on-route peak |E| in dB re per-tooth input; @ is peak Hz.")
    print("Controls (same dB reference): bed J0 and quiet Delta at targets, E at adjacent guards, "
          "cross-route E at targets, on-route complex window change at targets, and Delta at comb teeth.")
    comb = sum(map(tone, PROBES))
    comb[:ON] = 0
    quiet = np.zeros((2, END))
    carriers = np.array(PROBES) // STEP

    reference = {}
    for name, raw in processing_modes().items():
        mode_diff = {"bed D0": -np.inf, "bed D100": -np.inf,
                     "probe J0": -np.inf, "probe Delta": -np.inf}

        def compare(label, key, x):
            if raw == 0.0:
                reference[label, key] = x
            else:
                mode_diff[label] = max(mode_diff[label], peak_db(x - reference[label, key]))

        print(f"\nProcessing: {name} (raw request {raw:g})")
        get = mode_capture(name, raw)
        # Reuse the shared centered renderer; substitute only the host capture for other modes.
        differentiate = FunctionType(centered.__code__, dict(centered.__globals__, capture=get))
        baselines = {}
        for i, (_, route) in enumerate(ROUTES):
            probe = np.asarray(route)[:, None] * comb
            for level in LEVELS:
                q0, qdelta = differentiate(quiet, probe, amp(level))
                q0, qdelta = windows(q0), windows(qdelta)
                baselines[i, level] = qdelta
                compare("probe J0", (None, i, level), q0)
                compare("probe Delta", (None, i, level), qdelta)
                print(f"  Quiet {ROUTES[i][0]} {level}: max D0 carrier "
                      f"{peak_db(q0[:, i, carriers]):.2f} dB, "
                      f"max depth delta at carrier {peak_db(baselines[i, level][:, i, carriers]):.2f} dB")
        for f, (families, guards, aliases) in plans.items():
            bed = np.broadcast_to(amp(-24) * tone(f), quiet.shape)
            b0, b100 = (ms(get(bed.astype(np.float32), depth, fresh=True)) for depth in (0.0, 1.0))
            compare("bed D0", f, b0[:, START:END].copy())
            compare("bed D100", f, b100[:, START:END].copy())
            settled = max(np.max(np.abs(b[:, START:START + N] - b[:, START + N:END]))
                          for b in (b0, b100))
            print(f"  F={f}: bed-only window change {settled:.3g} FS max; "
                  f"bed-only depth delta {peak_db((b100 - b0)[:, START:END]):.2f} dBFS peak")
            targets = np.concatenate(list(families.values()))
            for i, (route_name, route) in enumerate(ROUTES):
                probe = np.asarray(route)[:, None] * comb
                previous = None
                for level in LEVELS:
                    off, delta = differentiate(bed, probe, amp(level))
                    q = baselines[i, level]
                    off, raw_delta = windows(off), windows(delta)
                    compare("probe J0", (f, i, level), off)
                    compare("probe Delta", (f, i, level), raw_delta)
                    e = raw_delta - q
                    line = e[0, i]
                    stats = {}
                    for k, bins in families.items():
                        best = bins[np.argmax(np.abs(line[bins]))]
                        stats[k] = f"{peak_db(line[bins]):.2f}@{best * STEP}"
                    for fixed, alias in aliases.items():
                        stats[fixed] = f"={alias}"
                    print(f"    {route_name}>{route_name} {level}: "
                          + " ".join(f"{k}:{stats[k]}" for k in (*families.keys(), *aliases.keys())))
                    checks = (peak_db(off[:, i, targets]), peak_db(q[:, i, targets]),
                              peak_db(e[:, i, guards]), peak_db(e[:, 1 - i, targets]),
                              peak_db((e[1, i] - line)[targets]), peak_db(raw_delta[:, i, carriers]))
                    level_change = ("" if previous is None else "; 2F/4F complex level change " +
                                    "/".join(f"{peak_db((line - previous)[families[k]]):.2f}"
                                             for k in ("2F", "4F")))
                    print("      J0/quiet/guard/cross/window/carrier: "
                          + "/".join(f"{v:.2f}" for v in checks) + level_change)
                    previous = line
        if raw != 0.0:
            print("  Max difference vs Zero Latency over the settled sweep: "
                  + "; ".join(f"{k} {v:.2f} {'dBFS peak' if k.startswith('bed') else 'dB re tooth'}"
                              for k, v in mode_diff.items()))


if __name__ == "__main__":
    main()
