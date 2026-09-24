# Reverb 0.2: audio investigation and first refinement

This is an engineering qualification milestone, not a declaration of professional
production readiness or FabFilter sonic equivalence. The implementation is original.
No FabFilter audio was available for a controlled comparison. The listening pack
contains synthetic probes; no subjective listening assessment is claimed.

## Research and decisions

- Julius O. Smith's [delay-line interpolation discussion](https://dsprelated.com/freebooks/pasp/Delay_Line_Interpolation.html)
  explains the gain error of linear interpolation and its significance inside
  nearly lossless feedback loops. Our baseline showed that problem in rendered
  audio. Rather than spend more CPU on higher-order interpolation in every loop,
  the new tank uses integer delays and modulates its mixing matrix.
- Schlecht and Habets, [Time-varying Feedback Matrices in Feedback Delay Networks
  and Their Application in Artificial Reverberation](https://www.audiolabs-erlangen.de/fau/assistant/schlecht/publications/)
  (JASA, 2015), supports unitary matrix modulation as a stable approach. Our
  implementation applies four slowly varying Givens rotations after normalized
  Hadamard scattering. Each rotation preserves instantaneous vector energy;
  modulation no longer imposes fractional-delay filtering on the loop.
- Schlecht and Habets, [Scattering in Feedback Delay Networks](https://arxiv.org/abs/1912.08888)
  motivates lossless delay/mixing structures for increasing density efficiently.
  We use that principle in an original **feedforward input diffuser**, not their
  full feedback architecture: three stages of eight short integer delays and
  normalized Hadamard mixing. Each stage preserves summed channel energy and
  creates more paths before injection. The feedback tank still has eight lines.
- Fagerström et al., [Velvet-Noise Feedback Delay Network](https://www.dafx.de/paper-archive/2020/proceedings/papers/DAFx2020_paper_23.pdf)
  highlights density/cost benefits and coloration tradeoffs in sparse filters.
  A four-tap-per-line sparse-input experiment improved buildup but raised our
  spectral-ripple diagnostic appreciably. It was rejected in favor of the
  lossless input diffuser. Adding more taps is not automatically better sound.
- Dal Santo et al., [Efficient Optimization of Feedback Delay Networks for Smooth
  Reverberation](https://arxiv.org/html/2402.11216v2) connects modal excitation,
  coloration, and density, with both objective and perceptual evaluation.
  This informed keeping separate density and spectral diagnostics rather than
  treating either as a sound-quality score. Modal optimization is a next-stage
  candidate; no learned model or research implementation was imported.
- [FabFilter's main-control documentation](https://www.fabfilter.com/help/pro-r/using/maincontrols)
  supplies the user-facing benchmark: smooth space transitions, controlled
  modulation, early-reflection character, and density. It does not disclose an
  implementation we can reproduce. Similar controls do not imply sonic parity.

## Implemented sound changes

The previous early path was a single delayed copy. Refined adds twelve signed
reflections per output, spread over roughly 67 ms with stereo timing differences
and some crossfeed. These are designed taps, not a measured room simulation.
The existing Thickness-dependent allpass input stages remain, followed by the
three-stage lossless diffuser. Plate/Vintage/Modern remain original voicings.

Static tank delays are rounded to samples. Space changes use two fixed read
positions with a 50 ms smoothstep crossfade; predelay uses a 30 ms crossfade.
Rapid requests coalesce until the current transition finishes, so automation
can lag by a transition window. This trades Doppler sweeps for brief blending
and possible phase cancellation. It is not a claim of artifact-free arbitrary
modulation. Stored/base parameter events still occur at their host sample offsets.

Post EQ and decay-EQ shape changes use two SVF states and a 20 ms smoothstep
crossfade. The old branch finishes with its old coefficients; the new branch
starts clean. Rapid requests coalesce rather than abruptly restarting a blend.
Frequency/gain/Q changes continue to use the existing smoothing and 32-sample
coefficient clock. No oversampling, saturation or automatic gain compensation
has been added. Character now modulates scattering rather than delay length.

Neutral or disabled decay sections and disabled post sections avoid unnecessary
sample processing. Gain conversions and coefficient generation are cached by
the parameters that actually affect them. Work remains bounded, with no audio
allocation, locks, file access, FFT or GUI calls. A new instance's Engine occupies
22,564,840 bytes on this build; buffers are allocated as part of the instance.

## Reproducible measurements

Run with the pinned Python/toolchain environment:

```sh
cmake --build --preset release
ctest --preset release
python tools/measure_reverb.py
python tools/measure_reverb_quality.py --legacy --output reports/reverb-quality/baseline.json
python tools/measure_reverb_quality.py --check
python tools/qualify_reverb.py
python tools/plot_reverb_quality.py
build/release/reverb_benchmark 48000 64 30 default
build/release/reverb_benchmark 48000 64 30 automated
build/release/reverb_benchmark 96000 64 30 automated 4
bash tools/check-clap.sh build/release/plugins/OpenFilterReverb.clap
```

`measure_reverb_quality.py --legacy --output reports/reverb-quality/legacy.json`
reproduces old-engine measurements through the retained compatibility path.
The independent baseline executable used for development came from commit
91be315 (0.1.1); it is retained only under ignored reports, never versioned.

At 48 kHz, 100% wet, zero predelay, Character 0, Brightness 100:

| Test | 0.1.1 | Refined |
| --- | ---: | ---: |
| 8 kHz octave T60, nominal 8 s | 5.151 s | 8.006 s |
| Buildup/mixing time, nominal 2.5 s | 360 ms | 110 ms |
| Buildup/mixing time, nominal 8 s | 555 ms | 205 ms |
| Local spectral ripple, nominal 0.65 s | 1.34 dB | 1.11 dB |
| Local spectral ripple, nominal 2.5 s | 0.88 dB | 1.08 dB |
| Local spectral ripple, nominal 8 s | 0.55 dB | 0.59 dB |

The spectral result is deliberately reported alongside the density improvement:
**not every setting became flatter**. More modal/voicing work is justified, and
these small ripple differences cannot establish audible preference by themselves.
The rejected sparse-input candidate measured 1.32 dB at 2.5 s and 1.04 dB at 8 s.

T60 uses independent octave-band Butterworth filtering and a Schroeder energy
integral, fitted from -5 to -25 dB and extrapolated. Buildup uses normalized echo
density: samples outside one standard deviation, divided by the Gaussian
expectation, averaged across channels in 20 ms windows at 5 ms hops. Mixing time
requires ten consecutive windows at density >= 0.9. This operational measure
is sensitive to sample rate, windowing and material; it is not a universal
perceptual threshold. All nine neutral rate/space cases pass the 1 kHz/8 kHz
T60 tolerance of 8%, and the 12 neutral/style cases reach mixing within 300 ms.

The ripple diagnostic computes narrow log-band power between 200 Hz and 12 kHz,
subtracts a broad local average and reports residual standard deviation. It is
not a full modal-excitation analysis, nor a calibrated audibility score.

For 997 Hz tone transitions, energy outside a 897–1097 Hz exclusion band during
0.98–1.4 s measures roughly 38 dB less for predelay, 43 dB less for Space and
43 dB less for a post-EQ shape switch. These are controlled stimuli with identical
parameter requests, not guaranteed improvements on every possible signal.

Freeze on a noise-excited tail retains the 5.6–11 kHz band within 0.01 dB between
1–3 s and 6–8 s windows; the old engine loses about 24.24 dB over those windows.
That result applies to stationary frozen settings. Changing Space or active
filters during Freeze can still change the stored energy and sound.

Results, plots, benchmark logs and matched WAVs live under
`reports/reverb-quality/`. The WAV pack contains plucked, percussive and sustained
synthetic material in wet-only and 75/25 mixes. Wet RMS is matched separately per
phrase, with a shared peak-safety gain; exact settings/gains are in qualification.json.

[Measurement plots](../reports/reverb-quality/quality-comparison.png) and
[listening guide](../reports/reverb-quality/listening/README.md) are generated
locally by these scripts; reports and audio are intentionally not committed.

### Performance on the development machine

Release build on an AMD Ryzen 5 5600GT, 64-sample blocks, measured with thread
CPU time. Percentages are fractions of one CPU core at real-time throughput,
not the DAW's CPU meter. Baseline probes process 20 seconds of audio; Refined
probes process 30 seconds. Both run faster than real time, without pacing.

| Probe | 0.1.1 CPU | Refined CPU |
| --- | ---: | ---: |
| One instance, 48 kHz, default | 2.75% | 1.44% |
| One instance, 48 kHz, all bands active + automation | 2.97% | 3.13% |
| Four instances serially, 96 kHz, all bands + automation | Not measured | 26.14% total |

The default benefits from skipping neutral filters and caching coefficients;
the fully active, moving case is slightly more expensive. In the four-instance
probe, aggregate per-block CPU time was 224 microseconds at the 99th percentile
and 515 microseconds maximum. Maximum observed wall time was 581 microseconds
against a 667-microsecond block interval, with no observed deadline exceedances
in this run. These figures do not establish a worst-case bound. An earlier,
sample-interleaved four-instance stress run exceeded the interval once; it is
retained in reports, but the final benchmark follows hosts' usual whole-block
plugin scheduling. No SIMD-specific optimization or new runtime dependency was
needed. Matplotlib is pinned only for the optional offline measurement plots.

## Compatibility and remaining qualification

The plugin ID and all 80 parameter IDs/units/enumerations remain unchanged.
State schema 2 appends an explicit engine revision before the checksum. Schema 1
loads as revision 1 (Legacy), and a resaved legacy state remains Legacy. New
instances use revision 2 (Refined). The footer identifies the engine. Create a
new instance to use Refined when comparing an older saved session. State loading
across engine revisions clears the tail; it does not morph between algorithms.
The unchanged legacy sound has binary-null render coverage and actual CLAP
save/load/audio regression coverage. No existing DAW project was modified.

Still pending: level-matched human listening against Pro-R on dry recordings;
voicing across a wider preset/material matrix; broader modal coloration and
mono compatibility assessment; long sessions and large host projects; actual
Bitwig automation/recall/export testing. Standalone CPU timing excludes CLAP
bridge, UI, host scheduling, revision-change buffer clearing, and DAW workload.
A maximum observed callback time is not a real-time scheduling guarantee.

Further work should prioritize listening-led modal/output-weight optimization,
room-dependent early reflections, and wider transition/overload sweeps. Simply
increasing FDN size or adding nonlinear processing is not justified by the
current evidence. The intended outcome is professional quality; this pass
provides measurable progress and a concrete listening candidate, not certification.
