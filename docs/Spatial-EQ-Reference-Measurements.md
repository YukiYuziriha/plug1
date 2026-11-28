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

### Follow-up: frequency tracking, phase, and model transfer

- [`Probe-Spatial-EQ-SidebandTracking.py`](../tools/Probe-Spatial-EQ-SidebandTracking.py): Fixed +6 dB/2.4 kHz/Q=1 bell, −24 dBFS Mid bed swept through 1.5/2/2.4/3.5 kHz, probe teeth 1,712/2,828/4,044 Hz at −48/−60 dBFS. Dominant lines tracked offsets of ±2F and ±4F, where F is the bed, not the EQ center. At separable fixed ±4.8/±9.6 kHz offsets Mid→Mid peaked at −93.44 dB or lower, and at ±F/±3F at −98.64 dB or lower, relative to an input tooth. Zero Latency and Linear Phase both showed tracking; their Mid→Mid peak levels were within 0.06 dB, not identical waveforms. This is evidence of bed-dependent frequency conversion, not a disclosure of the internal mechanism.
- [`Probe-Spatial-EQ-SidebandPhase.py`](../tools/Probe-Spatial-EQ-SidebandPhase.py): At a fixed 2.4 kHz bed, signed phase rotation `exp(i*n*bedPhase)` predicted held-out phases for orders `n = ±2, ±4`, with complex residuals from −94.5 to −35.9 dBr at the 1,732 Hz tooth across tested levels and routes. Odd orders did not follow this fit. Reusing the same complex coefficient at nearby probe frequencies or across levels gave substantially worse results; phase tracking does not imply constant amplitude or phase offset.
- [`Fit-Spatial-EQ-SidebandModel.py`](../tools/Fit-Spatial-EQ-SidebandModel.py): Compared a phase-aware interpolator, a bed-frequency-independent coefficient, and a zero-sideband baseline. In the first test, interpolating across unseen beds gave **+2.50 dBr**, worse than zero (0 dBr), even though a fit at the same bed and level predicted unseen phases to −42.92 dBr pooled. A denser development grid scored **−0.21 dBr** on five held-out beds and three held-out phases; applying fixed delay/onset anchors (+0.68 dBr) or development-fitted phase slopes (+3.69 dBr) made pooled performance worse. The regularly spaced development grid also aliases phase slopes; the fitted slope is not a physical delay estimate. No model was promoted, no reference audio or coefficients were saved, and steady-state scores do not measure time-domain overlap accuracy.

### Follow-up: irregular grids and interacting tones

Run both scripts with the same half-CPU wrapper. They reset and reuse the host for the main captures and check selected results against fresh instances.

- [`Probe-Spatial-EQ-IrregularGrid.py`](../tools/Probe-Spatial-EQ-IrregularGrid.py): Three local bed-frequency regions with 6–24 Hz steps and a wider sparse comparison, scored on withheld frequencies and phases. At −48 dBFS per tooth, dense local interpolation predicted Side→Side complex lines at −36.5 to −47.7 dBr. For Mid→Mid around the 2.4 kHz band, raw interpolation scored +1.53 dBr, the onset anchor −1.34 dBr, and a development-fitted phase slope −1.16 dBr; the local same-frequency phase fit scored −57.57 dBr. At adjacent center-region settings, median measured Mid→Mid line magnitude changed 5.42 dB, versus 0.04 dB for Side→Side. Circular phase steps and relative line amplitudes both changed in Mid; wrapping a phase display is not the only problem. These are steady-state complex line scores, not errors on a full render.
- [`Probe-Spatial-EQ-ComplexBeds.py`](../tools/Probe-Spatial-EQ-ComplexBeds.py): With 2.0 and 2.4 kHz Mid-only sines, the joint response was not the sum of the isolated responses. At −60 dBFS probe peak the Mid→Mid prediction error on the two families of pure lines was +11.2/+6.8 dBr. The joint bed produced strong mixed lines at `probe ± F1 ± F2` and `probe ± 2F1 ± 2F2`; at 2,132 Hz the mixed Mid line was −39.9 dB relative to the probe and the summed singles were −143.2 dB. The tested mixed-line phases followed `m*phase1+n*phase2` when the bed phases changed; at 2,132 Hz the Mid complex residual was −102.5 dBr relative to that line. An RMS-matched, four-harmonic saw produced odd offsets that were absent above controls for the single sine. The observed parity in the one-tone case does not extend to arbitrary beds; the probe does not identify a unique nonlinearity.

