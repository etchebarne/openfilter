# Saturator 0.1.1 — CPU and audio investigation

The optimized engine retains the original four transfer functions, 32× processing
at 44.1/48 kHz, the five FIR designs, 76-sample latency, sample-based smoothing
and schema-1 sessions. The measured speedup comes from reducing repeated work.
The intended studio-use range has strong numerical results. Musical preference,
Bitwig scheduling and the extreme-drive boundary still require qualification.

## What changed

- Half-band FIRs now use contiguous odd/even polyphase histories and power-of-two
  ring indexing. Direct-form reference tests cover 17/33/49/129-tap cases.
  The shared limiter primitive receives this arithmetic-preserving optimization;
  its independent measurement suite passes too. Its installed artifact is untouched.
- Resampling stages process short contiguous arrays in chronological order.
  Nonlinear evaluation runs in one loop, avoiding repeated callbacks inside five
  nested interpolation/decimation loops. Audio blocks and event offsets remain
  unchanged; these arrays contain the oversamples of one host sample.
- The dry interpolation/decimation cascade is replaced with its mathematically
  equivalent host-rate FIR, derived from the complete cascade at activation.
  Wet/dry filtering and phase remain matched; dry versus disabled bands nulls.
- Immutable cubic Hermite tables evaluate the original tanh/atan functions.
  Dense retains sqrt. Maximum observed curve error is 1.74e-12 on the dense sweep
  and boundary/extreme checks. Tables are initialized during activation and shared
  across instances, never lazily initialized in the audio callback. No fast-math,
  float-only processing, lower oversampling or new model is used.
- Stationary gain values are cached. Gain-dependent tone coefficients still
  update each sample during ramps; fixed-frequency tangent values are computed
  at activation. LP/HP crossover sections share the same denominator calculation.
  The linked envelope keeps running when Dynamics is zero so enabling it has no
  cold-detector transient, but its unused log/pow calculations are skipped.
- Mono buses process one audio channel. The exact-silence path activates only
  when all detector/filter/FIR/DC/delay histories equal zero; it advances every
  control and style ramp. There is no amplitude gate or truncated quiet tail.
  CLAP still reports a conservative infinite tail and continues callbacks.

Saved audio is covered by a 0.1.0 automation fixture at five rates, including
style fades, crossed crossovers, tone/dynamics and parameter changes during
silence. Numerical tolerance is 2e-10. The wider independent static-audio
reference reports errors around 1.4e-12. No state migration is needed for these
floating-point evaluation changes; IDs, defaults and meanings are stable.

## CPU measurements

`tools/benchmark_saturator.py` invokes the actual old/new CLAP binaries in the
same synthetic host, serially, with the C++ allocation guard enabled. Input
preparation and output checking are outside measured callbacks. The host records
thread CPU time, callback wall time, p99/max and deadline exceedances. Hardware:
AMD Ryzen 5 5600GT, Linux, GCC 16.2 release builds. Editor closed. CPU percentages
are fractions of **one core**, not whole-system percentages or Bitwig's meter.

At 48 kHz, 64-frame callbacks (1.333 ms budget):

| Workload | 0.1.0 CPU | 0.1.1 CPU | Reduction |
| --- | ---: | ---: | ---: |
| Stereo default | 21.88% | 9.68% | 55.7% |
| Mono | 22.20% | 4.98% | 77.6% |
| Tone + dynamics | 23.54% | 10.14% | 56.9% |
| Dense automation | 44.12% | 20.75% | 53.0% |
| Exact silence | 17.30% | 0.19% | 98.9% |
| Four default instances | 88.22% | 38.40% | 56.5% |
| Four automated instances | 174.08% | 80.47% | 53.8% |

These are unpaced desktop measurements, with no real-time priority or deadline
certification. Scheduling interruptions, other plugins, visible editors and DAW
work are excluded. The 120-second single-instance stress delivers mid-block
crossover, style, drive, tone and dynamics events continuously and checks every
output for finiteness. It measured 20.04% CPU, 330.47 µs p99 wall time and
938.03 µs maximum, with zero deadline exceedances and no guarded C++ allocations.
Four default instances also had zero exceedances in the 15-second run. Four
densely automated instances had **36 exceedances in 11,250 callbacks** and a
1.799 ms maximum; that workload is too close to one core's capacity to certify.
Full runs and exact binary SHA-256 values:
`reports/saturator-cpu/callbacks.json`.

The initial engine-only baseline was 2.308 s per 10 s of stereo audio; intermediate
builds measured roughly 2.189 s after polyphase-history changes, 1.899 s after
removing redundant dry decimators, 1.545 s with curve/gain evaluation changes,
and 1.038 s with stage batching. These are individual diagnostic runs, not an
additive attribution model. `perf` was unavailable; an instrumented gprof run
localized work to Engine::sample but did not resolve inlined stages/shared libm.
The final actual-CLAP paired measurements above are the comparison to use.

## Audio measurements and acceptance bounds

`tools/measure_saturator.py` independently constructs FIRs in SciPy and uses
RBJ/Butterworth direct-form equations for crossover/tone behavior. It checks
actual output, not just generated coefficients. Tone/dynamics residuals are
around 1.2e-12, dry response error stays below 0.0045 dB through the tested 20 kHz
points, and the asymmetric DC test settles below 1e-6. The old 128× two-tone
comparison remains approximately −87 to −94 dB relative residual, unchanged.

`tools/measure_saturator_quality.py` adds 576 coherent-sine cases:

- 44.1, 48, 96 and 192 kHz;
- all four styles; 12, 24 and 36 dB drive;
- −18, −6 and 0 dBFS input peaks;
- approximately 1, 7, 13 and 19 kHz, exact FFT-bin frequencies;
- at least one second of settling, then an 8192-sample FFT.

