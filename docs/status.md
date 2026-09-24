# Project status — 2026-09-23

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
