# Research notes and primary sources

Reviewed 2026-09-22. These notes distinguish published behavior from proposed
OpenFilter engineering. Vendor documentation is not proof of an internal algorithm
or independent verification of marketing claims.

## DSP research

- [Valhalla Supermassive](https://valhalladsp.com/shop/reverb/valhalla-supermassive/):
  published controls and algorithm descriptions discuss delay networks, density,
  modulation, and feedback filtering. Reverb quality involves those coupled
  behaviors and requires dedicated listening and decay analysis.
- [RBJ Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/): baseline
  coefficient formulas and parameter conventions for reference testing.
- [Vicanek, Matched Second Order Digital Filters](https://vicanek.de/articles/BiquadFits.pdf):
  published analog-magnitude matching approaches that address high-frequency
  cramping. Candidate methods to measure before choosing an implementation.
- [Cytomic technical papers](https://cytomic.com/technical-papers/): filter
  realizations, numerical comparisons, and smoothing research from a commercial
  audio developer. Useful primary material for our automation investigation.

## Integration and framework sources

- [CLAP](https://github.com/free-audio/clap): official API and extension contracts.
- [Parameter contract](https://github.com/free-audio/clap/blob/main/include/clap/ext/params.h)
  and [events](https://github.com/free-audio/clap/blob/main/include/clap/events.h):
  source of truth for values, modulation, timing, gestures, and thread rules.
- [clap-helpers](https://github.com/free-audio/clap-helpers): MIT-licensed C++
  helpers for implementing CLAP plugins.
- [clap-validator](https://github.com/free-audio/clap-validator): automatic
  validation and experimental fuzzing. Investigate failures and retain seeds.
- [JUCE license](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md):
  current master lists AGPLv3/commercial terms and references JUCE 9. Verify the
  exact selected revision's terms before adding the dependency.
- [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions):
  MIT-licensed, unofficial JUCE adapter with documented production users; not
  JUCE-team-supported. Its current README explicitly names versions 6, 7 and 8.
- [DPF](https://github.com/DISTRHO/DPF): C++ plugin framework with CLAP output and
  an ISC framework license. A credible alternative if permissive licensing is
  preferred; inspect individual dependency notices and needed CLAP features.
- [iPlug2](https://iplug2.github.io/): another C++ option with CLAP support; its
  homepage's listed official platforms omit Linux, so it is not the initial
  recommendation for this Linux-first project.

## Conclusions

There is no single professional-quality switch. The proposed EQ combines
explicit response targets, stable parameter movement, numerical robustness,
real-time discipline, trustworthy metering, reliable host integration, and a
fast editing workflow. These are recommendations derived from the sources and
the user's requirements. This was the initial research basis; implementation
progress and subsequent measurements are recorded in [status.md](status.md).

## Brickwall and clarity revision — 2026-09-22

The editor uses slim toolbars, a floating band panel, cut fills and a separate
meter axis. Brickwall cuts use an independently implemented 20th-order elliptic
filter with 0.01 dB passband ripple and a 100 dB stopband, using the existing
SVF state representation.

SciPy's [ellipap documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.ellipap.html)
defines the normalized passband edge and analog pole/zero prototype;
[ellip](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.ellip.html)
provides the independent digital SOS reference used against rendered audio.
The reproducible prototype generator uses the pinned development-only SciPy
version; no SciPy implementation is compiled into the plugin.


## Limiter workflow research — 2026-09-24

Inspected FabFilter's official Pro-L 2 overview image and help. Adopted the
workflow hierarchy (gain fader, large history, right-hand output/reduction,
collapsible timing controls) while drawing original native OpenFilter artwork.
No proprietary DSP or graphics were copied.

- [Overview and interface](https://www.fabfilter.com/help/pro-l/using/overview)
- [Timing and channel linking](https://www.fabfilter.com/help/pro-l/using/advancedsettings)
- [True-peak requirements](https://www.fabfilter.com/help/pro-l/using/truepeaklimiting)

The initial engine deliberately claims sample-peak limiting only. Reconstructed
peak tests demonstrate why true-peak qualification is a separate next milestone.

## Limiter engine research — 2026-09-24

[Limiter DSP design](limiter-dsp.md) records the primary references, original
equations, engineering decisions and measured qualification gates for 0.2.0.
The investigation compared short and long reconstruction filters, exposed
short-filter under-reading on noise/Nyquist bursts, and measured deep-bass gain
ripple before selecting the final hold and detector. This is independent
engineering; vendor workflow documentation does not disclose vendor source code.
