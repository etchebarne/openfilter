# OpenFilter Gate — 0.1.0 alpha

A separate native Linux CLAP gate / downward expander. Build output:
`build/release/plugins/OpenFilterGate.clap`. Install only this effect with
`bash tools/install-gate-local.sh`; load **OpenFilter Gate** in a scratch Bitwig
project. Restart an existing plugin process after replacing its binary.

## Controls and workflow

Start with Threshold, the large left knob. Signal above the threshold opens
the gate; below it, Ratio determines expansion and Range limits attenuation.
Ratio 1 is unity; 100 approximates a hard gate. Attack opens the gate and
Release closes it. Hold postpones closing; Hysteresis lowers the closing
boundary after the gate opens to reduce chatter. Knee rounds the transition.
The Peak/RMS selector changes the detector; these are original detection modes,
not emulations of another product's named styles.

The center shows six seconds of filled signal history behind the white transfer
curve: dim gray is input, brighter slate is output. Their separation shows the
parts the gate attenuates. Both layers use actual latency-aligned audio peaks,
with subtle horizontal dBFS guides. Gain reduction stays on its separate GR
meter so it does not compete with the signal shapes.

History scrolls horizontally in time; the white transfer curve uses the labeled
-80 to 0 dB input axis. The curve excludes attack, release, hold and hysteresis
memory. Drag it horizontally to adjust Threshold; dashed crosshairs locate the
threshold. Transfer hides the curve and switches the bottom labels to time.
Output metering is sample peak, not reconstructed true peak; click its area to
clear the clip latch.

Expert opens the lower controls: input gain, parallel mix, wet/dry gains and
stereo balance, hysteresis, linking, sidechain source/filtering and audition.
At 100% Mix the dry path is silent; reduce Mix to blend it in. Pan is a stereo
balance: center preserves both channels and each extreme attenuates the opposite
channel. Pan is ignored in mono. Link at 100% lets the louder channel open both
gates. External needs the host's sidechain route; an unconnected external input
is silence and closes the gate. Audition listens to the filtered sidechain;
its status remains visible when Expert is collapsed.

HP at 0 Hz and LP at 20 kHz are Off. Both filters are first order (6 dB/octave)
and affect detection/audition only. Lookahead advances detection by 0–10 ms;
the audio, parallel dry and bypass always have ceil(rate × .01) samples of
latency. Output trim follows the mixed/audition signal. Bypass returns delayed
untrimmed input.

Double-click any parameter to reset its declared default. Click its readout,
right-click the control, or focus it and press Enter for exact entry. Shift-drag
makes finer changes. Tab/arrows, A/B, Copy, undo/redo and starting presets follow
the suite conventions. Presets are starting points, not source-specific promises.

## Qualification boundary

See [status](status.md) for executed checks and [audio contract](gate-plan.md)
for equations and permanent IDs. This is a measured working alpha. Passing
numerical and host tests does not establish subjective parity with Pro-G or
readiness for irreplaceable sessions. Fast timing can audibly modulate bass;
listen to releases/tails and transient preservation at matched levels.

Pending: actual Bitwig scan/routing/playback/recorded automation, modulation,
save/reopen, duplicate/export and prolonged use; vocals, drums, bass and noise
listening; worst callback / multi-instance profiling. No oversampling, M/S,
MIDI trigger, proprietary style emulation, zero-latency mode, user preset browser
or full screen-reader accessibility. Tail reporting is conservatively infinite
for filtered audition and silence-based suspension is not yet implemented.
UI views, A/B and local undo persist only for the editor session; audio parameters
are saved in independent checksummed schema 1. Existing effects' sound/state is
unchanged.