### Batched two-tone gain and spacing transfer

[`Probe-Spatial-EQ-MixedBatch.py`](../tools/Probe-Spatial-EQ-MixedBatch.py) runs 80 centered measurements (320 reference renders) in one invocation through the same half-CPU wrapper. It calibrates complex mixed-line coefficients at one equal-level 400 Hz bed spacing, then holds out relative gain ratios, common gain, phase, and frequency pairs at 480 and 900 Hz spacing. All 72 held-out lines exceeded twice their measured controls; six fresh/reset checks agreed at the measured bins and controls. No audio or coefficients were saved.

A predictor proportional to `A1*A2` had +6.7/+6.7 dBr Mid/Side error when both tones shifted by ±6 dB; `A1*A2/(A1²+A2²)` gave −53.6/−76.8 dBr. This supports near-invariance to common gain in that fixture, not that the reference implements the tested formula. Across all holdouts the normalized prediction scored −3.2/−8.0 dBr versus 0 dBr for the additive and zero baselines, but it still lost to zero at the equal-level 900 Hz holdout. Neither hypothesis was promoted.

### Larger factorial and three-tone holdouts

- [`Probe-Spatial-EQ-DiverseBatch.py`](../tools/Probe-Spatial-EQ-DiverseBatch.py): 1,504 renders in 56.99 s in one wrapped invocation. Crossed four centers × four spacings, two probes (1,732/3,972 Hz), and both Mid and Side routes. Per route and probe, fit six equal-gain cells and hold out ten, including three unseen combinations of seen center and spacing values. Excluded the ratio, phase, co-scale and joint challenges from fitting. All 352 held-out lines exceeded 2× the measured controls and mixed residuals exceeded 8×; eight checked fresh/reused captures agreed at scored bins and controls. At Depth 100, joint target peaks were −49.7 to −32.2 dB relative to the probe, versus −138.2 to −133.9 dB at Depth 0.

  | Route / probe | Prior fixed | Probe-matched anchor | Grid constant | Spacing only | Center only | Both |
  | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
  | Mid / 1,732 Hz | +0.7 | +0.7 | +0.5 | +0.2 | +0.6 | +0.4 |
  | Mid / 3,972 Hz | +11.0 | +1.5 | +4.8 | +4.7 | +5.8 | +6.1 |
  | Side / 1,732 Hz | +0.2 | +0.2 | −2.5 | −5.0 | −2.5 | −4.6 |
  | Side / 3,972 Hz | +4.4 | −0.1 | −2.8 | −5.2 | −2.9 | −4.9 |

  Errors are relative complex RMS in dBr on the ten factorial holdouts; zero and measured additive rounded to 0.0 dBr. Spacing alone helped Side difference lines in unseen combinations (−10.5/−10.0 dBr across the two probes), but lost to zero on sum lines in the same group. No model transferred reliably across the full grid.

- [`Probe-Spatial-EQ-ThreeTone.py`](../tools/Probe-Spatial-EQ-ThreeTone.py): 296 renders in 13.94 s in one wrapped invocation. Added a controlled third tone C to an existing A/B bed and scored the same A/B mixed lines. At 680 Hz, the louder C changed line magnitudes by at most 0.6 dB on either route. At 3,560 Hz and the same −18 dBFS level, individual lines dropped by 1.3–4.7 dB, and phases shifted up to about 15°. A total-bed-power adjustment improved that strong 3,560 Hz case but did worse overall than leaving the A/B prediction unchanged (pooled −8.2/−8.4 vs −12.8/−12.3 dBr Mid/Side). The frequency of the added tone matters; broadband power alone does not explain it. A gated-C case was scored only after settling, not during the transition. All conditional lines cleared controls.

