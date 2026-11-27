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

## Extended measurements: controls, sample rate, and signal history

All tests use the same installed 0.7.0 binary. In the following commands, prefix `python` with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1` as above.

- [`python -B tools/Sweep-Spatial-EQ-Controls.py`](../tools/Sweep-Spatial-EQ-Controls.py): 0/10/25/50/75/90/100% Depth on the same bell, plus one-control sweeps with display values.
- [`python -B tools/Probe-Spatial-EQ-Context.py`](../tools/Probe-Spatial-EQ-Context.py): paired clicks of varying gap, routing, level and polarity; reset/block controls; burst over a sustained bed.
- [`python -B tools/Probe-Spatial-EQ-Rate.py`](../tools/Probe-Spatial-EQ-Rate.py): 44.1/48/96 kHz, block sizes and position, and selected frequency/Q checks.

**Depth changes the response, not just its gain.** On the +6 dB bell, the best-fit scalar from the full-Depth Side impulse to 50% Depth was `0.5001`, but the residual was −21.2 dBr relative to the measured Side delta. The Side peak moved from 23.71 ms at 10–25% to 18.92 ms at 50–100%. At 50% Mid, the best-fit scalar was `0.2611` and the residual was −2.3 dBr. Mid energy left after 50 ms fell from 7.80% at 25% Depth to 3.31% at full Depth. Mid and Side onset times did not move with Depth in this fixture.

**Band and control sweeps.** Frequency (240/800/2,400/6,400 Hz) and Q (0.5/1/2/4) changed the shape without moving impulse onset at 48 kHz. For example, tightening Q from 0.5 to 4 reduced the generated Side at an 800 Hz probe by 18.29 dB with the bell centered at 2.4 kHz, while leaving the on-center Side gain near +7.25 dB relative to input. At +3/+6/+12 dB, steady generated Side was −31.32/−26.14/−19.77 dBFS RMS. Width 0–200%, Spread 0–100%, and bass protection 20/120/500 Hz did not change the measured dual-mono deltas in the tested mode; the bass result also held at a 240 Hz center. This does not establish that those controls are inert on stereo input, with other band types/modes, or in other hosting paths.

**Delay onsets scale with time; largest Side peak does not.** Across 44.1/48/96 kHz, the first Side change was ~1.70 ms and the first Mid change ~5.96 ms, with onset samples tracking the rate ratio within one sample. The largest Side peaks were 17.23/18.92/23.71 ms, respectively; these peaks should not be treated as the time of a fixed tap. Steady block sizes 64/127/256/511/512/1024 and a change in impulse location within a block gave sample-identical aligned results for the baseline impulse at those rates.

**Signal history changes reflections that are already in flight.** For the reference at 48 kHz, simultaneous impulses added to numerical precision. At 5 ms separation, paired-hit non-additivity was −10.7 to −11.8 dBr Mid and −27.2 to −29.9 dBr Side relative to the paired Depth delta across tested channel routings; at 200 ms it was below −53/−69 dBr. The normalized L→R result was stable across −60 to −6 dBFS, with no discernible block/reset artifact under normal processing. With a 10.67 ms gap, 96.7% of the interaction energy arrived in the first 1 ms after the second hit. A reduction of the *first* hit’s existing tail fit the Mid interaction in that window much better than a gain change on the *new* response; one scalar did not explain Side. A burst over sustained audio also changed behavior with burst polarity, so isolated equal-level impulse behavior does not generalize. These observations constrain a model but do not identify the mechanism.

## Offline candidate and adjustments

Run [`python -B tools/Fit-Spatial-EQ-Candidate.py`](../tools/Fit-Spatial-EQ-Candidate.py) with the same wrapper. The candidate is an offline model of one static +6 dB, 2.4 kHz, Q=1 bell at 48 kHz, **not product DSP**. It derives stereo impulse matrices at 0/10/25/50/75/90/100% Depth and compares scalar, linear, and local quadratic response models. Model selection used different Depth settings (4/17/61/86%) and waveforms from final evaluation (7/37/68/94%). It also tested a short Mid-tail correction for isolated hit pairs, selecting a 5 ms version on the development set. No fitted data is saved or shipped.

- Across 20 held-out Depth/audio cases, median **Depth-delta** error fell by **12.07 dB Mid / 10.77 dB Side** versus scaling the full-Depth impulse response. The isolated Mid correction gave **2.64–15.33 dB** further Mid improvement on held-out click pairs versus the selected interpolation alone; it did not change Side.
- Matching isolated impulses did not guarantee a match for dense audio. At 37% Depth, harmonic mono measured **−10.39/−27.71 dBr Mid/Side** versus **−11.31/−28.09 dBr** for the simpler linear interpolation. During a burst over a sustained bed, Mid error remained **−7.62 to −5.70 dBr** in the initial holdout.
- An opt-in `--dense-diagnostic` tests a signed recent-vs-earlier input-power correction on the existing Mid tail. It was fitted on development clips and evaluated on fresh waveforms at disjoint Depth values. Mean normalized Mid error moved **−14.739 → −15.115 dBr** across 16 cases, with no regressions; burst windows improved 1.47–1.78 dB but still measured about **−8.1 to −10.7 dBr**. It is diagnostic, not part of the default candidate.

## Tail interaction and nonlinear system identification

Use the same wrapper and fixture as above. No audio or coefficients are written by these probes.

- [`Probe-Spatial-EQ-BedInteraction.py`](../tools/Probe-Spatial-EQ-BedInteraction.py) separates bed, hit, and joint renders across four hit phases, three relative levels, timings and 50%/100% Depth. In a settled bed, the 1–8 ms interaction from a +12 dB hit is mostly polarity-even and a bed-tail gain fit explains considerably more Mid than Side (at full Depth, −14.5/−4.1 dBr residual). At equal level the polarity-odd part accounts for 37.5%/33.5% of interaction energy; a signed cosine predictor helps Mid but leaves substantial quadrature and Side errors. At bed onset, the equal-level 8–30 ms interaction is 98.8%/89.8% odd and fits the isolated new hit response better than an old-bed gain change. These are distinct observed regimes, not proof of separate internal processors.
- [`Probe-Spatial-EQ-TailState.py`](../tools/Probe-Spatial-EQ-TailState.py) tests the first 1.5 ms, before either isolated-hit Depth component arrives. After a pulse train a click attenuated the old Mid response (−0.614 fitted gain change, −14.0 dBr residual); the late burst response did not fit the same model. A fixed Side correction failed to generalize. Matching recent input energy did not match the early suppression.
- [`Fit-Spatial-EQ-Tail-Engagement.py`](../tools/Fit-Spatial-EQ-Tail-Engagement.py) uses 24 development cases and 12 held-out cases. With the same −24 dBFS click and matched prior 12 ms energy, effective Mid suppression was 56.05% for one history and 4.40% for the other. A band-envelope model predicted 52.31% and 2.95%, respectively, and improved equal-weight group error to −4.65 dBr, but it barely improved one click group and lost to broadband on pooled error. No candidate generalized across all histories; no correction was promoted and the late response was not modeled.
- [`Probe-Spatial-EQ-LocalResponse.py`](../tools/Probe-Spatial-EQ-LocalResponse.py) measures small perturbations about an identical bed. For `F = Depth-on output − Depth-off output`, it computes the centered response `[F(b+εp)−F(b−εp)]/(2ε)` and the probe-even residual `[F(b+εp)+F(b−εp)]/2−F(b)`. At −84 dBFS, the sum of individual centered M and S responses predicted the joint weak probe to −48.79 dBr or better, but the worst step-to-step change was still −27.71 dBr. At weaker levels the output rounding floor grows, so this is evidence of approximate local additivity at finite probe size, not convergence to a derivative. Across bed phases, the quiet impulse response missed the centered response by as much as ~−4 dBr. Separate preconditions followed by **100 ms of identical input** and the same probe still yielded a **−5.42/−21.18 dBr** difference in centered Mid/Side response at −60 dBFS after subtracting each history’s own baseline. This measures history-dependent response, not just different residual tails. It does not establish a 100 ms time constant. Fresh/reset, block-size and lead-shift controls matched for the checked fixture.

### Techniques to close the remaining gaps

1. **Map the history-dependent response.** Repeat the identical-suffix test across durations, crossing conditioning recency and frequency at matched energy, then cross both conditioning and probe polarity. A probe-even result about a nonzero bed is not proof of global even-order distortion. Select the useful finite probe range by checking centered-response stability, input deltas after float32 conversion, and output rounding. Predict held-out weak waveforms and separate that score from finite-hit behavior.
2. **Use coherent detection-line multisines and probe sidebands.** Excite selected odd FFT bins and leave some odd bins empty, keep line magnitudes fixed, and compare repeated settled periods and multiple phase realizations. This separates phase dependence and unexcited-bin energy from repeatability and yields a stimulus-dependent best linear approximation, not a universal IR. Then use a much weaker comb around a single-tone bed and measure bed-spaced sidebands in the centered response; sidebands that persist after dividing by probe amplitude can be first-order frequency conversion *around the bed*, not necessarily higher-order probe distortion. Test Mid and Side separately and choose bins to avoid collisions. A first weak-comb pass was run below. The separate random-phase detection-line sweep remains proposed.
3. **Use synchronized swept sine as a secondary fingerprint.** It can summarize isolated harmonic responses across frequency; it does not identify general cross-lag memory or replace the bed- and history-conditioned tests. Do not infer an absolute-level compressor from the current data: the measured behavior is non-additive but nearly invariant to joint scaling over the tested range.

References: Schoukens and Ljung, [Nonlinear System Identification: A User-Oriented Roadmap](https://arxiv.org/abs/1902.00683); Schoukens, Vaes and Pintelon, [Linear System Identification in a Nonlinear Setting](https://doi.org/10.1109/MCS.2016.2535918) (detection lines and best linear approximation); Novak, Lotton and Simon, [Synchronized Swept-Sine: Theory, Application, and Implementation](https://doi.org/10.17743/jaes.2015.0071).

### Follow-up: conditioning memory and weak-probe sidebands

- [`Probe-Spatial-EQ-MemorySweep.py`](../tools/Probe-Spatial-EQ-MemorySweep.py): At −60 dBFS probe peak, changing the timing of a 2.1 kHz conditioning packet or swapping a recent 2.1 kHz packet for an energy-matched 6 kHz packet changed the centered response by about −11/−26 dBr (Mid/Side) after 100 ms of identical suffix. After 250 ms the changes were about −44/−61 dBr, above the estimated output rounding bound but sensitive to probe level; after 500 ms neither change cleared that bound. Matching the time since the packet ended (260 ms), while changing the intervening bed, still gave about −6.7/−21.3 dBr. Conditioning timing, frequency and intervening audio matter; the data do not establish a single decay constant or no memory after 500 ms. Matched input energy, zero Depth-off contrasts, pre-probe nulls and fresh/reset controls were checked.
- [`Probe-Spatial-EQ-Sidebands.py`](../tools/Probe-Spatial-EQ-Sidebands.py): With a settled 2.4 kHz dual-mono bed at −24 dBFS and a centered weak comb, the Side- and Mid-input probes both yielded output sidebands at probe frequencies offset by ±4.8 and ±9.6 kHz, ≥50 dB above the no-bed controls. No comparable evidence was found at ±2.4 or ±7.2 kHz. Across −48/−60 dBFS per tooth the amplitude-normalized complex coefficients differed by 0.6–1.6% for Side input and 15–31% for Mid input (percent of weaker-step peak). This measures frequency conversion around this bed, not the internal mechanism or the zero-probe limit.

**Next gate:** test whether the conversion offsets follow bed frequency and test a model that predicts held-out complex sideband coefficients and time-domain overlap under multiple histories. Then check other band settings, sample rates and real vocals before judging the match. No HEAVEN DSP or UI changed.
