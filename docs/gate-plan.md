# Gate / expander plan and audio contract

First target: Linux / Bitwig, CLAP only. Reference arrangement: the supplied
Pro-G screenshot. Use original suite graphite controls and the approved G
wordmark; no reference artwork or proprietary algorithm is incorporated.

## First milestone

Independent double-precision downward expander with threshold, ratio 1–100,
quadratic knee, maximum attenuation, opening attack, closing release, hold and
hysteresis. Internal/external Peak or RMS sidechain with detector-only HP/LP,
audition, stereo link, 0–10 ms lookahead, input/output trim, wet/dry gain,
wet/dry balance and parallel mix. Native editor: large left threshold, lower
left ratio/range, central transfer/history and meters, right timing controls,
expandable lower expert/sidechain section. Exact entry, default reset, balanced
host gestures, presets, A/B and undo follow the suite contract.

## Permanent host/state contract

Plugin ID `org.openfilter.gate`, state magic `OFGTSTAT`, schema 1. Permanent IDs:
0 Bypass, 1 Output, 2 Threshold, 3 Ratio, 4 Attack, 5 Release, 6 Knee, 7 Range,
8 Wet gain, 9 Mix, 10 Input, 11 Lookahead, 12 Hold, 13 Sidechain HP,
14 Stereo Link, 15 Detector (0 Peak / 1 RMS), 16 Hysteresis,
17 Sidechain (0 Internal / 1 External), 18 Sidechain LP, 19 Audition,
20 Wet pan, 21 Dry pan, 22 Dry gain. Units/defaults in Parameters.hpp.
Modulation remains separate from base state. All changes ramp over 10 ms in
physical units; event offsets are preserved. No process-time allocations,
locks, logging, UI work or file access. Fixed arrays support 1–768 kHz.

## Equations and timing

For x = threshold - detector dB and slope = ratio - 1, attenuation is
slope * max(x, 0) outside the knee, and slope * (x + knee/2)^2 / (2*knee)
inside |x| < knee/2, capped by Range. Ratio 1 is unity. Ratio 100 approximates
a hard gate; there is no advertised infinite ratio or proprietary style.
Peak follows instantaneous magnitude with 5 ms decay; RMS uses a 5 ms power
average. HP/LP are first-order filters (6 dB/octave), with 0 Hz HP and 20 kHz LP
meaning Off. Coefficients are limited to 0.45 * sample rate.

Stereo linking raises each detector toward the louder channel before computing
the gate. Above threshold + knee/2 the gate arms and reloads Hold. While armed,
hysteresis lowers the closing boundary by its dB amount. Below that boundary,
Hold counts exact samples; while armed/holding the target remains zero. Once
Hold expires, normal expansion resumes. Hysteresis=Hold=0 gives the static curve.
Attack moves attenuation toward a smaller target; Release moves toward a larger
one. Both are dB-domain one-pole time constants (63.2% in the stated time);
zero attack is immediate. Initial/reset attenuation is the silence curve, so
there is no initial noise burst. Short times can create modulation distortion.

Audio and bypass always have ceil(rate * 10 ms) latency. Lookahead advances the
detector within that fixed delay with fractional interpolation. Input gain is
stored with its audio sample. Audition is the filtered sidechain at the full
latency, crossfaded smoothly, before output trim. Bypass returns delayed raw
input. Wet and dry balance attenuate the opposite stereo channel linearly;
center is unity on both channels; pan has no effect on mono. Mix is linear and
latency aligned; dry remains inaudible at 100% Mix regardless of Dry gain.

## Qualification

Actual audio and independent Python reference across six rates; gate opening,
closing, hold, hysteresis, link direction, filters, latency, audition, mix/pan,
finite extremes and allocation guards. CLAP offset/partition invariance,
modulation, state rejection/round-trip, native editor backpressure/lifecycle.
Run CTest, measurements, validator, sanitizers, UI/native host tests and inspect
normal/compact/2x/menu/expert previews plus the EQ 24-band view.

Pending professional release gates: Bitwig scratch-project playback,
automation, external routing, save/reopen/export, matched-level listening on
vocals/drums/bass, worst callback and multi-instance profiling, prolonged use.
No oversampling, M/S processing, MIDI triggering, proprietary style matching or
professional-release qualification is claimed by this first alpha.
Tail reporting is conservatively infinite because filtered sidechain audition
has an IIR tail; silence-based suspension remains pending.


## Visualizer clarity revision

Keep the existing control arrangement and native suite materials. Show actual
latency-aligned input/output peaks as two filled, fading silhouettes, with the
white transfer curve and threshold crosshairs in front. Remove the overlaid GR
trace, unity diagonal, hysteresis guide and vertical grid; GR retains its own
meter. Curve-hidden mode labels the horizontal axis as time. This changes only
UI painting and preview material, not audio, telemetry, IDs or saved state.