### Mid-only: early trigger, weak-hit scaling, and overlap fit

All three tools below completed through the half-CPU wrapper; no render or fitted data was saved.

- [`Probe-Spatial-EQ-MidTrigger.py`](../tools/Probe-Spatial-EQ-MidTrigger.py) (1,134 renders) scores 0–1.25 ms after a click, before that click’s isolated Mid Depth response (5.958 ms). At −24 dBFS, histories matched in input energy per 1 ms still produced 2.63% versus 27.41% fitted old-tail suppression at a 3.5 ms gap (2.4 versus 6 kHz history). At 0.5 ms the corresponding values were 0.48% and 7.89%. The 0.25 dB sweep resolved no sharp jump in engagement. A band-weighted ratio improved the 6 kHz/3.5 ms waveform error to −10.61 dBr versus −4.48 dBr for a broadband ratio, but lost to zero in a short-gap holdout. This supports frequency-sensitive history, not a universal detector or gain law.
- [`Probe-Spatial-EQ-MidWeakLaw.py`](../tools/Probe-Spatial-EQ-MidWeakLaw.py) (414 renders) isolates the early polarity-even term. At a fixed 3.5 ms conditioning gap and five withheld probe levels with the same 0.25 ms/6 kHz hit shape, the even-response norm had a log-log slope of 2.008. The quadratic norm fit missed by 0.07 dB RMS, versus 8.55/8.42 dB for linear/cubic; the weakest holdout was 62.6 dB above the numerical-floor proxy. This did not imply a quadratic waveform model: the frozen template left 9–43% relative L2 error, and energy-matched unseen hit shapes missed the predicted norm by about −5 to +7 dB. Co-scaling history/hit by ±6 dB preserved the old-tail-normalized trace within the measured floor. The later 1.25–12 ms window was 24–37 dB stronger in the polarity-odd term than the even term for the checked cases; this window is distinct from the early fit window, not a claim about an exact regime boundary.
- [`Fit-Spatial-EQ-MidOverlap.py`](../tools/Fit-Spatial-EQ-MidOverlap.py) compares pre- and post-reflection corrections against the existing offline baseline. The pre-reflection candidate improved development by 1.43 dB, but the held-out burst-total error only moved from −4.45 to −4.47 dBr and hit-increment error worsened from −1.87 to −1.83 dBr. It regressed in 12/24 held-out burst cases, with an 18.24 dB worst regression on a releasing-tone overlap. The post-reflection candidate was effectively tied. Neither correction was promoted. These error figures are for this harder holdout suite, not directly comparable to the earlier burst tables.

### Mid reconstruction: gain on the reflection return

[`Fit-Spatial-EQ-ReflectionEnvelope.py`](../tools/Fit-Spatial-EQ-ReflectionEnvelope.py) tests the architecture identified by local binary analysis: a band-filtered 2 ms / 40 ms energy ratio controls the **output** of the reflection path. The traced steady-state detector uses a unity-at-center matched-pole band-pass after the normal EQ; an earlier approximation used a Q=2 band-pass on raw input. The script derives the fixed direct-EQ response and an ungated reflection kernel from isolated calibration captures, retains them only in memory, then predicts from input alone. This isolates the detector and placement hypothesis; it is **not** a calibration-free reconstruction of the delay network. Decompiler output and any raw reference data are not included.

| Held-out Mid error (dBr, lower is better) | Fixed baseline | Q=2 approximation | Traced detector |
| --- | ---: | ---: | ---: |
| Burst total (24 cases) | −4.45 | −38.42 | −48.37 |
| Hit increment over bed | −1.87 | −31.77 | −45.46 |
| Early hit increment (first 5.958 ms) | +0.08 | −1.56 | −1.56 |
| Late hit increment | −18.69 | −44.86 | −50.95 |
| Isolated packets | −16.46 | −47.76 | −52.60 |
| Isolated clicks and pairs | −33.72 | −45.75 | −45.42 |

