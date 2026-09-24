# De-esser plan and audio contract

First target: Linux / Bitwig, CLAP only. Version 0.1.0 is a measured alpha,
not a claim of FabFilter equivalence or completed professional qualification.
The provided Pro-DS image guides control hierarchy, not artwork or algorithms.

## First milestone

Independent double-precision DSP; mono/stereo float/double CLAP; detector
high-pass/low-pass, Vocal/Allround detection, Threshold/Range, Wide/Split band,
0–15 ms lookahead, stereo link, detector audition, external sidechain, trims,
attack/release. Native suite materials, original approved DS wordmark, continuous
activity history, two prominent dials, detection range handles, exact entry,
reset, A/B, undo and starting points. No additional runtime dependency.

## Permanent contract

Plugin ID `org.openfilter.deesser`, magic `OFDSSTAT`, schema 1. IDs in order:
0 Bypass (off), 1 Output (0 dB), 2 Threshold (-30 dBFS), 3 Range (6 dB),
4 Detection Low (6000 Hz), 5 Detection High (14000 Hz), 6 Lookahead (5 ms),
7 Stereo Link (100%), 8 Detection (0 Vocal / 1 Allround),
9 Processing (0 Wide Band / 1 Split Band, default 1), 10 Audition (off),
11 Sidechain (0 Internal / 1 External), 12 Attack (0.5 ms),
13 Release (80 ms), 14 Input (0 dB). Modulation never changes base values.

All parameters ramp over 10 ms in physical units, including discrete mode
crossfades; state stores base values only. Events enter ramps at sample offsets.
Detection edges are sorted for DSP only (base values remain untouched), separated
by at least 10 Hz, and clamped below 0.475 sample rate. At low sample rates the
usable band is correspondingly restricted.

Detection uses cascaded second-order Butterworth high/low-pass TPT SVFs and
1 ms power averaging. Vocal weights attenuation by smoothstep of the detected
band/broadband power ratio, from -18 to -6 dB; this is a spectral heuristic,
not phoneme recognition. Allround omits that weighting. The gain computer uses
8:1 compression with a fixed 6 dB quadratic knee, bounded by Range. Linking
interpolates channel requests toward their maximum. The dB envelope has explicit
attack/release one-pole time constants. There is no hidden auto makeup.

Latency is always ceil(rate * 15 ms), including bypass and audition. A fractional
read of the request ring controls lookahead without moving the audio read.
Detector audition returns the delayed filtered detector signal. Input trim
travels with the delayed program; output trim follows processing. Bypass returns
delayed untrimmed input. Reset clears all delay/filter histories and modulation.

Wide Band applies one gain to the whole signal. Split Band applies a TPT
Butterworth high-shelf cut centered on Detection Low with gain equal to the
negative reduction envelope. Detection High affects detection only. This is a
minimum-phase dynamic shelf, not Pro-DS's linear-phase crossover. Zero reduction
is transparent after latency. Range bounds the requested high-frequency cut,
not instantaneous sample peaks; this is not a limiter. No oversampling, M/S-only
processing, or proprietary sibilance classifier is advertised.

## Verification and qualification

Run CTest, independent NumPy/SciPy measurements, clap-validator, ASan/UBSan,
UI/native host gesture backpressure tests and normal/compact/2x/menu/entry/help
previews. Re-render EQ including 24 bands after shared brand-path additions.
Test actual audio: unity, detector selectivity, band preservation, envelope,
range, link, rate matrix, finite automation, in-place processing, partition
invariance and invalid state. Test host automation/modulation/state separately.

Pending release gates: level-matched real-vocal/cymbal listening across voices,
false-positive/lisping evaluation, automation stress in Bitwig, save/recall and
export in a scratch project, worst-callback profiling. Do not mark these complete
from a synthetic host or mathematical agreement alone.

## Research and attribution

- FabFilter's public workflow and documented linear-phase distinction:
  https://www.fabfilter.com/help/pro-ds
  https://www.fabfilter.com/help/pro-ds/using/advancedcontrols
- Shared TPT SVF uses Andrew Simper's published equations and retained suite
  attribution; see docs/research.md and THIRD_PARTY_NOTICES.md.

Only original suite code and approved suite artwork are included.


## Waveform display correction

Replace the initial dB history traces with a centered signed input waveform and
green filtered-band highlights during applied reduction, following the supplied
reference's visual structure. Five-millisecond extrema cover both stereo channels
without summing them. Read-only telemetry uses existing delayed input/detector
rings; no audio equation, parameter, state schema or latency changes. Square-root
amplitude mapping is display-only, with a zero baseline and no dBFS graph labels.
The waveform no longer acts as a threshold editor; the dial and exact entry retain
threshold editing/reset/undo. GR remains on the dedicated right-hand meter.


## Live spectrum in the detection selector

The detection-band strip now shows live frequency energy from the actual
filtered detector feed, including the external sidechain. It reuses the suite's
4096-point Hann FFT/triple-buffer tap; capture is bounded and only occurs with
the editor visible, and FFT/drawing stay on the main thread. The 1–22 kHz
logarithmic selector highlights in-band energy in green and mutes out-of-band
filter rolloff. Per-pixel peak aggregation preserves narrow tones. The display
has a -90 to -6 dB amplitude window, empty content above Nyquist and a stopped-feed
decay. Energy is visible below Threshold; it is distinct from the large waveform's
reduction-gated green highlight. Audio equations, parameters and state are unchanged.
