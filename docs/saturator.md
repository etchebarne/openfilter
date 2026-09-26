# OpenFilter Saturator — 0.3.1 alpha

An original three-band saturation processor for Linux / Bitwig, built separately
as `build/release/plugins/OpenFilterSaturator.clap`. Install just this effect with
`bash tools/install-saturator-local.sh`.

## Working with it

Select a band in the spectrum or with LOW / MID / HIGH. Drag the crossover lines
horizontally and the band handles vertically for level. The editor has a continuous
warm spectrum canvas, a shallow floating control rail, and a larger drive dial.
The LOW / MID / HIGH selectors at the bottom summarize each band's stored style
(or Off / Solo / Muted state) and drive. Their normalized curve glyphs show the
static waveshaper at that drive; they exclude dynamics, tone, mixing and output
level and are not full-processor transfer measurements. The spectrum fades under
the controls for label contrast; analysis values and audio remain unchanged.
The selected controls are:

- **On, Solo, Mute:** processing enable passes the dry band when off. Solo/mute
  operate after band processing; multiple solos combine. Global Mix can blend
  the full dry signal back into a solo/mute audition.
- **Style:** Soft (tanh), Rounded (arctangent), Dense (algebraic soft saturation),
  Asymmetric (biased tanh), **Punch** (calibrated odd harmonics, the default),
  and **Color** (calibrated even harmonics). Punch/Color start clean at zero Drive
  and reach stronger saturation earlier on quieter stems. These are mathematical
  voices, not claims of tape, tube, transformer or Saturn model matching.
- **Drive:** 0–36 dB before saturation. **Drive comp. defaults to 0%**, leaving
  the driven level untrimmed. Raising Drive therefore increases level as well as
  saturation. **Auto level** is enabled for new instances but its matching amount
  is zero until Drive comp. is raised: 100% matches average band power, with
  intermediate values applying partial matching. Tone and band Level follow this matching.
  Matching adapts over roughly a few tenths of a second, can change envelopes,
  and is neither a peak limiter nor a perceptual/LUFS loudness matcher.
- **Auto level Off** retains the old inverse-drive law. At 100% Drive comp., it
  removes every dB added by Drive, which makes saturated bands quieter. Old
  schema-1 sessions load in this mode to preserve their sound. Turn **Auto level On** in
  the footer and raise Drive comp. if you want adaptive matching, then save.
  Existing sessions retain their saved compensation value; this default change
  does not rewrite them. Double-click Drive comp. to reset it to the new 0% default.
- **Mix:** blends the band's wet and dry signal. Global Mix blends all bands.
- **Dynamics:** negative values expand quiet signals; positive values compress
  loud signals. Fixed 10 ms attack / 100 ms release with stereo-linked detection.
  Zero disables dynamic gain changes; it does not disable saturation.
- **Bass / Mid / Treble / Presence:** ±12 dB post-saturation tone filters at
  160 Hz / 800 Hz / 3 kHz / 8 kHz. **Level** is the band's output trim.

Both crossovers are 24 dB/oct Linkwitz–Riley, initially 250 Hz and 4 kHz. They
rotate phase. The low band receives matching phase compensation at the upper
crossover; unity bands sum flat. The upper crossover is limited to 45% of the
sample rate, and the lower stays at or below 80% of the upper. Stored automation
values remain independent. The graph uses effective crossover positions and
shows an effective-frequency note when clamping or host modulation changes them.
Very close bands have compact level handles; the panel always retains exact entry.

The dry mix passes through the same crossover and resampling response as wet.
Bypass returns the raw input with the same **76-sample latency** (1.583 ms at
48 kHz). Bypass therefore removes crossover phase rotation. External parallel
routing with an unfiltered dry track can produce phase cancellation; use the
internal mixes for aligned parallel saturation. The resampling filters are
linear phase; the crossover and tone filters are not.

The spectrum is actual pre/post stereo power analysis, including anti-phase
signals. Analysis runs only while the editor is visible. Meters show sample
peaks, with a clip latch cleared by clicking the meter area. They are not LUFS
or reconstructed-peak meters. Input is shown before the plugin's input trim.
The handles are band level controls, not a spectral transfer-response prediction.

Double-click resets a control to its descriptor default. Click a readout,
right-click a control, or use Tab then Enter for exact entry. Shift makes drags
fine; arrow keys and scrolling adjust values. A/B, Copy, undo/redo and five
starting-point presets are editor-session workflows. Audio parameters, including
solo/mute and Auto level, are saved in schema 3. Schema-1 sessions load with
Auto level Off; schema 2 retains its saved mode. Both retain their saved styles.
Choose Punch or Color to try the new sound. Restart the plugin process after
installation; Help displays v0.3. A/B/history/window size are not project state.

## Quality boundary

This is a measured alpha, not a completed studio-release qualification.
Nonlinear audio runs at **32× oversampling at 44.1/48 kHz**; this setting consumes
CPU. Version 0.1.1 reduced the old-mode callback load by about half; 0.2 adds
a small per-band level detector.
See [CPU and quality measurements](saturator-quality.md) for the workloads and
limits. Exact-zero histories take a low-cost path while parameter smoothing
continues; no noise gate truncates tails. Tail reporting remains conservatively
infinite and the CLAP adapter continues callbacks; host sleep remains pending.
At higher host rates the factor becomes 16× through 96 kHz,
8× through 192 kHz, 4× through 384 kHz and 2× above that. Padding keeps latency
at 76 samples. Tone filters and DC rejection run at host rate after matched
wet/dry downsampling. All parameter changes ramp over 10 ms; style changes blend
curves. No audio-thread allocations, locks, files or GUI work are used.

The reproducible tests include `tools/measure_saturator_character.py` (new voices),
`tools/measure_saturator_drive.py` (0.2 voices),
`tools/measure_saturator.py` / `tools/measure_saturator_quality.py` (legacy recall),
and four CTest suites. The new mode adds Drive/output-level and harmonic-growth
gates, stereo/level-step checks, its own alias matrix and an independent audio
reference. See [the Drive correction](saturator-drive.md).
See [the source investigation and matched auditions](saturator-research.md) for
the new voices and their separate full-Drive measurements. Historical
reports under `reports/saturator` describe the exact stimuli for styles 0–3. A −6 dBFS coherent
sine test covers four styles at three high frequencies and three drive values.
At 24 dB drive the worst non-harmonic residue is approximately −106 dBc; at
36 dB drive it rises to about −55 dBc. These are specific test conditions, not
universal alias-free guarantees. Intermodulation generated by nonlinear processing
is part of the effect and is distinct from aliasing. The 128× independent reference
also tests a two-tone input. Near-Nyquist content is intentionally attenuated by
anti-alias filters; at 44.1 kHz the first FIR alone is about −1.1 dB at 21 kHz.

Pending: Bitwig playback, automation recording, duplicate/save/reopen/export,
long-session and multiple-instance profiling, and level-matched listening on
vocals, drums, bass and complete mixes. Synthetic native-host success does not
establish these outcomes. No existing DAW project is touched by the test suite.

This initial version has three fixed bands and four original styles. Variable
band count, feedback, internal LFO/envelope modulation, linear-phase crossovers,
M/S processing, user preset files and full screen-reader support remain future
work. Host CLAP modulation is supported for continuous parameters.

Research sources and permanent IDs are in [saturator-plan.md](saturator-plan.md).

Numeric readouts support both exact entry and dragging throughout the suite:
click and release to type, or drag vertically to adjust (up increases, down
decreases). Hold Shift for finer movement. Double-click resets the descriptor
default. A drag is one undo step and one balanced host automation gesture.
