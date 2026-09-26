# EQ editor

The 0.3.3 editor is an original native Linux interface, built with Pugl and Cairo.
It targets Bitwig's embedded X11 CLAP windows (including an XWayland host session).
Cairo draws into a reusable CPU image, presented through XPutImage on the
editor’s private Pugl display. There is no browser, GPU context, or separate UI
runtime to install. The 0.3.1 visual revision uses graphite surfaces, raised
rotary caps and command buttons, recessed numeric fields, and a consistent
upper-left light source. The shared material primitives live in `libs/ui`.
The graph extends edge-to-edge between the toolbars without an inset frame.
Raised toolbar controls share a vertical center, with the Range control in the
header. The floating panel uses separate caption, dial and readout rows, with
symmetric spacing around Gain and clearance for focus rings. Input/output meters
have their own labeled dBFS scale beside the EQ gain axis.
The panel follows node drags immediately, moves above deep cuts while dragging,
and adapts its placement on resize. Knob and text edits keep the panel stationary.
Double-click still resets controls to their descriptor defaults.

## Using it

- Click empty graph space once to create a band, or use **+ Add band** / **N**.
  Drag a node for frequency/gain; scroll it for Q. Hold Shift for fine changes.
- Select a node to show its floating controls. Previous/next arrows move between
  existing bands. **X** / Escape closes the controls; the trash icon / Delete
  removes the band. Alt-click its node or use the power button to bypass it.
- Choose the filter, cut slope and Stereo / Left / Right / Mid / Side in the
  floating panel. Click the slope beneath **Low Cut** or **High Cut** and select
  **Brickwall** for an ultra-steep cut. Gain is unavailable for cuts and notches;
  Q is also unavailable for Brickwall. Stored Q/Gain values remain intact.
  Brickwall is a finite elliptic filter with nonlinear phase and edge ringing,
  not a linear-phase mode; see [audio contract](eq-contract.md).
- **Double-click any value control to restore its declared default.** Frequency
  resets to 1 kHz, gain/output to 0 dB, Q to sqrt(0.5). This also applies to
  selectors and parameter toggles. Double-clicking a node resets frequency,
  gain and Q together, preserving its shape/routing/enabled state.
- Click a numeric readout, right-click or Ctrl-click a value control, or focus it
  with Tab and press Enter for exact entry. Accepts `2.5 kHz`, `240 Hz`, `-3.5 dB`
  and Q values. Enter commits; Escape cancels. Double-click never opens entry.
- Drag knobs vertically or scroll their controls; arrows make small adjustments.
- A/B compares two editor-session settings; **A > B** / **B > A** copies settings.
  Undo/redo or Ctrl+Z / Ctrl+Shift+Z restore up to 64 local edits, including resets.
  Host automation is separate; loading state clears local history.
- Bypass, output trim and pre/post analyzer toggles sit in the bottom bar. Click
  the meter area to clear output clipping. Click **Range** in the header to cycle
  ±6 / ±12 / ±24 dB; double-click restores ±12 dB.
- Host resizing and scale factors 0.75–3 remain supported. Default size is
  1120×720; minimum is 900×600 logical pixels. **Help** displays shortcuts.

See [suite UI conventions](ui-conventions.md) for the shared interaction contract.

## Reading the display

Cut fills shade the retained frequency region beneath their response. Other
bands fill between their response and unity. Thin colored curves show individual bands; the two total curves show left
(gold) and right (pale gold). Their steady-state response uses the actual
SVF coefficient equations, including cascaded cuts and ordered routing. Responses are cached until effective parameters, sample
rate, routing configuration or graph width change. With
mixed stereo/M/S routing the output-channel curves represent RMS transfer gain
for uncorrelated equal-power stereo input. They are not a prediction for every
possible correlated input signal. Nodes show base parameter positions; curves
include host modulation, but do not visualize the short smoothing transition.

The analyzer overlays a fixed -96 to 0 dBFS scale, independent of the EQ gain
axis. Its 4096-sample Hann FFT averages left/right power, preserving anti-phase
content. This is a fixed-resolution display (11.72 Hz bins at 48 kHz), with no
spectral tilt or peak hold. Meters are stereo sample-peak envelopes, with a
0 dBFS output clip latch; they are not true-peak or loudness meters. Display
levels decay when the host stops sending audio. The graph background and grid remain continuous above Nyquist, without a
dark overlay. Response lines stop at the supported frequency boundary; the
display keeps its fixed 10 Hz–30 kHz axis.

## Ownership and lifecycle

`Editor` owns drawing, interaction, local history and FFT computation on the
main thread. It reads coherent parameter/meter snapshots and sends explicit
begin/value/end gestures. It never writes the audio engine.

The plugin bridge uses a 4096-entry SPSC queue plus a main-thread backlog.
Audio callbacks consume at most 1024 UI commands; host output-event refusal
retains the command for retry, including gesture ends. Optimistic main-thread
values keep dragging, parameter queries and immediate saves responsive. Accepted
state loads invalidate older queued values. Closing/hiding/focus loss finishes
active gestures. Editor creation registers one 33 ms CLAP host timer; destruction
unregisters it and releases the native child window.

A fixed circular audio history publishes pre/post stereo frames every 1024
samples through a triple buffer while the editor is visible. Analysis is disabled
when it is hidden. No FFT, allocation, locks, drawing or X11 call occurs in audio
processing. The fixed frame-copy cost still needs worst-callback profiling.

The headless `editor_preview` utility uses the same painter and creates demo
screenshots under `reports/ui/`. `ui_tests` verifies response-vs-audio, FFT
calibration and interactions. `gui_host_tests` embeds the actual `.clap` in its
own X11 parent and drives actual X11 events, including event backpressure and
repeated open/close. Use `xvfb-run -a ctest --preset release` without a display;
otherwise this particular test reports skipped (exit 77).

## Current boundaries

This is the first functional editor. Band solo /
audition, external-sidechain analysis, presets browser, dynamic EQ, linear phase,
copy/paste numeric entry, and full screen-reader accessibility are pending.
A/B, undo history, analyzer options, scale and window size are editor-session
state; project state remains schema 1 and stores audio parameters only. Disabled
bands with non-default values reappear on reopening; an entirely default,
disabled slot is treated as empty. Only Linux X11 embedding is implemented.
Bitwig playback, automation recording, project recall and prolonged listening
still require validation in a scratch session.

Numeric readouts support both exact entry and dragging throughout the suite:
click and release to type, or drag vertically to adjust (up increases, down
decreases). Hold Shift for finer movement. Double-click resets the descriptor
default. A drag is one undo step and one balanced host automation gesture.
