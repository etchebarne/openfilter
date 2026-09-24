# Limiter plan and audio contract

First target: Linux / Bitwig, CLAP only. Original implementation and suite
materials. FabFilter Pro-L 2 is the layout/workflow reference, not a claim of
algorithmic or sonic equivalence.

## Current implementation: 0.3.0 qualification candidate

The dual-stage engine, original styles, adjustable lookahead, attack, separate
linking and reconstructed-peak guard are implemented. Revision 0.3 adds 4× audio
oversampling, a Nyquist safety filter, efficient multistage peak detection and
bounded queue maintenance. The user owns Bitwig and real-material listening tests. Schema 1 states migrate
to Legacy; schema 2 appends parameter IDs 7–11. Fixed latency increased to
914 samples at 48 kHz, including the new filters (other rates in the DSP contract). See [the current DSP contract](limiter-dsp.md)
for equations, parameter semantics, research, compatibility and qualification
boundaries, and [status](status.md) for measured evidence. The original contract
below documents the Legacy engine retained for migration.

## First build: 0.1.0 sample-peak alpha

Independent stereo/mono double-precision engine, float/double CLAP, stable
5 ms lookahead/latency including bypass. Input gain drives a unity-threshold
limiter; output ceiling attenuates the limited result. Default ceiling -1 dBFS.
Seven permanent parameter IDs: 0 Bypass (off), 1 Gain (0..36 dB, default 0),
2 Ceiling (-24..0 dBFS, default -1), 3 Release (20..2000 ms, default 200),
4 Stereo Link (0..100%, default 100), 5 Auto Release (off/on, default off),
6 Unity Gain (off/on, default off). Toggles are not modulatable. Continuous
parameters accept global modulation without changing stored base values.

The gain computer uses a sliding minimum of required linear gain over D+1
samples, where D = ceil(rate * .005). It holds this minimum for an additional
10 ms to reduce low-frequency gain ripple. An instantaneous-down/exponential-up
release stays at or below this minimum, followed by a D+1-sample moving average.
Every gain value in that average was constrained by the delayed audio sample:
this is the sample-ceiling proof, without using a waveshaper as the limiter.
Full linking applies the same gain to both channels. Partial linking only adds
attenuation. Auto release lengthens the selected time up to fourfold from a
250 ms attenuation average. Unity Gain undoes the delayed input gain (leaving
ceiling trim) for comparison; it never increases output beyond the ceiling.

Gain is smoothed at input before delay. Ceiling, link, release and toggles use
10 ms sample ramps. Ceiling is output trim and applies at output time. Bypass
crossfades to untrimmed delayed input and consequently suspends the ceiling
contract during and after bypass. A numerical output bound only absorbs rounding
error. Non-finite input becomes zero and magnitudes are bounded at 1e12.
Fixed storage; no allocation, locking, logging or UI calls on the audio thread.

Stable plugin ID org.openfilter.limiter; state magic OFLMSTAT, schema 1. Save
base values only, checksum and strictly validate states, preserve bounded queue
backpressure and balanced UI gestures. No existing plugin IDs or sound changes.

## Editor

Original outlined L wordmark, graphite theme, prominent vertical gain slider,
six-second input/output/attenuation history, separate output and reduction
scales, collapsible release/link panel, ceiling and bypass footer. Exact entry,
double-click descriptor reset, keyboard navigation, A/B, undo/redo, and starting
points reuse suite conventions. The footer identifies sample-peak metering;
meter units are documented in limiter.md. A latched CLIP warning appears only
when clipping has occurred.

The 0.1.1 layout follows the supplied Pro-L 2 screenshot more closely: continuous
history canvas behind the integrated gain handle, small readouts over tall meters,
and a shallow lower-left advanced strip. Original graphite materials remain.
The gain handle supports click-to-edit and drag-to-adjust with balanced host
gestures. Audio, parameter descriptors and state schema are unchanged.

## Validation and remaining release gates

Actual audio: ceiling for impulses/random/transients/extreme gain; transparent
subthreshold delay; stereo image and partial link; independent reference
render; release/auto release; rate matrix; sample-event partition invariance;
allocation guards; in-place processing; state/modulation; native gestures under
backpressure. Run all CTest suites, independent measurement script, external
CLAP validator, ASan/UBSan, editor previews at normal/compact/2x/menu, and existing
EQ 24-band preview. Record results in status.md.

The original 0.1 scope left reconstructed peaks and oversampling unimplemented;
0.3 implements and numerically qualifies those stages. Real-material listening
and Bitwig automation/recall/export remain user acceptance gates. LUFS and dither
are optional future additions. Never infer Bitwig validation from synthetic tests.

## Primary references

- https://www.fabfilter.com/help/pro-l/using/overview (layout and gain workflow)
- https://www.fabfilter.com/help/pro-l/using/advancedsettings (timing/link workflow)
- https://www.fabfilter.com/help/pro-l/using/truepeaklimiting (distinction between
  sample peaks and reconstructed peaks; release quality gate)

Reference product source code and artwork are not included.
