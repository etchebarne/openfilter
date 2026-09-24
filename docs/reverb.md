# OpenFilter Reverb 0.1.1

A first native Linux CLAP reverb with an original eight-line feedback-delay
network. FabFilter Pro-R 2 is the workflow and quality reference; this alpha does
not claim its proprietary algorithms, sonic equivalence, or full feature parity.

Build: `cmake --build --preset release --target OpenFilterReverb` with the local
`.venv/bin` on PATH. Artifact: `build/release/plugins/OpenFilterReverb.clap`.
Install only this plugin: `bash tools/install-reverb-local.sh`.

## Using the editor

- **Space** changes room geometry and nominal decay. The time in the dial includes
  **Decay Rate**, the percentage field above it. Click the central readout to enter the base Space time.
- **Modern / Vintage / Plate** choose original diffusion/modulation voicings.
  Character increases modulation and early reflection level. Thickness increases
  diffusion; Distance trades direct early reflections for the diffuse tail.
- **Predelay** separates the wet arrival from the dry sound. Sync offers quarter,
  eighth, sixteenth and thirty-second notes; Offset appears when sync is enabled
  and scales that time. All synced
  values are capped at 500 ms. Without a host tempo the default is 120 BPM.
- **Brightness** controls high-frequency absorption. Width controls wet mid/side
  balance, Ducking follows input level, and Auto Gate gates the wet output after
  input falls below -45 dBFS. The field alongside Auto Gate is its hold in ms.
- **Freeze** fades out tank injection and damping. It can sustain a tail, but
  interpolation still absorbs energy, especially at high frequencies.
- **Mix** blends dry/wet; use 100% on a send. Lock Mix preserves Mix while loading
  a starting preset. Input gain drives wet only; Output trims the mixture.
- Choose **Decay Rate EQ** or **Post EQ** to edit that layer. Click the canvas for
  a new band, drag for frequency/amount, scroll a node for Q. The inspector
  exposes frequency, amount, Q, enable and shape. Delete disables the selected
  band; Escape closes its inspector. There are six stable slots per layer.
- Decay bands support bell and shelves. Shelf Q is fixed, so its field is disabled.
  Post bands also support 12 dB/oct low/high cuts and notch. Post EQ affects wet
  only, with no automatic gain compensation. Curve scales have distinct units.
- Double-click restores the parameter's descriptor default. Double-click a node
  resets its frequency, amount and Q. Click a readout/right-click a control for
  exact entry; Enter commits and Escape cancels. Shift makes knob drags finer.
- Undo/redo, A/B and Copy operate on editor-session settings. Host state saves
  all audio parameters and Mix Lock, but not live audio history or UI history.

The analyzer shows the current wet signal in gold and final output in white,
with 2.2 seconds of fading gold wet-spectrum history. The gold history uses
broad, softly blurred contours; the white live output and editable EQ remain sharp. These are captured audio
spectra after Post EQ, width, ducking and gating, before Mix and Output trim;
they show the tail evolving, not individual reflections or an impulse-response
measurement. History ages in audio time and clears when the editor is hidden or
state/sample rate changes. FFT work and history painting run on the UI thread.
The analyzer uses a separate fixed -96 to -12 dBFS display range; the labeled
left/right axes belong to the editable decay/post EQ curves. The blue
curve estimates feedback loss; it is not a measured room decay. Overlapping decay
boosts share a stability budget, so stacking bands may reduce their individual
extension. The central time is a nominal reference, not a measurement of every
frequency. Output meters are sample peaks, not true peaks or loudness.

## Validation and next gates

See [status](status.md) for completed checks and [audio contract](reverb-plan.md)
for permanent IDs, ownership and DSP equations. Run the four reverb CTest suites,
`tools/measure_reverb.py`, `tools/check-clap.sh build/release/plugins/OpenFilterReverb.clap`,
and `reverb_editor_preview` after changes. The suite also needs its existing UI
and native host regressions. On a headless system use `xvfb-run`.

Remaining: matched listening and voicing in a new Bitwig scratch project;
automation recording, save/reopen, duplication, mono/stereo and offline export;
worst-callback profiling. Rapid Space/predelay changes can pitch-shift a tail,
and filter-type switches still need transition qualification. IR import, surround,
decay-notch shaping, steep post cuts, automatic EQ compensation,
gate tempo sync and a full preset browser are not implemented.

The control reference is FabFilter's official
[main controls](https://www.fabfilter.com/help/pro-r/using/maincontrols),
[Decay Rate EQ](https://www.fabfilter.com/help/pro-r/using/decayrateEQ), and
[Post EQ](https://www.fabfilter.com/help/pro-r/using/postEQ) documentation.