The traced detector gave −48.87/−49.10/−47.01 dBr across sustained, gated, and decaying burst totals. The separately calibrated pre-return placement instead scored +12.40 dBr overall. The original per-case relative guard was retained and **failed** for six early increments with near-zero reference energy. None of those regressions exceeded the absolute final-output rounding margin estimated from the float32 renders. Pooled early absolute error dropped from `9.50e-4` to `4.89e-6` full-scale RMS. The absolute check is not a waiver of the relative guard or a claim of an exact null.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Fit-Spatial-EQ-ReflectionEnvelope.py
```

### Calibration-free one-band delay network

[`Spatial-EQ-DelayModel.py`](../tools/Spatial-EQ-DelayModel.py) implements the two fixed tap banks, four-line feedback, both Side injections, and the band-conditioned gain on the filtered return. It uses no captured impulse response, reference binary, or fitted coefficients when processing. The matched-pole filter designs include their analog Nyquist targets; the previous detector experiment used the same band-pass poles and center gain but forced a Nyquist zero. This initial validation covered the settled positive-gain Stereo Bell Depth delta. The later shelf and cut extensions below retain that path.

[`Test-Spatial-EQ-DelayModel.py`](../tools/Test-Spatial-EQ-DelayModel.py) retains the preceding 24 held-out overlaps at 33/71/96% Depth and compares against the installed reference without gain fitting, time alignment, or calibration. Dual-mono alone hid a left/right routing error on one late tap; checking the asymmetric impulse localized the first divergence, and correcting the tap routing resolved it at both 44.1 and 48 kHz.

| Held-out error (dBr, lower is better) | Mid | Side |
| --- | ---: | ---: |
| Full bed + hit, entire fixture | −121.71 | −129.94 |
| Burst total | −107.93 | −129.49 |
| Early total | −101.69 | −128.84 |
| Late total | −124.25 | −129.75 |

Full-window absolute errors were `2.31e-9`/`1.28e-8` FS RMS and `1.95e-8`/`6.56e-8` FS peak, Mid/Side. Early increment and interaction ratios can still look much worse because six windows have near-zero reference; the cited early absolute errors are below `4e-9` FS RMS. At 44.1/48 kHz, isolated dual-mono and asymmetric anchor clicks at 50/100% Depth scored −130.11 to −124.57 dBr. Depth 0 was exactly zero.

Off-anchor checks set and read back the controller display values. At 800 Hz/+3 dB/Q 0.70 and 6.4 kHz/+12 dB/Q 2.00, dual-mono and asymmetric clicks across 48/96 kHz and 33/100% Depth came within −96.58 and −114.23 dBr, respectively, in both outputs. A 1.25 s asymmetric tone fixture plus tail at the latter setting, 96 kHz and 71% Depth, scored −117.06/−121.92 dBr. Display value rounding may play a role off-anchor; several residuals exceed final output rounding and the test does not isolate the cause.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Test-Spatial-EQ-DelayModel.py
```

### Positive Stereo shelves

The Low Shelf feeds the existing reflection engine through a low-pass on the post-EQ signal; the High Shelf uses a high-pass instead. Both extraction filters use the band center and fixed Q `1/√2`, independent of the shelf EQ's own Q. This is not a filter on `EQ output − dry`. The direct EQ uses matched-pole shelves; the High Shelf boost is constructed as the reciprocal of a matched High Shelf cut.

