# OpenFilter Limiter 0.3.0

Linux native CLAP limiter with independent transient and sustained gain control,
mono/stereo float/double processing, adjustable lookahead, original limiting
styles, true-peak protection and output metering. This is a qualification
candidate for the user's Bitwig and real-material listening tests.
Modern styles now process audio at 4× internally at host rates through 192 kHz;
2× is used through 384 kHz and native rate above that.

## Workflow

Raise **Gain** to drive the limiter; start with occasional 1–3 dB reduction.
**Ceiling** trims the final output (default -1 dBFS). Increasing Gain drives
more limiting; lowering Ceiling trims it. The six-second history overlays
input (slate), output (teal) and attenuation (rose). The level axis is -36..0 dBFS;
reduction runs downward from 0 to 36 dB. Gold callouts mark attenuation maxima
within three two-second regions. Right-hand meters show output and reduction.

The vertical **Advanced** tab reveals:

- **Style:** Clean is the default balanced envelope. Punch recovers faster after
  peaks and uses a gentler sustained envelope. Dense recovers more slowly and
  applies more sustained attenuation. These are original OpenFilter voicings.
  Legacy recalls the 0.1 engine for older projects.
- **Lookahead:** 0–5 ms of transient anticipation/smoothing. Longer settings
  soften the gain onset; shorter settings can preserve immediacy but increase
  distortion. Start at 5 ms for clean work. Changing it does not change latency.
- **Attack:** 0.1–1000 ms; controls how quickly sustained attenuation develops.
  It does not allow peaks through the safety stage like a compressor attack.
- **Release:** 20–2000 ms; controls recovery of the sustained envelope.
  **Auto release** in the footer lengthens it according to recent attenuation.
- **Channel linking / Transients:** links the fast peak-catching stage.
- **Channel linking / Release:** links the slower envelope. Both at 100% preserve
  stereo ratios; lower settings allow the channels to react more independently.

Legacy uses fixed 5 ms lookahead and its original single linking control. Attack,
adjustable Lookahead and Release Link are dimmed in that mode and their stored
values are preserved for the new styles.

**True peak** enables an eight-phase reconstructed-peak guard after the main
limiter, plus true-peak output metering. The guard is fully stereo-linked and
uses a 0.3 dB reconstruction allowance near the ceiling. It also activates the
Nyquist safety filter in Legacy; modern modes always use that filter. Quiet audio is not
continuously trimmed by that allowance. With protection off, output meters show
sample peaks. Peak readings decay; they are not session-integrated maxima or
LUFS. Clicking the meter area clears the clipping latch.

**Unity gain** compensates input drive for comparison; it retains ceiling trim
and is not automatic loudness matching. **Bypass** returns latency-aligned,
untrimmed input and suspends the ceiling. Starting points are suggestions, not
genre-specific or delivery certifications.

Drag the gain's numeric handle or knobs vertically; Shift gives fine adjustment.
A stationary click on the gain handle opens exact entry. Click a numeric field,
right-click a control, Ctrl-click, or focus it and press Enter for exact entry.
Double-click resets its descriptor default. Tab/Shift+Tab navigate, arrows adjust,
Enter commits and Escape cancels. A/B, Copy and Ctrl+Z / Ctrl+Shift+Z provide
comparison and undo/redo. Host state loads clear local undo. The Advanced drawer
is a view setting, not an audio parameter.

## Compatibility and latency

Old seven-parameter states migrate to Legacy with True Peak off. New instances
start in Clean with True Peak on. Parameter IDs 0–6 and their ranges/defaults
are unchanged. New controls use IDs 7–11. Saved state now uses schema 2 and cannot
be loaded by the older binary. Keep backups when testing upgrades.

Version 0.3 keeps schema 2 and the same parameters, but intentionally changes
the sound of modern styles through audio oversampling and band limiting. Existing
modern-style states use the new sound; exact 0.2 modern recall requires the old
binary. Legacy with True Peak off retains the old gain behavior.

Latency is fixed through automation, bypass and protection changes: **914 samples /
19.04 ms at 48 kHz**, up from 640 in 0.2. Legacy is padded to the same delay. The
host must refresh delay compensation; recheck older sessions' timed automation.
The tail reported to CLAP includes the FIR filter tails, not just group delay.
See [DSP contract](limiter-dsp.md) for other rates and exact semantics.

## Qualification boundaries

The independent tests cover sample ceilings, reference equations, reconstructed
peaks, stereo linking, timing, sine residuals, state migration and host events.
They do not establish sonic equivalence to FabFilter or transparency on all music.
True peaks are estimated with finite reconstruction filters. The known gated
Nyquist failure is addressed by a narrow safety band limit and checked using
longer independent filters and libebur128. This is not a universal DAC/codec
guarantee or formal BS.1770 meter certification.

Audio-path oversampling reduces sampling artifacts; it does not eliminate all
distortion from fast/heavy limiting. Linear-phase filters add some pre/post
ringing and a transition close to Nyquist. The audible-band transfer and
high-rate convergence tests are recorded in status.md. LUFS metering and dither
remain optional, unimplemented features.

## Build and validation

Build: `cmake --build --preset release --target OpenFilterLimiter`.
Install: `bash tools/install-limiter-local.sh` (only this artifact).
Refresh Bitwig's plugin scan, then load **OpenFilter Limiter** in a new scratch
project. Restart its plugin process after replacing a loaded binary.

Automated checks cover actual audio, independent reference equations, ceiling,
latency/rates, linking, timing, exact host event offsets, block partition
invariance, state/base/modulation, no C++ allocations during callbacks, native
editor gestures/backpressure and scaling. See status.md for the recorded run.

Still required in Bitwig: level-matched listening on varied real music; automation
and modulation removal; bypass; mono/stereo; save/reopen and duplication;
sample-rate changes; offline exports; UI resizing and long-session behavior.
Record the host version and settings. Synthetic host success does not complete
these checks. Use only a new scratch project, never an existing DAW project.

For the current acceptance pass, use the [Bitwig/listening handoff](limiter-testing.md).
The previous 0.2 binary is preserved under
`reports/limiter-0.3/OpenFilterLimiter-0.2.0.clap` outside the plugin scan directory.
