# Limiter 0.3 — Bitwig and listening handoff

Use a scratch project, reload the plugin process after installation, and record
Bitwig version, sample rate and buffer size. The installed artifact is
`~/.clap/OpenFilterLimiter.clap`; plugin version is 0.3.0.

## Host behavior

1. Start at 48 kHz / 256 samples, the initial configuration covered by the
   multi-instance CPU gate, then try your usual buffer. Confirm scanning, opening, resizing,
   closing/reopening, transport starts/stops and repeated playback work normally.
2. Add several instances and try a smaller buffer. Check Bitwig's DSP meter and
   actual dropouts, with editors both open and closed. Standalone CPU timing is
   documented in status.md; it does not certify host scheduling.
3. Automate Gain, Ceiling, Lookahead, Attack, Style and True Peak. Add/remove a
   modulator on a continuous control. Check smooth changes, parameter recall and
   that removing modulation reveals the saved base value.
4. Save/reopen and duplicate the device. Compare parameters and exports. For
   new 0.3 states, unchanged settings should render repeatably. Existing 0.2
   modern-style states intentionally sound different; Legacy/True Peak off
   retains its original gain behavior.
5. Check bypass and latency compensation against a parallel dry track. At 48 kHz
   every mode reports **914 samples / 19.04 ms**. Modern filters introduce ringing,
   so a modern wet signal is not expected to null against an unfiltered dry wire.
6. Export a passage with a transient near the end and enough tail. Compare
   playback/offline rendering and repeat at another sample rate. Check mono as
   well as stereo, including a quiet channel beside a loud transient.

## Sound

Use varied vocals, drums, bass-heavy mixes, acoustic material and full masters.
Begin with Clean, 5 ms Lookahead, 100 ms Attack, 200 ms Release, both links at 100%,
True Peak on and Ceiling −1 dBFS. Raise Gain for occasional 1–3 dB reduction, then
try heavier reduction deliberately.

Compare Clean/Punch/Dense, then short/long Lookahead and Attack/Release extremes.
Listen for bass roughness, softened or distorted attacks, release pumping,
pre-ringing, image movement and unwanted changes to ambience. Evaluate separate
channel linking on asymmetrical stereo material.

Match listening levels before comparing to bypass or another limiter. Unity Gain
compensates input drive and retains ceiling trim; it is not LUFS matching. Use an
external loudness meter/manual trim for a fair comparison. A louder result alone
is not evidence of better quality.

For ceiling checks, use an unencoded floating-point export, fixed ceiling,
True Peak on and Bypass off. Check an independent true-peak meter. Record its name
and settings if readings differ; finite reconstruction methods can disagree.

## Reporting a problem

Record the sample rate/buffer, style, Gain/Ceiling/Lookahead/Attack/Release/link
settings, True Peak/bypass state, whether it occurs in playback or export, and
whether it survives reopening. A short reproducible passage or parameter sequence
is more useful than an overall impression. No real-material listening or Bitwig
pass has been claimed by the implementation tests.
