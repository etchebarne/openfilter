# Project status — 2026-09-23

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
