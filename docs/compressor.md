# OpenFilter Compressor 0.1.2

A native Linux CLAP compressor with the EQ's graphite materials and interaction
conventions. The first build provides feed-forward compression with
Peak and RMS detection. It is an original implementation, using FabFilter Pro-C
as a workflow reference. It does not reproduce or claim equivalence to its
proprietary compression styles.

## Using it

Load **OpenFilter Compressor**. Start with Threshold, Ratio, Attack and Release.
The continuous level display fills the workspace, with four main dials floating
along its lower edge: Threshold, Ratio, Attack and Release. Makeup and Mix are
smaller stacked controls on the right. Knee/Range and Lookahead/Hold sit beneath
their respective dials. Input, Output and Bypass stay in the footer.

**Knee** shows/hides the static transfer curve over the left of the history,
using the same vertical dB scale. The curve includes effective host-modulated
threshold, ratio, knee and range, before makeup/mix/output. Drag horizontally in
its exposed upper region to set the base threshold; the gold handle marks that
base value on the effective curve. Double-click the exposed curve to reset
Threshold. **Side chain** opens a lower drawer containing
source, detector HP and stereo link. Hidden controls are skipped by keyboard
focus. View visibility is editor-session state; toggling it never changes audio.
Double-click Knee restores the overlay; double-click Side chain closes its drawer.

The six-second history shows input in gray, output in teal, and attenuation in
coral. Input/output is -60 to 0 dBFS; attenuation is 0 to 60 dB from top to bottom,
using the same pixels per dB. Histories collect peak values in 10 ms buckets.
The narrow right meters show stereo sample peaks and maximum channel reduction.
They share the EQ’s recessed segmented style: green-to-gold signal levels grow
upward, while warm gain reduction grows downward on its own positive dB scale.
Click the meters to clear the output clip latch. These are not true-peak or
loudness meters.

- Drag dials vertically or the small slider tracks horizontally; hold Shift for
  fine movement. Scroll any value to adjust. Click a slider’s right-hand value
  for exact entry.
- Double-click a value, toggle or selector to reset its descriptor default.
- Click a readout, right-click/Ctrl-click a control, or use Tab then Enter for
  exact entry. Enter commits, Escape cancels. Units are optional; accepted units
  are dB, ms, Hz, %, and :1 for ratio. Numeric entry clamps to the documented range.
- A/B keeps two editor-session settings; Copy writes the active slot into the
  other. Undo/Redo or Ctrl+Z / Ctrl+Shift+Z restore up to 64 local changes.
- Starting points offers Default, Gentle bus, Vocal control, Parallel drums and
  External ducking. These are editable initial settings, not loudness-matched
  promises for arbitrary source material. They replace all audio parameters.
- Detector selects Peak or RMS. RMS measures mean-square level with a 10 ms
  averaging time, so a sine reads approximately 3 dB below its peak level.
- Auto release lengthens release with sustained compression, up to four times
  the selected release value. Hold delays the start of release.
- Range caps the requested attenuation. Knee sets the width of the quadratic
  transition around threshold. Mix blends dry and compressed audio with equal
  latency; Makeup affects wet audio only. Output trims the combined signal.
- Internal sidechain follows the input trim. External uses the dedicated CLAP
  sidechain input. Route a source in the host before choosing External; an absent
  external source is silence and causes no compression. Sidechain HP filters
  only the detector; 0 Hz disables it. There is no sidechain-listen mode yet.
- Stereo Link blends independent channel attenuation toward the larger requested
  attenuation. At 100% the two channels share the same gain envelope from reset.
- Lookahead ranges from 0 to 10 ms. The plugin always reports a fixed 10 ms audio
  delay (rounded up to whole samples), including bypass and 0% mix. This keeps
  host delay compensation stable while parameters are automated.

The default editor is 1120×720; minimum 900×600, scale 0.75–3. Keyboard navigation
and numeric entry are supported; full screen-reader accessibility is pending.
All audio parameters are saved to the host project. Window size, comparison
slots, history and undo are editor-session state.

## Build, test and install

Use the repository's pinned toolchain and dependencies:

```sh
export PATH="$PWD/.venv/bin:$PATH"
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release
.venv/bin/python tools/measure_compressor.py
bash tools/check-clap.sh build/release/plugins/OpenFilterCompressor.clap
build/release/compressor_editor_preview
bash tools/install-compressor-local.sh
```

Without `DISPLAY`, use `xvfb-run -a ctest --preset release`. Install writes only
`~/.clap/OpenFilterCompressor.clap`, or the `CLAP_INSTALL_DIR` override. It does
not replace the EQ. Refresh Bitwig's plugin scan, or restart its plugin process
if replacing an already loaded build. No existing DAW project is modified by
these tools. Build the plugin alone with `cmake --build --preset release --target
OpenFilterCompressor`.

## Quality evidence and remaining release gates

See [status.md](status.md) for measured results and [compressor-plan.md](compressor-plan.md)
for exact detector, timing, realtime and state contracts. Automated tests do not
establish subjective parity with a reference compressor. Bitwig playback,
sidechain routing, automation recording, duplicate/save/reopen, offline export,
and matched-level listening on vocals/drums/bass/full mixes remain manual gates.

The engine has no intentional saturation or oversampling. Very fast envelope
settings create modulation distortion, especially on bass: at 55 Hz and about
9 dB of compression, measured THD+N is -51.5 dB with default time controls and
-28.3 dB with 0.1 ms attack / 10 ms release (48 kHz, peak detection, unlinked).
At 997 Hz with default time controls the same test is -96.2 dB. These are specific
stimuli/settings, not a general distortion specification. Use longer timing,
RMS detection or less reduction when low-frequency cleanliness matters.
No analog matching, brickwall peak limiting, zero-latency mode, oversampling,
mid/side mode, automatic makeup gain or Pro-C style parity is claimed.
