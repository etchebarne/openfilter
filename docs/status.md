# Project status — 2026-09-24

## Reverb sound investigation and refinement — 0.2.0

**Implemented:** researched feedback-delay-network density, coloration, delay
interpolation and energy-preserving modulation. Replaced fractional tank
modulation with integer reads and unitary matrix modulation in the new Refined
engine. Added three lossless input-diffusion stages and twelve signed early
reflections per channel. Space/predelay read positions and EQ shape changes
crossfade with bounded work. Neutral sections and unchanged coefficients avoid
unnecessary processing. No audio-thread allocation, locks or new runtime
dependencies were added. Matplotlib is a pinned offline plotting dependency.

Schema 2 retains all 80 parameter IDs and records an engine revision. Schema 1
loads and resaves as Legacy; older sessions keep their original sound. The
footer identifies the engine. New instances use Refined. Research, decisions,
rejected sparse-input experiment, limits and reproduction commands are in
[reverb-quality.md](reverb-quality.md).

**Tested:** all 16 release CTest suites passed; the four reverb suites passed
again after the final DSP changes, in release and ASan/UBSan builds. Regression
coverage includes actual legacy-state recall/audio, invalid revision rejection,
tempo retention across engine changes, fast overlapping geometry/filter-shape
automation, allocation guards and sample-offset partition invariance. Four
legacy impulse/burst/tone renders null exactly against the 0.1.1 executable.
The original measurement script and new audio qualification gates pass.

At 48 kHz, neutral 2.5 s mixing time improves from 360 to 110 ms; the neutral
8 s tail's 8 kHz octave decay improves from 5.151 to 8.006 s. All nine neutral
44.1/48/96 kHz rate/space cases meet the 8% tolerance at 1 and 8 kHz. All twelve
neutral/style cases reach measured mixing within 300 ms. Freeze upper-band
energy stays within 0.01 dB over the tested windows. Tone-transition sideband
energy is 38–43 dB lower. Some spectral-ripple measurements are slightly worse;
the report preserves those results rather than equating density with quality.

Standalone 64-sample CPU probes on Ryzen 5 5600GT: one default instance at
48 kHz uses 1.44% of one core versus 2.75% baseline; all bands with automation
use 3.13% versus 2.97%. Four automated instances at 96 kHz use 26.14% total,
with no observed deadline exceedances in the final 30-second-audio run.
This excludes host/UI scheduling and engine-revision buffer clearing. It is
not a worst-case real-time guarantee; see the report for an earlier stress
run's single exceedance and detailed callback timings.

CLAP validator: 36 passed, 0 failures/warnings, 8 optional checks skipped;
all five retained fuzz cases and an additional 60-second fuzz run passed.
UI/native host tests pass; reverb normal/compact/2x/menu/all-band and Legacy
footer views were inspected, plus EQ normal/compact/2x/menu/24-band views.
Native tests used DISPLAY=:0 because Xvfb is unavailable. Generated evidence,
plots and level-matched synthetic WAVs are under reports/reverb-quality/.
Installed only OpenFilterReverb.clap to ~/.clap and verified its SHA-256 matches
the validated release artifact. No existing DAW project was modified.

**Pending:** human listening on real recordings and controlled Pro-R comparison,
wider modal coloration/mono/voicing assessment, long large-project profiling,
and actual Bitwig automation/recall/export validation. No FabFilter sonic parity
or professional production-readiness claim is made. New-instance Refined audio
is ready for the user's listening evaluation; old sessions remain Legacy.

## Reverb glow refinement — 0.1.1

**Implemented:** reduced the tail blur's logical spread by 10% and gold-layer
opacity by 10%, retaining the diffuse trails with a lighter appearance.

**Tested:** release EQ/reverb UI and native GUI host suites pass (four suites).
Normal, compact, 2x and menu renders inspected for both editors, including the
EQ 24-band view. Dense reverb paint averages 20.4 ms/frame over 60 frames.
Evidence: reports/reverb/glow-trim-*.txt. DSP/CLAP contracts are unchanged, so
measurements and external validation were not repeated for this visual tweak.

**Pending:** assessment during actual Bitwig playback. Installed the reverb
artifact only; the existing listening/recall gates remain pending.

## Reverb diffuse tail display — 0.1.1 follow-up

**Implemented:** replaced thin historical spectrum lines with broad translucent
contours and a soft gold glow. A reusable alpha mask at one third logical
resolution receives three separable binomial blur passes on the UI thread.
The blur keeps the same logical size at 2x host scaling. The live output, EQ
curves, nodes and all controls stay sharp. Quiet spectra extend below the plot
instead of creating a false glowing baseline. Audio processing, capture timing,
parameters and state are unchanged; no dependencies were added.