[`Test-Spatial-EQ-Shelves.py`](../tools/Test-Spatial-EQ-Shelves.py) covers two positive settings per shape, dual-mono and asymmetric stereo impulses and sweeps, and 44.1/48/96 kHz. At 0/50/100% Depth it separates direct EQ from the Depth delta. The worst grouped Mid/Side errors were **−98.19 dBr direct** and **−104.06 dBr Depth delta**. Maximum grouped residuals were `2.37e-7` FS RMS and `2.10e-6` FS peak for direct EQ, and `2.30e-7` FS RMS and `4.68e-6` FS peak for the Depth delta. The modeled Depth-zero delta was exactly zero.

### Negative Stereo Bell and one boost/cut pair

A cut uses the reciprocal of a positive matched-pole Bell at the absolute gain. In the direct pass, cut Depth restores a fraction of the cut Mid toward its uncut value but leaves Side fully cut. Boost extraction reads the final post-EQ stereo signal, after both direct bands. An independent cut blends the accumulated wet signal toward its cut version by the same Depth fraction, on both channels, before the common wet high-pass. The cut itself does not generate spatial energy.

[`Test-Spatial-EQ-Cuts.py`](../tools/Test-Spatial-EQ-Cuts.py) compares full outputs and Depth contrasts at 48 kHz for an isolated −6 dB Bell, a coincident +6/−6 dB pair in both slot orders, and a separated-frequency control. It checks an asymmetric impulse and a dual-mono multisine independently. The coincident cut changed generated Side at 2.4 kHz by **−6.00 / −4.98 / −6.00 dB** at 0/50/100% cut Depth, but the equal endpoint tone levels did not imply equal waveforms. Tests report both relative error and absolute residuals, including near-null interaction contrasts.

### Automation: single positive Stereo Bell

The trace indicates a persistent **32-sample** control tick with an **18 ms** one-pole step. Frequency and Q interpolate in log space; gain interpolates in dB and Depth in percent. The resulting boost/cut amount has a separate **12 ms** per-sample slew. Some return coefficients follow the tick-level target rather than the slewed amount. Band enable has its own 12 ms slew toward `active && enabled`; unlike Depth, flag changes can affect the wet return on the first sample.

[`Probe-Spatial-EQ-Automation.py`](../tools/Probe-Spatial-EQ-Automation.py) measured a 33→71% Depth step with no instance reset at 48 and 96 kHz. Boundaries at phases 13, 27, and 3 modulo 32 first changed the output 19, 5, and 29 samples later, respectively. No-op splits matched unsplit runs exactly; host buffers of 257 and 512 samples gave the same result. Explicit reset changed even the no-op waveform. This verifies the tick at host-call boundaries, not VST3 in-block queue timing.

[`Spatial-EQ-AutomationModel.py`](../tools/Spatial-EQ-AutomationModel.py) updates filters and feedback at the tick and uses the per-sample amount for tap blending and output scaling; it does not crossfade static renders. It preserves histories across ordinary control updates. [`Test-Spatial-EQ-Automation.py`](../tools/Test-Spatial-EQ-Automation.py) checks individual Depth, frequency, Q, and gain steps and a 20-point interleaved ramp at 48 and 96 kHz. It scores direct EQ, Depth delta, and full output separately. The 96 kHz ramp scored **−101.68 dBr** or better on full output, with `4.62e-7` FS maximum Mid/Side peak error. Off-anchor comparisons match frequency and Q at controller display precision, not at separately measured internal values.

### Tail lifecycle and reset

[`Probe-Spatial-EQ-Tails.py`](../tools/Probe-Spatial-EQ-Tails.py) observed retained wet history after 4 and 60 ms holds for `active`, `enabled`, and Depth; direct ringing did not explain it. Restoring `active` or `enabled` after 60 ms changed the first sample, while restoring Depth waited 25 samples for the next tick in that fixture. A 3-second Depth-zero hold suppressed a late marker to at most `6.44e-15` FS peak after restoration; the 1%-Depth positive control retained a marker at `0.0360–0.0366` FS. The trace shows the band stops wet processing when current and target amount are zero and clears its per-band wet history on re-entry. The marker test bounds observable carry-over; it cannot independently distinguish clearing from not writing during dormancy.

