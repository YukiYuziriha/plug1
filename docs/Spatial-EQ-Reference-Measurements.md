# Spatial EQ reference measurements

Black-box measurements of the installed **Riverflux Audio Spatial EQ 0.7.0** VST3. These are observations for the fixtures below, not a claim about RFA's implementation. See [transcript notes](Spatial-EQ-RFA-Transcript-Notes.md) for the original claims and caveats. No reference binary, rendered audio, or fitted coefficients are stored in the repo.

## Fixture and reproduction

- Installed module: `C:\Program Files\Common Files\VST3\Spatial EQ.vst3\Contents\x86_64-win\Spatial EQ.vst3`.
- Module SHA-256: `058b89a2e97e79b6f5da4d825f78bdd1eda04a8d292f9bbfda9c58506ec64849`.
- Pedalboard 0.9.26 (loads this Windows module directly, not the bundle directory); NumPy 2.5.5. Dependency setup is in [`Measure-Spatial-EQ.py`](../tools/Measure-Spatial-EQ.py). No DAW or GUI is needed.
- 48 kHz, 512-sample host buffer, stereo float32, Zero Latency mode, output trim 0 dB, spatial bass protection 120 Hz. Main band: 2.40 kHz Bell, Q 1.00, Stereo, dynamics off, Width 100%, Spread 0%. See the harness for the exact host parameter values and stimuli.
- Mid = `(L+R)/2`; Side = `(L−R)/2`. The steady-state probe uses eight tones (80–10,000 Hz, −32 dBFS peak each), one second of settling, and two seconds of analysis. Both Depth settings had the same direct arrival; no alignment shift was applied.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Measure-Spatial-EQ.py --fingerprint
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-LTI.py
```

## Response by gain and Depth

Each cell lists Depth **0% / 50% / 100%**. Center-frequency gains are magnitudes relative to input. Generated Side is measured over the whole dual-mono multisine.

| Bell gain | 2.4 kHz Mid gain, dual mono | 2.4 kHz Side gain, pure Side input | Generated Side RMS, dual mono |
| --- | --- | --- | --- |
| +6 dB | +6.00 / +5.22 / +4.91 dB | +6.00 / +7.41 / +8.85 dB | −∞ / −32.14 / −26.14 dBFS |
| 0 dB | 0 / 0 / 0 dB | 0 / 0 / 0 dB | −∞ / −∞ / −∞ dBFS |
| −6 dB | −6.00 / −2.49 / 0 dB | −6.00 / −6.00 / −6.00 dB | −∞ / −∞ / −∞ dBFS |

- **Boost:** Depth at 100% changed the mono fold at 2.4 kHz by **−1.09 dB** vs Depth off. The Mid waveform residual was **−44.70 dBFS RMS**, or **−21.16 dB relative to Depth-off Mid RMS**. An exactly side-only implementation cannot match this fixture. This is not the vocal used in the reference video and does not test its 0.2 dB claim.
- **Isolated cut:** The 50% Depth Mid waveform matched the arithmetic mean of the 0% and 100% waveforms to `7.45e-9` full scale. At 100% it matched dry sample-for-sample. Output on the pure-Side input was identical across all three Depth settings. A candidate for this observed case is `Mid = lerp(EQ(Mid), Mid, Depth); Side = EQ(Side)`; do not assume it explains every band or combination.
- **Overlapping cut:** A second −6 dB bell at the same frequency/Q reduced the generated Side of the +6 dB, 100%-Depth boost by **−6.00 / −4.98 / −6.00 dB** at 2.4 kHz as cut Depth moved 0% / 50% / 100%. Relative to the boost alone, Mid changed **−6.00 / −2.21 / +0.56 dB**. In combination, a full-Depth cut did not leave the boost-only Mid identical.

## Timing and decay of the added response

Dual-mono impulses at −36 and −12 dBFS followed 100 ms of silence. The output here is the **full-Depth minus Depth-off difference**, not the whole output and not plug-in latency. Onset threshold was −100 dB relative to the input impulse peak. Percentages are remaining delta energy relative to the whole delta.

| Component | Onset | Peak time | Peak vs input | Energy remaining after 20 / 50 / 100 ms |
| --- | --- | --- | --- | --- |
| Mid | 5.958 ms | 6.000 ms | −37.85 dB | 23.84% / 3.31% / 0.00749% |
| Side | 1.708 ms | 18.917 ms | −26.89 dB | 48.28% / 4.64% / 0.04186% |

Normalizing by input amplitude gave nearly identical isolated impulse responses at both levels. The late energy is measurable, but this test cannot attribute each arrival or the decay to a particular tap, filter, or reverb algorithm.

## Does a fixed response match fresh audio?

A stereo 2×2 convolution model fitted from isolated channel impulses was tested against separate synthetic speech-like bursts and decorrelated stereo signals. Error is RMS relative to the corresponding measured target component. Depth-only error compares the modeled and measured difference between Depth off and full Depth. The scores below held at tested input peaks of −36, −18, and −6 dBFS.

| Held-out input | Full output error, Mid / Side | Depth-only error, Mid / Side |
| --- | --- | --- |
| Dual-mono speech-like bursts | −34.09 / −31.38 dBr | −13.10 / −31.38 dBr |
| Decorrelated stereo | −35.23 / −40.19 dBr | −16.08 / −32.28 dBr |

Depth off matched the model to about −147 dBr wherever the reference had signal, and fresh reference renders repeated exactly. At full Depth, two isolated impulses 512 samples apart each fit individually, but the actual pair differed from the sum of their isolated outputs by **−34.10 / −38.92 dBr, Mid / Side**. This establishes a failure of the fixed linear model for this fixture; it does not identify the mechanism or prove a particular envelope/ducking design. The first sparse delayed-bandpass approximation scored worse on both holdouts.

## Next matching checkpoint

Use the isolated-cut behavior and the measured boost timing, spectra, L/R balance, and decay as separate targets for a one-band prototype. Probe transient context with bursts over sustained input and vary frequency, Q, gain, Width, Spread, and bass protection before fixing a wideband topology. Keep a listening comparison on real mono vocals and stereo material separate from measurements on synthetic probes. Score the Depth-induced change, not only the full signal error: the direct EQ can hide a poor spatial match. No product DSP or UI has been changed at this stage.
