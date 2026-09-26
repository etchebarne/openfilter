# OpenFilter De-Esser 0.1.0

A native Linux CLAP de-esser alpha with the OpenFilter suite's graphite editor.
Build: `build/release/plugins/OpenFilterDeesser.clap`.
Install this effect only: `bash tools/install-deesser-local.sh`.
Host name: **OpenFilter De-Esser**. Permanent plugin ID: `org.openfilter.deesser`.

## Working with a vocal

1. Start with Vocal detection and Split Band processing. Play a phrase with
   both voiced words and sibilants. Starting points are suggestions, not automatic
   analysis or loudness-matched presets.
2. Enable Audition to hear the detector band. Drag its two handles or enter
   Low/High values to isolate the harshness. The strip shows live filtered
   detector frequency energy, highlighted green inside your selected range.
   Turn Audition off before mixing.
3. Lower Threshold until the offending consonants trigger. Range limits the
   maximum requested reduction; begin with modest amounts and listen for lisping.
4. Compare Wide Band for full-signal ducking versus Split Band for a dynamic
   high-shelf cut that preserves low frequencies. Try Allround for cymbals or
   material that should not use the vocal spectral-balance heuristic.
5. Adjust Lookahead, Attack and Release if the onset escapes or recovery sounds
   unnatural. Keep Stereo Link at 100% to preserve common stereo gain behavior.
   External sidechain requires a separate routed source in the host.

Threshold is filtered-detector RMS dBFS. The six-second display shows a centered
grey input waveform with green detector-band regions while reduction is active.
Signed positive/negative peaks are collected over 5 ms slices across both stereo
channels, so anti-phase audio remains visible. Input and detector waveforms are
aligned to the delayed program. A square-root display scale makes quiet syllables
visible; waveform height is not a dBFS axis. The thin horizontal line is zero
amplitude, not Threshold. Adjust Threshold using its dial or numeric field.

The detection strip has a logarithmic 1–22 kHz axis and a live 4096-point Hann
spectrum of the actual filtered detector signal (internal or external sidechain).
It shows energy even below Threshold: green means inside the selected band, not
necessarily that reduction is occurring. Muted energy outside the handles shows
filter rolloff. The trace decays when input stops, and frequencies above Nyquist
remain empty. Narrow spectral peaks are preserved when a display pixel spans
multiple FFT bins. FFT work runs only on the UI thread.

Gain reduction has its own right-hand meter, alongside stereo sample-peak meters.
These are not true-peak or loudness meters. Audition and bypass suppress the GR
readout and green highlighting because reduction is not applied to the monitored
output. The waveform is read-only; clicking it never edits an audio parameter.

Click a numeric readout or right-click a value control for exact entry (including
`5.5 kHz`). Double-click any parameter to reset its descriptor default. Drag
knobs vertically; Shift gives fine control. The waveform display is read-only.
Tab focuses controls; Enter edits, arrows adjust, Escape cancels. A/B and Copy
compare editor-session settings; Ctrl+Z / Ctrl+Shift+Z undo/redo local edits.
Opening a saved host state clears local undo and A/B history.

## Audio contract and limitations

Fixed latency is ceil(sample rate * 15 ms), including bypass and audition;
lookahead automation never changes host compensation. Split Band uses an original
minimum-phase dynamic shelf centered on the lower detection edge. The upper
edge affects detection only. It is not a linear-phase crossover. Vocal detection
uses a band-to-broadband energy heuristic, not a learned phoneme classifier.
No oversampling or mid/side-only mode is included. It is not a brickwall limiter.

Crossed detection edges are sorted internally without changing saved values;
frequencies are limited to the sample rate's usable bandwidth. Trims, modes,
frequency and time changes ramp per sample. State schema 1 stores only base
parameters; modulation and live delay/filter histories are never serialized.
The [audio plan](deesser-plan.md) records the exact equations and stable IDs.

## Validation and remaining qualification

Reproduce the automated gates:

```sh
export PATH="$PWD/.venv/bin:$PATH"
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release
python tools/measure_deesser.py
bash tools/check-clap.sh build/release/plugins/OpenFilterDeesser.clap
build/release/deesser_editor_preview
build/release/deesser_render --benchmark
```

Use `xvfb-run -a` for CTest when DISPLAY is unavailable. The de-esser adds DSP,
CLAP contract, UI and native GUI host suites. ASan/UBSan use the suite's sanitize
preset. Previews cover normal, compact, 2x, menu, exact-entry and Help views.
The measurement script tests actual C++ output against independent SciPy and
RBJ equations and writes `reports/deesser-measurements.json`.

**Pending:** diverse real-vocal and cymbal listening at matched levels, lisping
and false-trigger assessment, Bitwig scanning/playback/automation/recall/export
in a new scratch project, and worst-callback profiling. Automated agreement
establishes implementation correctness for the stated cases; it does not certify
FabFilter parity or production readiness. See [status](status.md) for completed
checks and their measured limits.

Numeric readouts support both exact entry and dragging throughout the suite:
click and release to type, or drag vertically to adjust (up increases, down
decreases). Hold Shift for finer movement. Double-click resets the descriptor
default. A drag is one undo step and one balanced host automation gesture.