Explicit reset clears direct and wet history and restarts the tick. The reference and modeled silent suffixes were sample-exact zeros after reset. The model does not emulate Pedalboard-specific reprepare behavior or VST3 in-block automation.

### Static simultaneous boosts

Each boost sums eligible extraction-filter magnitudes at its own center frequency, including its own filter if eligible. The reciprocal of that sum, floored at unity before inversion, scales both Side injections and the filtered return, not the delay writes or the energy detector. Contributions use the extraction filter response with no further gain or Depth scaling. Only enabled positive Bell/Low Shelf/High Shelf bands above `1e-5` dB and above `0.001` percentage points of Depth count toward overlap. The Depth threshold excludes a band from the sum, but does not by itself silence its wet path. Multiple complete boost returns are summed before the wet-only cuts and common high-pass. Two coincident Bells verified the normalization from the first block, without a warm-up ramp from unity.

[`Test-Spatial-EQ-Multiband.py`](../tools/Test-Spatial-EQ-Multiband.py) compares overlapping and separated bells, mixed shelves and bells, eight-band holdouts, and mixed boost/cut slot orders. The fixtures include asymmetric impulses and sweeps. Across 90 Mid/Side comparisons, the worst Depth-delta RMS error was **−93.1 dBr**; its maximum residual was `1.92e-7` FS RMS and `1.73e-6` FS peak. Full output peaked at `2.99e-6` FS residual. Removing the model's wet path as a negative control failed the overlapping-bell Depth checks while direct EQ still passed.

### Moving overlap weights and cuts

The code updates bands in slot order every 32 samples. A receiver uses its newly smoothed center frequency. Earlier contributors use this tick's working frequency and Q; later contributors still use their previous values. Eligibility uses the target snapshot. Each weight has its own 18 ms tick-rate smoother, seeded during prepare/reset but not at wet-dormant re-entry. For negative Bells, the direct and wet filters share moving coefficients but keep separate histories; the wet blend is scaled by cut amount and enable.

[`Test-Spatial-EQ-MultibandAutomation.py`](../tools/Test-Spatial-EQ-MultibandAutomation.py) verified the moving-band traces at 48 and 96 kHz. Swapping two moving Bell slots changed reference wet Side by up to `1.566e-3` FS peak; the model reproduced the contrast within `3.789e-8` FS peak. Advancing all filters before scoring receivers failed the dedicated order check. At both rates, changing the buffer from 512 to 257 samples and adding irregular no-op call boundaries did not change the reference waveform. Four/eight-band holdouts cover shelf moves, Depth ramps, switches, and reset; the largest full-output Mid/Side residual was `1.461e-6` FS peak. The moving negative Bell fixture passed in both slot orders; its Depth-on minus Depth-off comparison includes the Depth-dependent direct Mid cut, not just the wet return.

### Recorded voice check