**Tested:** release EQ/reverb UI and native GUI host suites all pass. Reverb UI
and GUI host suites pass with ASan/UBSan and leak detection. Sanitized preview
renders also pass memory/undefined-behavior checks (leak detection disabled only
for the standalone preview's process-wide graphics caches). Reverb normal,
compact, 2x, menu and all-band views were inspected, plus EQ normal, compact,
2x, menu and 24-band regressions. Native tests used DISPLAY=:0; Xvfb is absent.
Dense wet-history painting averages 20.1 ms over 60 frames at 1120x720, excluding
FFT and X11 transport; not a worst-case timing guarantee. Logs are under
reports/reverb/glow-*.txt. DSP measurements and external CLAP validation were not
repeated for this painter-only change; their prior results are recorded below.

**Pending:** visual assessment during actual Bitwig playback. Synthetic host
checks do not establish this. The existing listening/recall gates still apply.

## Reverb editor clarity and tail display 0.1.1

**Implemented:** separated dial footprints, captions and readouts with a dedicated
Space caption below its outer ring and one central time readout. Auxiliary
controls occupy their own row; predelay Offset appears only with sync enabled.
The flatter control deck gives the frequency canvas more height while retaining
the suite's graphite materials and original artwork.

The analyzer now captures wet audio after Post EQ/width/ducking/gating, before
Mix and Output trim. Gold contours retain 2.2 seconds of measured wet spectra,
with a white live final-output trace and subtle fills. A fixed 56-slice buffer
runs on the UI thread; painting selects at most 12 historical contours. History
clears on hide, state load and rate changes, and ages without counting stopped
host intervals twice. This is spectral tail history, not a display of individual
room reflections. Parameter IDs, state schema and DSP equations are unchanged.

**Tested:** all 16 release suites passed, including `ui_tests` and
`gui_host_tests`; the four reverb suites passed again after the final history
clock correction. All four reverb ASan/UBSan suites passed. New regressions check
caption/dial clearance at three widths and four scales, calibrated anti-phase
stereo analysis, bounded history, idle timing and actual mono/stereo wet-tap
agreement independent of Mix. Native GUI tests retain gesture-backpressure and
reopen coverage using the working X11 display (Xvfb is unavailable).
Independent audio measurements pass. Four before/after impulse, burst and tone
cases across 44.1/48/96 kHz, styles and EQ are bit-identical.

Reverb normal, compact, 2x, menu, selected-band and empty layouts were visually
inspected, together with the suite EQ normal/compact/2x/menu/24-band regression
renders. The dense-history painter measured 22.7 ms/frame over 60 frames at
1120x720; this excludes FFT/X11 work and is not a worst-case guarantee. Evidence:
reports/reverb/ui-refresh-*.txt and reports/reverb-ui.
CLAP validator: 36 passed, 0 failed, 0 warnings and 8 optional checks skipped;
all five retained fuzz seeds passed. Installed only OpenFilterReverb.clap to
~/.clap and verified its SHA-256 matches the release artifact.

**Pending:** interactive Bitwig listening/automation/recall and worst-callback
profiling remain the manual gates described below.

## Reverb first working alpha 0.1.0

**Implemented:** independent `OpenFilterReverb.clap`, plugin ID
`org.openfilter.reverb`, schema 1 with 80 stable parameters. Original eight-line
Hadamard feedback-delay network, four input allpass stages per channel, modulated
reads, six feedback-loss EQ bands and six stereo wet post-EQ bands. Space, Decay
Rate, Modern/Vintage/Plate voicings, Predelay with host sync/offset, Character,
Thickness, Distance, Brightness, Width, Ducking, Auto Gate/hold, Freeze, Mix/lock,
input/output and bypass are functional. Fixed storage supports 1–768 kHz, with
zero dry latency and no allocation during guarded processing/flush/reset.
Feedback boost overlap is bounded; see [audio contract](reverb-plan.md).

Native X11 editor follows the Pro-R control hierarchy with the suite's graphite
materials and approved R artwork. Large central Space dial, dual frequency canvas,
editable nodes and band inspector, real pre/output FFT, output meters, starting
presets, exact entry, default resets, undo/redo and A/B. Numeric entry preserves
unmodified host precision. Fields with no effect on the selected filter are
disabled; cut nodes drag frequency only. Host event backpressure retains balanced
gestures, including close during a drag. No existing plugin sound/state changes.

**Tested:** all 16 release CTest suites passed; all 16 ASan/UBSan suites passed,
followed by the four reverb suites again after final DSP/UI fixes. Tests cover
actual dry/wet audio, rate boundaries, stereo, predelay/sync arrival, freeze
sustain/input rejection, extreme decay overlaps, reset, mono/stereo float/double,
sample-offset transport/parameter events, bit-identical event partitions
1/17/64/257/1024/4096, state round-trip/corruption/queue limits, modulation/base
separation, native embedding, five reopen cycles and rejected gesture retries.
Xvfb is unavailable; native tests used the working X11 display with test-owned
windows. No existing DAW project was opened or modified.

Independent rendered-audio measurements pass: a 1.5-second neutral midband tail
measured 1.489–1.493 seconds at 44.1/48/96 kHz. A 1 kHz decay band measured
0.393 seconds at 25% and 2.876 seconds at 200%, against 1.503 seconds neutral.
Post lowpass complex response agrees with an independent SciPy Butterworth
reference within 1.53e-10. Output trim, mono width, sample-exact predelay shift,
ducking and gate attenuation pass. See reports/reverb/measurements.json.

Rendered/inspected reverb normal, compact, 2x, menu, entry, selected, Help and
all-band layouts, and EQ normal/compact/2x/menu/24-band regressions. Reports are
under reports/reverb-ui and reports/ui. Final CLAP validator: 36 passed, 0 failures, 0 warnings, 8 unsupported optional
extensions skipped; all five retained fuzz seeds passed. Results are recorded
under reports/reverb/clap-validator.txt. Installed only OpenFilterReverb.clap to
~/.clap and verified its SHA-256 matches the release artifact. The initial validator rejected the
192 kHz activation ceiling; fixed storage and boundary coverage now match the
suite's 768 kHz limit. Five retained fuzz seeds are included in validation.

**Pending:** subjective listening/voicing and automation/recall/export in a new
Bitwig scratch project; worst-callback profiling and longer fuzz/listening runs.
No claim of FabFilter sonic equivalence or full control parity. Decay shaping is
an approximate loss model with shared boost limits; freeze is not perfectly
lossless. Filter-type transitions and pitch changes during Space/predelay
movement need listening qualification. IR import, surround, decay waterfall,
decay-notch shaping, steep post cuts, automatic EQ gain compensation, gate tempo
sync, full preset browser and accessibility remain future work. The host tail is
conservatively infinite. See the [reverb guide](reverb.md).


## Limiter oversampling and qualification 0.3.0

**Implemented:** Clean/Punch/Dense now process audio at 4× through 192 kHz host
rate, 2× through 384 kHz and native rate above that. Original sparse, symmetric
half-band FIRs interpolate/decimate actual audio. A narrow Nyquist safety filter
addresses the known gated-Nyquist reconstruction failure. Post-decimation sample
and reconstructed-peak guards catch newly generated peaks; no waveform clipper
conceals them. Legacy/True Peak off retains the old gain equations and delayed
wire. The footer reports the processing mode at normal width.

Replaced eight long direct reconstruction convolutions with efficient multistage
8× detectors. Bounded minimum-queue tail replacement by a short linear search plus
binary search; removed repeated coefficient/held-level calculations and unused
averaging conversions. All histories remain warm and instance-owned. Fixed
storage is 6,745,088 bytes. There is no audio-thread allocation, locking, logging,
file access or GUI work.

Parameter IDs/defaults/enums and state schema 2 remain stable; schema 1 still
migrates to Legacy/True Peak off. **Modern sound intentionally changes in 0.3**,
including on loading existing schema 2 modern-style states. Latency is now
914 samples / 19.04 ms at 48 kHz in every mode. CLAP tail reporting includes FIR
extent (1178 samples at 48 kHz). Output-time ceiling events retain their offsets;
internal gain automation travels through the new pipeline. Host compensation and
earlier timed automation need rechecking. Full definitions: [DSP contract](limiter-dsp.md).

**Tested:** all 12 release CTest suites and all 12 ASan/UBSan suites pass after the
final DSP optimization, including native UI/host tests on X11. Tests cover actual
audio, supported-rate boundaries, overloads, stereo links, lookahead/attack,
constant latency, FIR symmetry/DC/linearity/tail, startup Unity compensation,
minimum-queue replacement/ring/counter wrap, allocation guards, state migration,
exact event offsets and block partitions 1/17/64/257/1024/4096. Rendered/inspected
limiter normal/compact/2×/menu and EQ normal/compact/2×/menu/24-band views. Formatting,
Python syntax and whitespace checks pass. Xvfb is unavailable; the working X11
display was used. Remote CI has not been run.

Independent NumPy/SciPy full-rate FIR/convolution and batch-envelope equations
match within 2.4e-14 over 12 cases, including 44.1–192 kHz, extreme gain, partial
linking, Legacy, bypass, Unity and protection off. A saved 0.2 renderer nulls
against aligned Legacy/True Peak off within 3.5e-18 on the comparison fixture.
The 20 Hz / +6 dB-drive sine residual is −78.82 dB; the measured stationary-sine
cases all pass the −60 dB regression gate. Numerical cancellation floors in other
sine cases are not subjective transparency claims.

The original 114 independent 32× peak stress cases pass (highest −1.234 dBFS for a
−1 dBFS ceiling). An additional 96 cases span all four styles and 44.1/48/96/192 kHz,
checked with four reconstruction kernels from 8193 to 65537 taps and libebur128
1.2.6; all pass (highest across those readings −1.195 dBFS). The previous gated
Nyquist failure passes all longer kernels. Sixty-four analytical-sine meter cases
have errors between −0.058 and 0 dB and pass independent 64× reconstruction checks.
The external library's short Hann detector under-reads some high-frequency
fixtures (−0.436 dB at 0.4 Fs/44.1 kHz; up to 1.249 dB relative to ours across the
matrix). We retained analytical/long-filter accuracy gates, documented the
external discrepancy, and did not tune our detector to the under-read.

Actual rendered impulse responses show maximum 20 Hz–20 kHz deviation below
0.000300 dB across 44.1–768 kHz and Nyquist rejection better than 124 dB. In the
zero-lookahead rising-carrier test, 4× processing reduces level-matched error
against a 16× reference by 1.72–28.07 dB over seven frequencies from 1–19 kHz;
all 4× residuals are below −60 dB. This measures total in-band convergence,
including envelope sampling differences, not exclusively alias energy and not
elimination of nonlinear distortion.

**CPU investigation:** the final short probe used 6.85% of real time, versus
0.2's 11.8–12.3% on the same machine (about 42% lower). Longer 64-frame tests still show occasional deadline
misses on this machine. CPU affinity and FTZ experiments do not remove them.
Hardware counters on slow callbacks show normal instruction counts (roughly
1.1–1.2 million) but about 9–11 million cycles, versus 0.43–0.50 million cycles on
ordinary callbacks. Instrumented stages slow together; no faults/context switches
were observed in the sampled slow callbacks. This points toward execution stalls
or resource contention, not additional algorithmic work, but does not establish
a complete root cause. **64-sample host reliability is not qualified.**

Final serial benchmark matrix (Ryzen 5 5600GT, Linux 7.2.5, ordinary desktop
scheduling; simulated audio duration, processed faster than real time):

| Rate / buffer | Instances / audio duration | Thread CPU average | Worst callback wall time | Budget | Processing overruns |
| --- | --- | --- | --- | --- | --- |
| 48 kHz / 256 | 1 / 180 s | 7.37% | 0.934 ms | 5.333 ms | 0 |
| 48 kHz / 256 | 4 / 120 s | 29.89% total | 3.196 ms | 5.333 ms | 0 |
| 96 kHz / 512 | 1 / 60 s | 14.67% | 1.506 ms | 5.333 ms | 0 |
| 192 kHz / 1024 | 1 / 60 s | 29.61% | 2.961 ms | 5.333 ms | 0 |

A separate 30-second wall-clock-paced 48 kHz / 64-frame run had zero processing
overruns across 22,500 callbacks (maximum CPU 0.292 ms, wall 0.952 ms, budget
1.333 ms), but six deadlines were missed because of late scheduler wakeups.
That is evidence for DSP headroom, not a substitute for Bitwig's real-time thread
and long-session testing. Start the user acceptance pass at 48 kHz / 256 samples,
then evaluate smaller buffers in the actual host.

Final CLAP validator: 36 passed, zero failures/warnings, eight unsupported optional
checks skipped; all five retained fuzz seeds and a fresh 60-second two-worker fuzz
run pass. Installed only `~/.clap/OpenFilterLimiter.clap`; build/installed SHA-256:
`40b481db6774e0feebfe83f872b64cca125e2dcaf8f5ef97574e332d30c5faf2`.
The prior 0.2 binary is preserved outside the scan directory at
`reports/limiter-0.3/OpenFilterLimiter-0.2.0.clap` for exact old-version recall.

Reports: `reports/limiter-0.3` contains measurement/quality JSON, CPU diagnostics,
validation logs and a refreshed RMS-matched synthetic audition. No human listening
pass is claimed. libebur128 is a test-only system library; the CLAP adds no runtime
dependency. CI now includes the extra numerical quality gates.

**User acceptance remaining:** Bitwig playback, automation/recall/export and
real-material listening, as explicitly assigned to the user. See the
[focused test handoff](limiter-testing.md). LUFS and dither remain optional future
features. This is a candidate for that acceptance pass, not a claim of completed
production approval, universal codec safety or formal meter certification.

## Limiter advanced controls and DSP 0.2.0

**Implemented:** the shallow advanced panel now contains Style, Lookahead,
Attack, Release and separate transient/release channel linking, following the
supplied reference's grouping with original suite controls and materials. Clean,
Punch and Dense are original dual-stage envelope voicings. Lookahead continuously
blends safe transient envelopes; Attack shapes sustained attenuation. New
reconstructed-peak protection and final-output peak metering use eight-phase,
256-tap detection. The graph legend and bottom meter captions remain removed.
Legacy disables its unavailable controls visibly. Numeric entry, double-click
reset, keyboard focus, undo, A/B and balanced host gestures remain supported.

DSP uses fixed instance storage without audio-thread allocations. Parameter IDs
0–6 retain their ranges/defaults; IDs 7–11 append the new controls. State schema 2
loads schema 1 as Legacy with True Peak off. Original Legacy gain equations remain
available. Constant latency increased from 5 ms to 640 samples / 13.33 ms at 48 kHz,
including bypass and Legacy: hosts must refresh compensation, and old timed
internal-gain automation needs rechecking. See [DSP design/research](limiter-dsp.md)
and the [user guide](limiter.md) for precise behavior and compatibility.

**Tested:** all 12 release CTest suites and all 12 ASan/UBSan suites passed with
`UBSAN_OPTIONS=halt_on_error=1`. After the final paint adjustment, all six UI/native
host suites passed again in both configurations. Native checks used the available
X11 display; `xvfb-run` is unavailable and was not needed. Tests cover actual audio,
constant latency, independent stereo links, attack/lookahead response, silent/quiet
transparency, true-peak metering, old-state migration, corrupt state rejection,
allocation guards, sample-offset events and bit-identical block partitions
1/17/64/257/1024/4096. Formatter and whitespace checks pass.

The independent NumPy/SciPy batch reference matched within 1.49e-14 across the
measurement cases, including 44.1–192 kHz. All 114 independent 32×/8193-tap
reconstruction stress cases passed the -1 dBFS ceiling (highest measured peak
-1.218 dBFS). Turning protection off on the inter-sample test produced +2.099 dBFS.
Extending the new transient hold to 26 ms reduced the +6 dB-drive 20 Hz sine
residual from approximately -36 dB in the prototype to -77.38 dB. These are
specified numerical tests, not universal reconstruction or listening guarantees.

CLAP validator: 36 passed, zero failures/warnings, eight unsupported optional
checks skipped. All five retained fuzz seeds passed, followed by a successful
60-second two-worker fuzz run. Inspected limiter normal/compact/2x/preset-menu/
style-menu/Legacy/numeric-entry/collapsed/Help/toggle-entry renders, plus shared EQ
normal/compact/2x/menu/24-band regression views. Reports and the UI review are in
`reports/limiter-0.2`; current previews are in `reports/limiter-ui`.

Three final 48 kHz stereo probes used 11.8–12.3% of real time on average. With
64-frame blocks (1333 µs budget), medians were 147–154 µs and p99 262–283 µs;
maximum wall callbacks reached 2034 µs and maximum process-CPU readings reached
1529 µs. Scheduling contributed to some spikes but does not explain all of them.
This does **not** establish reliable operation at a 64-sample buffer. Instance
storage is 5,923,552 bytes, including warm Legacy and detector histories.

Rendered an original synthetic bass/drums/chords audition, latency-aligned and
RMS-matched at -24 dBFS, with dry/Clean/Punch/Dense sections. It is not LUFS-matched
and no subjective listening pass is claimed. Files and settings are in
`reports/limiter-0.2/audition`.

Installed only `~/.clap/OpenFilterLimiter.clap`; build/installed SHA-256 match:
`feec7d05cbec6c02a17d094a86cf70fe26e1ffa9dadf5b1fe226072390df2021`.

**Pending / qualification boundaries:** real-mix level-matched listening and
Bitwig automation/recall/duplication/offline export; long-session worst-callback
profiling and optimization; broader aliasing and external true-peak meter checks.
Eight-phase detection is not audio-path oversampling. Full audio oversampling,
LUFS and dither remain unimplemented. Pathological sharply gated exact-Nyquist
signals can exceed the ceiling under still longer reconstruction kernels; the
finite detector is not a universal DAC/codec guarantee or formal meter
certification. This is a qualification candidate, not production approval or a
claim of FabFilter sonic equivalence. Remote CI has not been run.

## Limiter meter-caption cleanup

**Implemented:** removed the persistent dBFS/dB captions below the limiter
meters. The conditional CLIP indicator remains. Audio and interactions are unchanged.

**Tested:** all six release UI/native-host suites passed on X11; inspected fresh
limiter normal/compact/2x/menu and EQ normal/compact/2x/menu/24-band renders.
Whitespace check passed. DSP measurements and CLAP validation were not rerun for
this paint-only edit. Installed only the limiter; build/installed SHA-256 match:
`ea080cfe70bce61f3276174e42646e258801825234fa231726f8fec8dc11b9ca`.

**Pending:** Bitwig-specific visual confirmation.

## Limiter legend cleanup

**Implemented:** removed the small INPUT / OUTPUT / REDUCTION legend above the
history at the user's request. This is a paint-only follow-up to 0.1.1.

**Tested:** all six release UI/native-host suites passed on the available X11
display. Rendered and inspected limiter normal/compact/2x/menu and EQ
normal/compact/2x/menu/24-band views. Whitespace check passed. DSP measurements
and CLAP validation were not rerun because audio and host integration are unchanged.
Installed only the limiter; build and installed SHA-256 match:
`7004d3e6e3de98e5196cc3ab8e76143120fa3be319990771db4d69d43b7fbb51`.

**Pending:** Bitwig-specific visual confirmation.

## Limiter reference-layout revision 0.1.1

**Implemented:** removed oversized meter figures; small peak/reduction readouts
now sit directly over tall meters. The history fills the workspace behind an
integrated gain fader and shallow lower-left control strip, following the supplied
Pro-L 2 screenshot with the suite's original graphite materials. The strip contains
release mode, fixed lookahead, Release and Channel Link with a vertical Advanced
tab. The gain readout is a draggable handle; a stationary click opens exact entry,
and double-click still resets. Attenuation callouts show measured history maxima.
Graph/output scales align at -36..0 dBFS; reduction uses 0..36 dB. No audio,
parameter ID/default, latency or state-schema change.

**Tested:** all 12 release CTest suites passed. All six UI/native GUI suites
passed under ASan/UBSan with `UBSAN_OPTIONS=halt_on_error=1`, including gain-handle
entry/drag/reset and balanced host gestures. Available X11 `DISPLAY=:0` was used;
`xvfb-run` is not installed and was not needed. CLAP validator: 36 passed, zero
failures/warnings, eight unsupported optional checks skipped; all five retained
fuzz seeds passed. Rendered and inspected limiter normal/compact/2x/menu/entry,
collapsed, Help and toggle-entry states, and EQ normal/compact/2x/menu/24-band.
Formatting and whitespace checks passed. No measurement rerun was needed because
DSP is unchanged. Reports: `reports/limiter-layout`, `reports/limiter-ui`.

Installed only `~/.clap/OpenFilterLimiter.clap`; build/installed SHA-256 match:
`777ff9cf26464933749a96a302aaee6a50f2680c7e6ee880e888c951dfe7858b`.

**Pending:** Bitwig-specific visual/interaction confirmation and the audio
qualification gates recorded below. This remains a sample-peak alpha.

## Limiter sample-peak alpha 0.1.0

**Implemented:** independent `plugins/limiter` engine, native editor and
`OpenFilterLimiter.clap`, plugin ID `org.openfilter.limiter`, schema 1 with
`OFLMSTAT` magic and seven permanent parameters. Predictive minimum-window gain
control, smoothed attack, fixed 5 ms latency/lookahead, 10 ms peak hold,
release/auto release, stereo linking, gain/ceiling, unity comparison and aligned
bypass. Audio is independent of CLAP/UI and uses fixed storage. Event offsets,
base/modulation separation, bounded state handoff and gesture backpressure follow
the existing suite contract. The original L wordmark is now embedded alongside
EQ/C; no dependency changes and no EQ/compressor DSP changes.

The editor follows the official Pro-L 2 workflow reference: left gain fader,
large six-second input/output/reduction display, right-hand output and reduction
meters, collapsible timing panel, footer ceiling/unity/bypass, A/B, undo/redo,
starting points, exact entry and descriptor-default double-click resets. The
native graphite materials, controls and graphics are original OpenFilter work.
Read limiter.md and limiter-plan.md for semantics and limitations.

**Tested:** all 12 release CTest suites passed, and all 12 ASan/UBSan suites
passed with `UBSAN_OPTIONS=halt_on_error=1` and no sanitizer diagnostics. Includes
existing `ui_tests`/`gui_host_tests`, compressor equivalents and four new limiter
suites. Native tests used the available X11 `DISPLAY=:0`; `xvfb-run` is not
installed locally and was unnecessary. Synthetic host tests cover mono/stereo,
float/double in-place audio, exact event offsets, bit-identical automation across
1/17/64/257/1024/4096-sample partitions, state, modulation, allocation guards,
five editor reopen cycles and begin/value/end events under host backpressure.
A pre-existing zero-length `memcpy` with a null source in the compressor test
stream was guarded to make the strict sanitizer run clean; this changes no
plugin behavior. CI now measures/validates the limiter and fails on UBSan errors.
The updated GitHub Actions workflow has not been run remotely.

Independent NumPy/SciPy minimum-filter/convolution reference residual stayed
below 3e-14. Sample ceiling passed impulses, random overload and extreme-gain
stress; rates include 44.1/48/88.2/96/176.4/192 kHz in the independent measurements
and 1–768 kHz boundary/rate tests in C++. At +6 dB drive, steady sine residuals
were -73.9 dB (30 Hz), -121.7 dB (55 Hz), -113.6 dB (997 Hz); these narrow tests
are not perceptual transparency claims. A deliberate inter-sample test exceeded
the -1 dBFS ceiling by **3.01 dB** after 16x reconstruction: true-peak protection
is not implemented. Aggregate 48 kHz stereo DSP benchmark: 0.048 s for 10 s of
audio (~0.48% of real time), not a worst-callback scheduling guarantee.

Final CLAP artifact: 36 validator checks passed, zero failures/warnings, eight
unsupported optional checks skipped; all five retained fuzz seeds passed.
Rendered and inspected limiter normal/compact/2x/menu/entry/toggle-entry/Help and
collapsed views, plus EQ normal/compact/2x/menu/24-band and compressor
normal/compact/2x/menu. EQ dense painting measured 11.73 ms/frame. Reports and
previews are under `reports/limiter`, `reports/limiter-ui`, `reports/ui` and
`reports/compressor-ui` (generated files are not committed).

Installed only `~/.clap/OpenFilterLimiter.clap`. Build and installed SHA-256 match:
`a8a3b88b31646873edf33868e241916df49440e291e092901f2aa76834ac27ce`.

**Pending:** true-peak reconstruction/limiting, oversampling and aliasing
qualification, BS.1770 loudness, dither, additional timing/styles, level-matched
listening on real program material and Bitwig-specific automation, recall,
resizing and export checks. This is a working sample-peak alpha, **not a
production-mastering qualification**. No existing DAW projects were opened or
changed. Synthetic-host success does not complete Bitwig validation.


## Product wordmark integration

**Implemented:** exported the approved Figma lettering into nine transparent,
outlined SVGs under `assets/branding`: the suite master plus EQ, compressor,
limiter, reverb, multiband compressor, de-esser, gate and saturator. The existing
EQ and compressor headers now paint the matching EQ/C logos as embedded Cairo
vectors, with the corrected e, centered i dot and optical spacing. SVG sources,
font attribution and the reproducible native-path generator are included.
No runtime asset files or additional graphics dependencies are required.
Audio, parameter IDs, interaction bounds and state schemas are unchanged.

**Tested:** release build succeeded; `ui`, `gui_host`, `compressor_ui` and
`compressor_gui_host` all passed on the available X11 display. Rendered and
inspected normal, compact, 2x and menu views for both plugins, plus the EQ
24-band view and compressor compact sidechain view. Previews are under
`reports/branding`. Logos remain clear of toolbar controls at minimum width.
No DSP measurement or CLAP validator rerun was needed for this paint-only change.

**Pending:** Bitwig-specific visual confirmation. Updated binaries are built
under `build/release/plugins`; this milestone does not replace installed plugins
or claim Bitwig validation.

## Compressor suite styling 0.1.2

**Implemented:** aligned compressor styling with the existing EQ while retaining
its revised layout. Both editors now use the same meter painter: narrow recessed
lanes, six-pixel segmentation and green-to-gold level fill. Compressor reduction
uses a warm downward lane and its own positive dB scale. Matched the suite
wordmark, header/footer gradients, undo/redo icons, toolbar heights, captions,
inset bold readouts, signed trims, menu selection marks and hover/entry states.
The compact Lookahead label has explicit clearance from its numeric field.
DSP, audio parameters and state schema are unchanged.

**Tested:** all eight release and all eight ASan/UBSan suites passed; all four
EQ/compressor UI/native-host suites passed again after the final spacing fix.
Normal, compact, 2x, menu and entry compressor previews inspected, including
sidechain views and Help. EQ normal, compact, 2x, menu and 24-band previews are
pixel-identical to their pre-extraction renders (SHA-256 comparison) and were
visually inspected. Dense EQ software painting measured 11.86 ms/frame mean on
the local Ryzen 5 5600GT. Installed CLAP: 36 validator checks passed, no failures
or warnings, eight unsupported optional checks skipped; retained seeds passed.
No DSP measurement rerun was needed for this styling revision. Reports are in
`reports/compressor-style-*` and `reports/eq-style-preview.txt`.

Installed only `~/.clap/OpenFilterCompressor.clap`; build/installed SHA-256 match:
`178393d67645e0baff680712a6b5ad8fb39084c73d40e1f99a42490e1bbf941d`.

**Pending:** Bitwig-specific interaction/listening and the release qualification
items below. Synthetic host tests do not complete those gates.

## Compressor layout revision 0.1.1

**Implemented:** reworked the arrangement after direct inspection of FabFilter's
[official Pro-C overview](https://www.fabfilter.com/help/pro-c/using/overview).
The workspace is now a continuous level history with an optional knee overlay,
four dominant dials (Threshold/Ratio/Attack/Release), smaller stacked Makeup/Mix,
adjacent Knee/Range/Lookahead/Hold sliders, narrow meters and a collapsible
sidechain drawer. Input/Output/Bypass occupy the footer. Original EQ graphite
materials are retained. The history and GR meter now both span 60 dB, with coral
attenuation marking. Audio parameters, DSP and state schema are unchanged.

View toggles emit no audio events; hidden sidechain controls are skipped by
hit testing and keyboard focus. Slider tracks drag horizontally and numeric
readouts retain exact entry. Double-clicking the exposed knee restores the
threshold default with balanced host gestures. External source selection stays
visible in the collapsed drawer button.

**Tested:** all eight release suites passed. All eight sanitizer suites passed
for the layout, with all four EQ/compressor UI/native-host suites rerun after
the final knee-reset fix. CLAP validation: 36 passed, zero failures/warnings,
eight unsupported optional checks skipped; retained seeds passed. Previews were
inspected at normal, compact, 2x, menu and sidechain states; the EQ's normal,
compact, 2x, menu and 24-band views were also rendered and inspected. No DSP
measurement rerun was needed for this UI-only revision; earlier audio evidence
continues to apply. Reports: reports/compressor-layout-*.txt.

Installed only `~/.clap/OpenFilterCompressor.clap`; build/installed SHA-256 match:
`85387284652251018a383ebdfd4ea0c6de671638b067eb8bbd0eb6363119ce40`.

**Pending:** Bitwig-specific interaction/listening validation and the release
qualification items below. Synthetic host tests do not complete those gates.

## Delivered milestone: Compressor alpha 0.1.0

Built and installed separately as `~/.clap/OpenFilterCompressor.clap` (plugin ID
`org.openfilter.compressor`). EQ audio, IDs, state and installed artifact are
unchanged. Build/installed SHA-256:
`65247ad813abd75d8e1503c09ce3b8b4eca43654b449e9253242e24660193c45`.
See [compressor guide](compressor.md) and [audio contract/plan](compressor-plan.md).

**Implemented:** independent double-precision feed-forward DSP, mono/stereo and
float/double buffers, Peak/RMS, quadratic knee, range, attack/release/hold,
program-dependent auto release, stereo link, input/makeup/output, parallel mix,
smoothed bypass and automation, global CLAP modulation, detector HP and external
sidechain. Lookahead is 0–10 ms with constant ceil(rate × .01) latency, including
bypass/dry. The engine uses fixed arrays; no process-time storage resizing.
Versioned/checksummed schema 1 stores base parameters, separate from modulation.

The native editor reuses the EQ's original graphite theme, vector knobs, wells,
click tracking and X11 raster support. It provides transfer editing, six seconds
of level/GR history, meters, exact entry, all-parameter default resets, fine drag,
keyboard navigation, five starting points, A/B and 64-edit undo/redo. The bridge
retains begin/value/end events under host backpressure, including editor close.
Existing primitives are reused; no new library dependencies were introduced.

**Tested locally on Linux x86-64 / AMD Ryzen 5 5600GT:**

| Check | Result |
| --- | --- |
| Release CTest | All 8 suites passed (EQ + compressor DSP, CLAP, UI, native GUI) |
| ASan + UBSan CTest | All 8 suites passed with leak detection enabled |
| Independent compressor audio reference | 20 cases, six rates 44.1–192 kHz, Peak/RMS, fractional lookahead, timing, sidechain HP, auto release, hold, mix, range; max sample residual 1.06e-15 |
| Static knee/ratio audio | 60 rendered cases; max dB error 1.43e-14 |
| Timing and safety | Exact attack time constant, hold, delayed bypass/mix impulse alignment through 768 kHz, reset, finite-input stress, C++ allocation guards |
| Host contract | Sample-offset events, bit-identical outputs at block sizes 1/17/64/257/1024/4096, base/modulation separation, buffered state and rejection, external SC float/double mono/stereo |
| Native GUI | Five reopen cycles, 2x scaling, host backpressure, pending save/load, balanced gestures, hide/close during drag; real X11 test windows on DISPLAY=:0 |
| Editor QA | Compressor normal/compact/2x/menu/entry/Help inspected; EQ normal/compact/2x/menu/24-band inspected |
| UI interactions | Default reset for all 18 controls, exact entry/error handling, fine-drag continuity, undo/redo, A/B/copy, graph threshold editing, preset undo, state-load history reset |
| clap-validator | Installed artifact: 36 passed, 0 failed/warnings, 8 unsupported optional checks skipped; retained regression seeds and a 60-second, two-worker fuzz run passed |
| Aggregate engine benchmark | 480k stereo samples at 48 kHz in 0.0741 s (~0.74% of real time), lookahead + auto release + SC HP; not worst-callback latency |
| EQ measurement regression | Existing independent EQ/Brickwall measurements passed |

Reports are generated under `reports/compressor-*`; screenshots are under
`reports/compressor-ui`. The standalone benchmark includes stimulus generation.
The CI workflow now includes compressor audio measurements and CLAP validation;
these changes have not yet run remotely.

**Measured limitations and pending work:** this is a usable first compressor
build, not a completed professional release qualification or a Pro-C clone.
997 Hz steady-sine THD+N is -96.2 dB at 48 kHz with default time controls,
unlinked Peak detection and about 9 dB reduction. At 55 Hz it is -51.5 dB;
0.1 ms attack / 10 ms release raises it to -28.3 dB. Short timing creates audible
bass modulation distortion. These are stated stimuli/settings, not blanket
sound-quality claims. No oversampling/alias suppression, saturation/analog
matching, automatic makeup, M/S, zero-latency mode or proprietary style matching
is implemented. Sidechain listen, user preset-file browsing and full screen-reader
accessibility are also pending. Dynamic lookahead automation changes detector
sampling and needs matched-level listening alongside timing/ratio automation.
Bitwig scan/playback, external routing, recording, duplicate/save/reopen/export,
long-session stability, multi-instance callback profiling and level-matched
vocals/drums/bass/mix listening remain explicit manual gates. Native test-host
success does not mark Bitwig validation complete.

## Delivered milestone: EQ editor alpha 0.3.3

The native CLAP EQ now has a functional Linux editor. Build output is
`build/release/plugins/OpenFilterEQ.clap`; local installation is
`~/.clap/OpenFilterEQ.clap`. The project is published from `main` to
`github.com/etchebarne/openfilter`. The first CI run exposed a GCC 13
range-loop copy warning in the drag regression test. The loop now uses a const
reference. Ubuntu sanitizer testing also exposed a Cairo dependency unload leak,
reproduced by a minimal dlopen/dlclose program without plugin code (12,384 bytes).
The CLAP test host now retains Cairo for its process lifetime and clears its
static caches, like the native GUI host. Leak detection remains enabled.
Remote results are available in
[GitHub Actions](https://github.com/etchebarne/openfilter/actions/workflows/build.yml).
Built and installed
SHA-256 match: `7fdb9d22161b24dddb00f976a33c49cef2c07d73d2aa2716e90b844717e2835c`.

Version 0.3.0 adds Brickwall low/high cuts: an independent 20th-order elliptic
filter with 0.01 dB passband ripple, a 100 dB stopband and no buffered latency.
It appends slope value 3 while preserving all prior parameter IDs, enum values,
defaults, filter equations and schema-1 layout. Old sessions retain their sound;
sessions using Brickwall require 0.3.0 or newer. Q/Gain are inactive for this mode,
with stored values retained. See [eq-contract.md](eq-contract.md) for cutoff,
phase, ringing, automation and compatibility details.

Version 0.3.3 repositions the floating panel during node drags instead of waiting
for release. Knob/text editing stays stationary. A regression reproduces the
previous delayed movement and checks both drag directions, release continuity,
single-step undo and balanced gestures at normal and compact sizes.

Version 0.3.2 removes the dark Nyquist overlay so the graph background and
grid stay continuous to the right edge. This is a painter-only change; audio,
parameter limits, response calculations and state remain unchanged.

Version 0.3.1 corrects layout while restoring the tactile header/footer style:
separate caption, knob and value rows prevent overlap; the floating controls
are symmetric around Gain; toolbar controls and text are vertically centered.
Range moves into the header and the graph becomes the continuous main surface,
with frequency labels inside the canvas. Responses outside the display range
are clipped rather than flattened into a false bottom border. The separate
meter scales, slope menu and Brickwall behavior from 0.3.0 are retained.
No audio equations, parameter IDs or state format changed in 0.3.1. Shared
material and text-centering primitives live in libs/ui. Double-click reset
remains suite-wide.

The editor provides an expansive graph with colored
response fills, a gold total curve, compact floating rotary controls, slim header
and global footer. Double-click resets all parameter controls to their descriptor
defaults. Exact entry is a readout click, secondary/Ctrl-click or Enter. Empty
graph space uses a single click to add a band. Shared theme/knobs/click handling
and docs/ui-conventions.md establish these conventions for the whole suite.
The panel remains stable while editing knobs, avoids deep-cut nodes during selection and dragging,
and can be dismissed independently of deleting a band. Response curves are cached
between parameter/rate/width changes. Existing analyzer, metering, A/B, undo,
routing and precision controls remain available. See [editor.md](editor.md) for controls and display conventions.
Cairo renders into owned CPU images; Pugl embeds the X11 child window and Xlib
presents the image. This avoids per-display Cairo rendering-resource leaks
observed during repeated native editor teardown. Font caches are released only
by the standalone test process, never by the plugin inside a host.

Existing slope 0–2 sound, parameter IDs, plugin ID, and state schema 1 are unchanged.
The engine retains 24 bands, six shapes, 12/24/48 dB/oct and Brickwall cuts, stereo/L/R/M/S
routing, mono/stereo buses, float32/float64 buffers, smoothing and global CLAP
modulation. All bands initially start disabled; the editor creates enabled bands.
Shared libraries now include reusable drawing, raster presentation and analysis
alongside DSP, parameter, concurrency and stream primitives.

## Validation evidence

0.3.3: all four release suites passed, including the new live panel-drag
regression (which failed on 0.3.2). Normal, compact, 2×, menu and 24-band renders
were inspected. See reports/panel-drag-tests.txt and reports/panel-drag-before.txt.
Prior sanitizer and validator results are dated below.

Machine: Linux x86-64, AMD Ryzen 5 5600GT. GCC 16.2.1 release build; Clang 22.1.8
for AddressSanitizer + UndefinedBehaviorSanitizer. No fast-math flags.

| Check | Result |
| --- | --- |
| CTest, release | All four suites pass: DSP, CLAP contract, UI and native GUI host |
| CTest, ASan/UBSan | All four suites pass in the Ubuntu 24.04 / Clang 18 CI reproduction with Xvfb, including native editor leak checks |
| Independent audio reference | 720 cases; max normalized peak residual 4.98e-11; max finite-window response error 8.88e-7 dB vs RBJ/SciPy reference |
| clap-validator 0.4.1, pinned revision | On 0.3.1: 36 passed, 0 failed, 0 warnings, 8 skipped (44 total); unsupported optional extensions are skipped |
| Brickwall reference | 120 C++ impulse cases against independent SciPy elliptic SOS, six rates, both cuts, Q extremes and cutoff limits; max absolute residual 1.01e-12 |
| Brickwall automation | Extreme sweeps from 1 kHz to 768 kHz sample rate, routing/Q/reset regressions; exact CLAP output agreement across block sizes 1/17/64/257/1024, including slope and shape transitions |
| Randomized external fuzzing | 60-second run on 0.3.0; see reports/brickwall-fuzz.txt |
| Retained fuzz regressions | All five seeds in tests/fuzz-seeds.txt passed on 0.3.1 |
| Allocation guard | No C++ new/delete observed in guarded process/active-flush calls, including aligned forms and analyzer-enabled processing |
| UI response correctness | Complex response compared with actual rendered impulses across all six shapes, legacy slopes plus both Brickwall cuts, and a mixed stereo/L/R/M/S cascade |
| Analyzer | Calibrated bin-centered sine, including anti-phase stereo; no cancellation from summing channels |
| Native editor | Actual X11 input and double-click reset after host automation; embedding, resizing, 2× scaling, five reopen cycles, hide/show, close during a gesture, rejected output-event retry |
| UI state handoff | Save before audio consumes an edit, restore into another instance, discard queued values superseded by state load |
| Visual review | Actual painter at 1120×720, 900×600, 2240×1440; menus, empty, disabled, exact entry, help, 24-band and Brickwall low/high-cut states; reports/ui |
| Reset conventions | Frequency/gain/Q, output, bypass, enabled, filter/routing selectors, graph nodes, undo and double-click distance/target isolation |
| Snapshot stress | 100,000 coherent publications checked with a concurrent reader |
| Bitwig discovery | Bitwig 6.0.11 logged reading CLAP metadata for the installed artifact at 20:59:58 local time |

CTest covers normal-sample identity, channel isolation, smoothing, reset, extreme
parameters, randomized automation, malformed/truncated state, partial streams,
concurrent state saving/loading, separate base/modulation values, and exact output
agreement for the same automation across block sizes 1, 17, 64, 257 and 1024.
The validator additionally exercises out-of-place processing and fractional /
extreme sample rates. An earlier parallel validation produced a denormal timing
warning (2.83×); a serial rerun passed without warnings. Timing comparisons
under concurrent build/test load are noisy; both reports are retained.

Fuzzing found and fixed host-precision subnormal output and intermittent failed
state saves under concurrent processing. Saved seeds and focused tests retain
these regressions. Generated reports are under `reports/` (not versioned).

Aggregate DSP benchmark, 10 seconds of 48 kHz stereo audio with all 24 bands:
0.094 seconds for static bells; 0.502 seconds while automating all bell gains;
0.851 seconds for static Brickwall cuts; 1.238 seconds while automating all cut
frequencies every 64 samples. These are single observed runs, not worst callback
times or DAW instance-capacity guarantees. The Brickwall benchmark intentionally
stacks alternating low/high cuts, so it is a throughput test rather than a
representative audible configuration. See reports/eq-benchmark.txt.

Editor software-paint benchmark: see reports/ui-render.txt for the latest
60-frame mean with 24 active bands at 1120×720. This excludes X11 transport and
FFT work, and is not a worst-case or multi-instance CPU measurement. The 0.3.1 render measured 24.7 ms/frame during development and 12.6 ms/frame
in the final isolated render; these observations are not frame-time guarantees. Rendering runs near 30 Hz while open; analyzer capture
is disabled while hidden.

## Remaining work and limitations

- The user reports that the 0.2.0 editor works in Bitwig. Discovery is observed. In-project playback, modulation, save/reopen,
  duplication, and listening validation still require a scratch-session test.
  Native DAW UI automation was unavailable in this session; no existing project
  was modified. Synthetic tests do not establish those user-facing outcomes.
- Bell/shelf/notch and conventional cut designs remain the bilinear-transform SVF baseline. It agrees with the
  independent reference, but does not yet meet the intended analog-matching
  target. A 15 kHz, +12 dB, Q=1 bell at 48 kHz differs from the corresponding
  analog magnitude by up to 6.48 dB over 1–21 kHz. This deliberately exposes the
  cramping problem in the current baseline.
- Compare matched designs and their modulation behavior before declaring the
  filter algorithm ready. Preserve this baseline via a versioned algorithm
  mode/migration if existing saved sessions must retain their sound.
- The editor is implemented and tested in an isolated native CLAP host. Actual
  Bitwig editor use and automation recording remain unverified. The earlier
  Bitwig scanner observation applied to 0.1.0, not an interactive editor test.
- Band audition, a preset browser, sidechain analysis and full accessibility are
  pending. A/B/history/view preferences last for the editor session only.
  Other platforms and native Wayland embedding are not implemented.
- Dynamic EQ, linear phase, spectral processing, and automatic gain matching
  are deferred.
- Infinite-tail reporting and CONTINUE processing are conservative; implement
  measured silence/tail behavior before calling resource usage production-ready.
- Windows/macOS packaging, worst callback profiling, broader host checks, and
  long listening sessions remain unverified. This is an alpha, not a release
  candidate for irreplaceable sessions.

Next milestone: use the installed editor in a Bitwig scratch project, exercise
playback/automation/recall and gather interaction feedback; then continue the
matched-filter investigation without silently changing saved-session sound.