The explicit intended-use gate is **unwanted residue below −90 dBc in
20 Hz–20 kHz, with drive ≤24 dB and input peak ≤−6 dBFS**. Worst measured:
**−102.75 dBc** (−127.89 dBFS RMS), Dense, 44.1 kHz, 24 dB drive, −6 dBFS input,
approximately 19 kHz. This is a finite test matrix, not a universal audibility
or alias-free guarantee. Wanted harmonics are excluded; dBc and dBFS are both
reported so gain compensation cannot conceal the ratio.

The same report retains **full-Nyquist** residue. This distinction matters:
at 96 kHz a ~7 kHz fundamental produces an unwanted ~47 kHz component in the
resampling filter's transition region, giving approximately −35.5 dBc over
0–48 kHz. It is outside the stated audible-band gate. It must remain visible
in the report because later nonlinear processors can intermodulate ultrasonics.
An initial all-Nyquist gate failed; the diagnostic report is retained as
`quality-full-band-investigation.json`. This was not a shipping-audio regression.

A second test uses multitone plus filtered noise, with independently designed
64× and 128× ideal resampling references. Audible-band engine/reference residuals
are approximately **−98 dB** across all styles at 12/24/36 dB drive. The 64×/128×
reference discrepancy is below −182 dB in this test. Full-band differences are
also retained (about −45 to −67 dB): the production FIR rolls off before Nyquist,
whereas the ideal reference has a different transition band. Calling those
full-band differences “aliasing” would conflate filtering and nonlinear errors.
The audible comparison uses a Hann window and 20 Hz–20 kHz spectral projection.

### Extreme settings are not qualified as pristine

The worst measured hot case is Asymmetric at 36 dB drive, 0 dBFS peak input,
~19 kHz at 44.1 kHz: **−41.8 dBc** audible residue, about **−77.8 dBFS RMS** after
100% drive compensation. The earlier −6 dBFS/36 dB case is around −55 dBc.
Changing compensation/output gain changes the absolute level, not the alias
ratio. These settings may be useful for aggressive distortion, but they do not
meet the intended-use −90 dBc gate. No claim of transparent mastering at every
possible setting is warranted.

## Alternatives investigated

[FabFilter documents 8× and 32× quality modes](https://www.fabfilter.com/press/1589878800/fabfilter-releases-fabfilter-saturn-2-distortion-and-saturation-plug-in).
Matching that factor alone does not establish matching quality; filter response,
nonlinearity, level and modulation all affect the result.

[Werner and Azelborn, DAFx 2023](https://www.dafx.de/paper-archive/2023/DAFx23_paper_61.pdf)
describe polynomial antiderivative antialiasing and combining it with oversampling.
[Gabrielli and Squartini, DAFx 2025](https://www.dafx.de/paper-archive/2025/DAFx25_paper_30.pdf)
investigate LUT-based antiderivatives, including numerical error tradeoffs.
Our shipping tables evaluate the original static functions; they do **not**
implement ADAA. The original offline experiment
`tools/investigate_saturator_adaa.py` uses analytic first antiderivatives and a
small-difference fallback, the documented crossovers/FIRs, −6 dBFS at ~17.6 kHz:

| Method | Worst residue, 24 dB drive | Worst residue, 36 dB drive |
| --- | ---: | ---: |
| Retained direct 32× | −105.9 dBc | −55.3 dBc |
| First-order ADAA, 8× | −72.5 dBc | −57.2 dBc |
| First-order ADAA, 16× | −104.7 dBc | −76.1 dBc |

First-order ADAA also adds half an oversample of delay and a linear-regime
averaging response: at 20 kHz/48 kHz, −0.117 dB and 0.0625 host samples at 8×;
−0.0291 dB and 0.03125 samples at 16×. It improves the extreme case, but an 8×
replacement would worsen the tested normal-drive result. A future mode needs
explicit phase/dry alignment, numerical and modulation tests, state migration,
and listening comparisons. It was not silently substituted in this revision.

## Remaining studio qualification

Bitwig scratch-project playback, automation recording, save/reopen, duplication,
offline exports, multiple visible editors and paced real-time scheduling remain
unverified. Synthetic-host success and unpaced timing do not establish those.
The four original smooth mathematical styles are not measured tape/tube hardware
models, and no direct Saturn listening/measurement comparison was performed.

`tools/saturator_program_material.py` prepares six-second excerpts from a local
[EBU SQAM archive](https://qc.ebu.io/testmaterials/523/): double bass, castanets,
cymbal, piano, soprano and English speech. Each source has phase-matched dry plus
four processed variants, latency-aligned and RMS-matched with estimated 4× peak
headroom. The local pack is `reports/saturator-cpu/program-material/`; its manifest
records source/binary hashes, offsets, settings, gains and old/new residuals.
All 30 renders passed finite-output and regression checks: maximum old/new sample
error **4.07e-13**, worst error RMS **−260.96 dBFS** before audition matching.
Five sets target −24 dBFS RMS; castanets target −30.71 dBFS to retain transient
headroom. Maximum estimated 4× peak across the matched files is −0.5 dBFS.
Source audio and renders are retained solely as R&D material under EBU's terms,
not included in plugin distribution or covered by the repository's MIT license.
RMS matching is not a perceptual loudness certification. The renders were
generated and numerically checked; **human listening has not been performed**.

Level-matched listening on these recordings and representative studio stems and
complete mixes remains necessary. Assess attack clarity, low-end phase behavior,
stereo image, high-frequency grit, automation transitions and CPU in the target
session. Separate wanted saturation from unwanted artifacts. Existing projects
and other installed plugins are not changed by this investigation.