[`Test-Spatial-EQ-Voice.py`](../tools/Test-Spatial-EQ-Voice.py) compares both processors at 48 kHz on a static four-band body/low-mid/presence/air boost preset with 40–85% Depth. Fixtures were Garth Comira's mono audiobook narration ([librosa `libri1`](https://librosa.org/data/audio/5703-47212-0000.txt), CC BY 4.0) and Sofie Sigrinn's native stereo recording of [Harmonica singing *Tourdion*](https://commons.wikimedia.org/wiki/File:Harmonica_sings_Tourdion.flac) (CC BY-SA 4.0). The latter is choir, not isolated solo singing. Neither render was time-aligned or level-corrected for these metrics.

| Fixture | Full L/R residual RMS / sample peak | Depth-delta L/R residual RMS |
| --- | ---: | ---: |
| Mono narration | −134.40 / −115.57 dBFS | −142.96 dBFS |
| Stereo choir | −134.27 / −113.25 dBFS | −140.38 dBFS |

The model/reference RMS gain difference was less than `0.000001` dB on narration and `0.000030` dB on choir. Optional `--ab` exports local reference/model WAVs with pooled L/R RMS matched, not perceptual loudness matched. No human listening test has been performed.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Test-Spatial-EQ-Shelves.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Test-Spatial-EQ-Cuts.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-Automation.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Test-Spatial-EQ-Automation.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-Tails.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Test-Spatial-EQ-Multiband.py --with-cuts
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Test-Spatial-EQ-MultibandAutomation.py
# After downloading either attributed fixture to the current directory:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Test-Spatial-EQ-Voice.py Harmonica_sings_Tourdion.flac --seconds 40 --ab
```

### Negative shelves and C++ test build

[`Test-Spatial-EQ-Negative.py`](../tools/Test-Spatial-EQ-Negative.py) verifies negative Bells and both shelves plus live gain-sign crossings. The trace separates cut and boost amount envelopes from the signed direct coefficients. Crossing zero keeps filter and delay histories; the wet cut has a separate state and uses nonpositive working gain. The HEAVEN processor tests cover Depth 0/50/100% and crossings in both directions with an overlapping boost.

[`Test-Spatial-EQ-Cpp.py`](../tools/Test-Spatial-EQ-Cpp.py) ran 38 C++/Python parity fixtures at 44.1, 48, and 96 kHz. Worst peak errors were `9.96e-8` FS full output and `6.74e-8` FS Depth delta, with sample-identical irregular splits per fixture. [`Compare-Spatial-EQ-Cpp-Reference.py`](../tools/Compare-Spatial-EQ-Cpp-Reference.py) compares the standalone C++ engine directly against installed 0.7.0. On mono speech and native stereo choir at 48 kHz, the worst pooled stereo residuals were **−106.99 dBr full / −96.13 dBr Depth delta** on static renders and **−104.42 / −99.07 dBr** on gain-crossing renders. These are whole excerpts plus tail at original alignment and levels; the first 100 ms after the gain write independently scored **−104.13 / −99.65 dBr**. Here dBr measures RMS difference relative to the RMS of the reference waveform, not a perceptual match percentage.

The Release test build uses the new eight-band Spatial EQ node without changing legacy EQ8. The native harness verifies impulse onset with no added latency and observes no C++ `new` calls during the test after prepare. At 48 kHz and 256 stereo frames, median steady-state standalone cost was about 7/31/223 µs per block for zero/one/eight active Depth bands. That is approximately 0.13/0.57/4.19% of the block deadline, not total HEAVEN or machine-wide CPU. CLAP and VST3 smoke tests and focused offscreen editor/processor Spatial EQ tests passed.

### Integrated HEAVEN VST3 on recorded singing

[`Compare-Heaven-Spatial-EQ-Reference.py`](../tools/Compare-Heaven-Spatial-EQ-Reference.py) loads the built HEAVEN VST3, recalls `Lead → Spatial EQ → Output`, and compares it against Spatial EQ 0.7.0 on the stereo *Tourdion* excerpt. Both all-bands-off renders matched dry exactly. The static cut/boost preset scored **−108.05 dBr** full output and **−101.10 dBr** Depth delta, before any level matching or time shift. Between-call Bell and Low Shelf gain crossings in both directions scored **−104.85 dBr or better** on full output, with a worst peak residual of `2.34e-6` FS. In the first 100 ms after the write, the isolated change relative to a no-op scored **−94.13 dBr or better**. No-op split renders matched their unsplit controls exactly. The A/B export uses pooled L/R RMS matching; it has not been auditioned.

Native CLAP Gain and Depth timestamps matched explicit splits; native VST3 Gain and Depth did not. The [native automation findings](Spatial-Audio-Plan.md#native-vst3-automation-finding) describe the wrapper limitation. The singing comparison does not test VST3 parameter queues.

**Remaining scope:** automated shape changes, additional shapes/channel modes and spatial controls, a fix for VST3 in-block parameter delivery, and human listening. The research build has not been installed or released. No reference audio or decompiler output is tracked.
