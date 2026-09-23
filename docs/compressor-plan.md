# Compressor plan and audio contract

First target: Linux / Bitwig, CLAP only. Original OpenFilter implementation and
EQ-derived suite materials. FabFilter Pro-C is a workflow/quality reference,
not a claim of identical proprietary algorithms or measured sonic equivalence.

## First usable build

- Independent double-precision feed-forward DSP, mono/stereo, float/double CLAP.
- Peak and RMS detectors, threshold, ratio, quadratic soft knee, maximum range,
  attack/release, hold, optional program-dependent release, stereo linking.
- Input trim, wet makeup, latency-aligned dry/wet, output trim, smoothed bypass.
- Internal/external sidechain, detector high-pass, variable 0–10 ms lookahead.
- Fixed ceil(sample-rate * 10 ms) latency, including bypass/dry, so automation
  never changes compensation. Delay buffers are fixed/preallocated. No oversampling
  or analog emulation claims. Fast time constants can deliberately distort bass.
- Native resizable editor: suite graphite materials and gold/teal accents,
  input/output history, gain-reduction history, interactive transfer curve,
  exact entry, double-click reset, undo/redo, A/B, useful starting presets.
- Stable plugin ID org.openfilter.compressor; independent state magic OFCPSTAT,
  schema 1; permanent IDs 0–17. Enumerations: Detector 0 Peak / 1 RMS;
  Sidechain 0 Internal / 1 External. See Parameters.hpp for units/defaults.

## Detector and timing semantics

Peak detection uses rectified magnitude with instantaneous rise and 10 ms decay.
RMS is a 10 ms one-pole power average (sine level is RMS, not peak calibrated).
A first-order high-pass affects detection only; 0 Hz bypasses it. The detector
level is converted to dB, then a quadratic knee computes requested attenuation.
Per-channel reduction is linked toward the maximum by the stereo-link amount.
Attack/release are one-pole dB-domain time constants (63.2% movement in the
specified time for a constant target). Hold delays release after a falling
request. Auto release blends the selected release time toward 4x that value
using a 250 ms average of reduction. Parameter changes ramp over 10 ms in stored
units; bypass and sidechain selection ramp too. Mode changes crossfade detectors.
Lookahead shifts the detector read position within a fixed 10 ms ring, with
fractional interpolation. Audio is always read at the fixed latency. Makeup
applies to wet only, output trim to the combined signal; bypass returns delayed
untrimmed input. Silence and non-finite input must not poison histories.

## Verification gates

Actual rendered audio against independent Python equations; static knee/ratio,
attack/release/hold, range, detector modes, sidechain filtering, latency, parallel
alignment, mono/stereo, rate matrix, event partition invariance, finite extremes.
Synthetic CLAP host: offsets, modulation/base separation, state checksums and
malformed state, allocation guards, sidechain routing and native UI backpressure.
CTest, measurement scripts, clap-validator, ASan/UBSan, native GUI tests under
Xvfb, and normal/compact/2x/menu previews. Re-render EQ including 24 bands to
check shared styling. Bitwig listening/automation/recall/export remain a separate
manual release gate; never infer them from the synthetic host.

## Sources

- https://www.fabfilter.com/help/pro-c/using/overview
- https://www.fabfilter.com/help/pro-c/using/displays
- Giannoulis, Massberg, Reiss (2012), Digital Dynamic Range Compressor Design:
  A Tutorial and Analysis. https://eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf

No source code or graphics from reference products are included.


## Layout revision 0.1.1

Based on direct inspection of the official Pro-C overview: continuous level
history, optional knee overlay, four dominant compression dials, stacked
makeup/mix, adjacent auxiliary sliders and a collapsible sidechain drawer.
Keeps the EQ's original native graphite materials. Audio and schema 1 are
unchanged. All view changes stay on the main thread and emit no audio events.

## Suite styling revision 0.1.2

Share the original EQ meter painter, including recessed lanes, segmentation and
level gradient. Preserve the compressor layout and give attenuation its own
downward warm lane and dB scale. Align branding, header/footer materials, toolbar
icons, captions, inset values, menus and hover/entry states with the EQ.
Audio, parameters and state schema remain unchanged.
