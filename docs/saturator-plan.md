# Saturator audio and implementation contract

First target: Linux / Bitwig, separate CLAP artifact, original DSP and suite UI.
Read alongside development.md, architecture.md and ui-conventions.md.

## Initial product

Three permanent bands with 24 dB/oct Linkwitz–Riley crossovers, initially 250 Hz
and 4 kHz. Spectrum-led editor: draggable crossover boundaries and band levels,
selected-band drive, style, dynamics, four tone controls, mix, level, enable,
solo and mute. Global input/output, mix, bypass and drive compensation. A/B,
undo, presets, exact entry, double-click descriptor reset and host modulation.
Four original smooth waveshapers: Soft, Rounded, Dense and Asymmetric. These
are mathematical voicings, not measured tape/tube/transformer emulations.

## DSP contract

Split at host rate with cascaded Butterworth sections; compensate the low band
with the second crossover's allpass (LP + HP) so all three sum to an allpass.
Crossover 1 is clamped below crossover 2 in the effective DSP only; stored host
values remain independent. Each band runs through rate-adaptive linear-phase FIR
oversampling, using the suite's proven sparse half-band primitive. The nonlinear
stage runs at 32x through 48 kHz; matched wet and dry decimators precede
host-rate linear tone filters, residual DC rejection and mixing. Global mix
uses the identically filtered dry path. Bypass uses the latency-aligned raw
input. Normal processing has minimum-phase crossover rotation; bypass does not.
Fixed 76 sample FIR round-trip latency at all rates. Continuous parameters and
style weights ramp over 10 ms in samples, not blocks. Toggle/solo transitions
are ramped too. Stereo-linked dynamics, independent channel waveshaper histories.
No audio-thread allocation, locks, logging, GUI, files or unbounded work.

Drive is pre-gain, 0–36 dB. Compensation continuously scales inverse drive,
0–100%; at 100% the small-signal gain remains unity. This is deterministic gain
compensation, not a loudness matcher. Nonlinear DC is removed only from the
wet-minus-dry residual with a 5 Hz blocker, preserving the dry response.
Tone: low shelf 160 Hz, bell 800 Hz, bell 3 kHz, high shelf 8 kHz, Q=sqrt(.5),
±12 dB. Tone and dynamics are inside the wet path. Dynamics uses a linked
10 ms attack / 100 ms release envelope and a -18 dBFS pivot, smoothly morphing
between expansion and compression. No feedback oscillator in this milestone.

## Permanent state

Plugin ID org.openfilter.saturator; magic OFSTSTAT; schema 1.
Global IDs 0–6: bypass, output dB, input dB, global mix %, lower/upper crossover
Hz, drive compensation %. Band IDs start at 7 with stride 12: enabled, style
(0 Soft, 1 Rounded, 2 Dense, 3 Asymmetric), drive dB, mix %, level dB, dynamics
%, bass/mid/treble/presence dB, solo, mute. 43 parameters total; descriptor table is canonical.
State stores base values only; transient modulation and DSP histories are not
serialized. Main/audio handoff and UI gesture retry follow existing plugins.

## Qualification gates

Independent audio reference for nonlinear transfer and crossover response;
oversampling alias/high-rate convergence; dry/wet phase alignment; DC and
silence; channel isolation; reset; nonfinite input; sample-rate/parameter extremes;
sample-offset events and partition invariance; no C++ allocation in process or
active flush; partial/corrupt streams and state recall; UI/native GUI tests;
release CTest, sanitizer tests, external clap-validator, visual preview at normal,
compact, 2x and menu sizes plus the suite EQ 24-band view.

Bitwig playback/automation/recall, level-matched musical listening, long-session
stability and worst-callback profiling are separate pending release gates.
Do not describe synthetic-host success as completed Bitwig qualification.

## Research (2026-09-25)

- FabFilter Saturn 2 official band controls and interactive multiband display:
  https://www.fabfilter.com/help/saturn/using/bandcontrols
  https://www.fabfilter.com/help/saturn/using/display
  Workflow reference only; no proprietary code/artwork or emulation claims.
- Parker, Zavalishin & Le Bivic, DAFx 2016, reducing waveshaping aliasing:
  https://github.com/julian-parker/DAFX-AntiAliasing
  Antiderivative antialiasing is a candidate for later severe nonlinear models;
  this milestone measures smooth functions with oversampling fixed per activation instead.

- Linkwitz Lab, crossover/allpass derivations:
  https://www.linkwitzlab.com/crossovers.htm
  https://www.ranecommercial.com/legacy/note160.html
- FabFilter's official description of Saturn's 8x / 32x quality options:
  https://www.fabfilter.com/press/1589878800/fabfilter-releases-fabfilter-saturn-2-distortion-and-saturation-plug-in
  Oversampling factor alone does not establish equivalent sound quality.

### Numerical definitions

Curves: tanh(x), atan(x), x/sqrt(1+x²), and
(tanh(x+0.35)-tanh(0.35))/(1-tanh²(0.35)). All have unit slope at zero.
FIR stage lengths: 129 / 33 / 17 / 17 / 33, Kaiser beta 10, independently
normalized even/odd phases. Round-trip delays: 64 + 8 + 2 + 1 + 1 = 76 samples.
The stereo envelope is the one-pole smoothed maximum absolute band input.
Compression gain is -0.75*d*max(0, envelope_dB+18), expansion gain is
-0.75*d*min(0, envelope_dB+18), where d is the signed Dynamics percentage / 100.
Clamp to [-60,0] dB. The detector precedes the interpolation FIR, so it leads
the oversampled program path by the interpolation delay. Tone uses the shared
TPT filters at host rate; DC rejection is (1-z^-1)/(1-exp(-2*pi*5/fs)*z^-1)
applied only to wet minus matched dry. Band level follows band mix. Enabled
crossfades the complete band processor to matched dry; solo/mute follow it.
Global mix and output trim follow summation. Bypass crossfades to delayed raw
input and excludes input/output trim. Supported activation rates: 1–768 kHz.

Above 48 / 96 / 192 / 384 kHz the oversampling factor becomes 16 / 8 / 4 / 2,
respectively. Unused stages are skipped and 1 / 2 / 4 / 12 samples of output
padding retain the same 76-sample latency. This bounds nonlinear work at high
host rates. Factor changes happen only at activation; they are not automatable.


## 0.1.1 compatibility-preserving optimization

The analytic curves, five FIR designs, crossover/tone coefficients, smoothing
and latency remain unchanged. Cubic Hermite tables evaluate tanh and atan to
absolute errors below 5e-12 over the tested range; Dense retains native sqrt.
Tables initialize once during activation. They are immutable and shared by all
instances of the binary, with no first-use initialization in `sample`.
Gain caches update whenever the sample-smoothed control value changes.

The shared half-band stores even/odd polyphase histories separately in padded
power-of-two rings; convolution arithmetic is unchanged. Saturator batches each
resampling stage in chronological order. Its matched dry FIR is constructed
from the full original interpolation/decimation cascade's impulse at activation,
then evaluated at host rate with symmetry. These paths differ only in floating
point evaluation, covered by independent reference and stored 0.1.0 automation
fixtures. Schema 1 needs no migration; no parameter ID or meaning changes.

Mono processes one channel; channel configuration is fixed between prepare/reset
calls, as enforced by CLAP activation. The exact-silence path is entered only
with zero detector, crossover, resampler, tone, DC and delay histories. It still
advances every parameter/style ramp. It does not truncate a quiet tail, and the
adapter continues processing callbacks. See saturator-quality.md for experiments,
CPU results, passband definitions and remaining production qualification.
