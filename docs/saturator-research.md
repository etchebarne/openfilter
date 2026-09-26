# Saturator source study and calibrated voices — 0.3.0

Version note: 0.3.1 changes the factory Drive comp. amount to **0%**. The
matched-level measurements and auditions below explicitly use **100%** to keep
the research conditions reproducible. Existing saved values are preserved;
see [the current guide](saturator.md) for the untrimmed default workflow.

The user rejected both the original sound and the 0.2 level correction. Passing
an alias test and producing measurable harmonics did not demonstrate a useful
saturation control. This investigation separates source evidence, measured
behavior, and the listening judgment still needed.

## Sources actually studied

| Reference | What the source/manual establishes | Application here |
| --- | --- | --- |
| [Airwindows Drive](https://github.com/airwindows/airwindows/blob/d22a25b7f7c0c8c05e9f3ae480e7f1da9769f49d/plugins/LinuxVST/src/Drive/DriveProc.cpp) | Cascaded polynomial saturation; Drive controls stage depth, with input clipping, optional wet high-pass and separate output/mix. | A useful drive control need not be a literal unbounded gain into one curve. The calibration and output law are part of the sound. |
| [Airwindows Density](https://github.com/airwindows/airwindows/blob/d22a25b7f7c0c8c05e9f3ae480e7f1da9769f49d/plugins/LinuxVST/src/Density/DensityProc.cpp) | Repeated sine shaping plus a fractional stage; negative control selects a different operation. | Static nonlinearities can produce deliberate character. Memory is not a prerequisite for a useful saturator. |
| [Airwindows PurestDrive](https://github.com/airwindows/airwindows/blob/d22a25b7f7c0c8c05e9f3ae480e7f1da9769f49d/plugins/LinuxVST/src/PurestDrive/PurestDriveProc.cpp) | Sine-shaped signal mixed with dry according to current and previous sample magnitude. | Signal-dependent mixing is a separate design choice; it is not our slow RMS output matcher. |
| [Airwindows Tube2](https://github.com/airwindows/airwindows/blob/d22a25b7f7c0c8c05e9f3ae480e7f1da9769f49d/plugins/LinuxVST/src/Tube2/Tube2Proc.cpp) and [ToTape6](https://github.com/airwindows/airwindows/blob/d22a25b7f7c0c8c05e9f3ae480e7f1da9769f49d/plugins/LinuxVST/src/ToTape6/ToTape6Proc.cpp) | Tube2 combines asymmetry, polynomial shaping and previous-sample behavior. ToTape6 includes frequency-dependent softening, resonant low-frequency processing and flutter. | Calling an ordinary static curve “tube” or “tape” would omit substantial behavior found in these designs. |
| [CHOW Tape source](https://github.com/jatinchowdhury18/AnalogTapeModel/tree/604372e4ffd9690c3e283362e4598cb43edbb475/Plugin/Source/Processors/Hysteresis) and [Chowdhury, DAFx 2019](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf) | Jiles–Atherton magnetic hysteresis with stored magnetization and numerical solvers, plus separate tape-related processing. Inspected the processor, solver and hysteresis-operation files. | A physical tape model is a distinct development effort, including stability and CPU qualification. No CHOW code or model is incorporated into this plugin. |
| [Soundtoys Decapitator manual](https://www.soundtoys.com/wp-content/uploads/Decapitator-Manual.pdf) | Different characters, Drive, optional gain adjustment, tone and parallel mix are separate controls. | Match level when comparing character. Its proprietary transfer functions are not disclosed or measured here. |
| [Klanghelm IVGI manual](https://klanghelm.com/docs/IVGI2-manual.pdf) | Input-dependent behavior, asymmetry control and frequency-selective response. | Harmonic symmetry and spectral excitation deserve separate attention; post-EQ alone cannot reproduce pre-emphasis. |
| [FabFilter Saturn band controls](https://www.fabfilter.com/help/saturn/using/bandcontrols) | Model/style, Drive, dynamics, tone, mix and output workflow. | Workflow benchmark, not source access or a measured sound match. |

Airwindows revision: `d22a25b7f7c0c8c05e9f3ae480e7f1da9769f49d` (MIT).
CHOW revision: `604372e4ffd9690c3e283362e4598cb43edbb475` (GPL-3.0 source,
studied only). Downloaded sources/licenses and experimental executables stay in
ignored `reports/saturator-research`; no third-party DSP is linked into the CLAP.
The new shipping implementation is original and does not emulate named hardware.

## Diagnosis and implemented change

The 0.1 inverse-drive law caused the reported band attenuation. Version 0.2
corrected steady power, but retained the same saturation onset. At −24 dBFS
input peak and 12 dB Drive, a roughly 1 kHz soloed mid-band tone through Soft
measures only −45.84 dB THD. Independent band splitting also reduces each
curve's input level and removes some cross-band intermodulation. That makes a
full-band saturator's settings an unsuitable one-for-one comparison.

Version 0.3 adds **Punch** and **Color**, with Punch the new-instance default.
Both start exactly linear at zero Drive, then reach useful excitation earlier:
6/12 dB Drive corresponds to curve excitation gains 5.32/9.58, rather than the
old 2/3.98. Excitation approaches a finite ceiling, avoiding the progressively
steeper waveform transitions of unlimited gain at maximum Drive. Punch is odd
symmetric. Color has a stronger even-harmonic component. These remain static
mathematical voices, not magnetic-memory or tube-circuit models.

At the same −24 dBFS / 12 dB / 1 kHz test:

| Voice | THD | Second harmonic | Third harmonic |
| --- | ---: | ---: | ---: |
| Soft | −45.84 dB | below −300 dBc | −45.84 dBc |
| Asymmetric | −27.72 dB | −27.75 dBc | −49.33 dBc |
| Punch | −31.20 dB | below −300 dBc | −31.21 dBc |
| Color | −15.13 dB | −15.13 dBc | −47.99 dBc |

THD describes the changed character; a larger value does not prove better sound.
A sharper polynomial prototype was rejected: at an excitation gain of 16 and
−6 dBFS peak input its measured worst high-tone alias residue was around
−76 dBc. The chosen smooth voices retain the existing oversampling and CPU
structure instead of increasing oversampling to conceal that failure.

## Reproducible signal definition and compatibility

For displayed Drive `D`, let `g = 10^(D/20)` and `k = 16*(g-1)/(g+1)`.
For the oversampled input `x` after linked dynamics:

- At `k=0`, both voices output `x`.
- Punch: `g*tanh(k*x)/k`.
- Color: `g*tanh(k*x)/(k*(1 + 0.65*tanh(k*x)))`.

The scale retains small-signal gain `g` before output compensation. Color's
expression is a stable identity for a shifted tanh with its zero offset removed
and origin slope normalized; it evaluates exactly to zero for zero input.
The existing per-band Auto level follows these stages. Near zero Drive its gain
crossfades from inverse-drive gain to the matcher using `min(1,k)`; style weights
participate so automated transitions remain continuous. This keeps zero Drive
exactly clean, including DC, instead of letting a mean estimator change it.

Original style enum values 0–3 retain their meanings. Punch/Color append 4/5;
parameter IDs, units, counts, plugin ID and latency are unchanged. Schema 3 saves
all 44 values and accepts the extended style range. Schema 1/2 recall their
stored styles and gain mode; schema 1 still migrates Auto level Off. Old schemas
reject out-of-range new style values. Double-click Style resets to the new
factory default, Punch. Old factory presets partly inherit the new default;
saved projects store explicit values and retain their previous voices.

To audition the change in an existing project, restart the plugin process,
check **v0.3** in Help, select **Punch** or **Color**, and enable **Auto level**.
Try 6–12 dB Drive. Merely reopening an old state intentionally recalls its old
voice. Input trim, band Level and Mix remain useful controls; this is not a
fixed-loudness normalizer or peak limiter.

## Tests and listening material

`tools/measure_saturator_character.py` renders actual engine output:

- 240 cases compare old/new voices at 44.1/48 kHz, low/mid tones, −24/−18/−12
  dBFS peaks, and 0/6/12/24/36 dB Drive. New-mode settled RMS drift ≤0.00319 dB.
- 96 new-mode alias cases cover 44.1/48/96/192 kHz, approximately 7/13/19 kHz,
  −6 dBFS peaks, and 6/12/24/36 dB Drive. Worst 20 Hz–20 kHz unwanted residue
  **−104.27 dBc**. This covers maximum Drive under those conditions, not arbitrary
  input boosts, near-Nyquist content, or every automation trajectory.
- 18 independent SciPy references include random audio, tone EQ, both dynamics
  directions and near-zero Drive. Peak error **1.35e-10**.
- C++/CLAP coverage includes exact zero-Drive dry nulls, legacy automation/audio,
  extended-style sample-offset transitions, state recall, parameter gestures and
  selecting the last menu row at normal/compact sizes.

`tools/research_saturators.py` compiles the unmodified double-precision processing
functions from the pinned Airwindows Drive and Density sources in an offline
harness. No VST/plugin is installed. It uses fixed noise seeds, HP off and full
output/wet. Six EBU SQAM excerpts are rendered with the same −12 dBFS-peak input:
dry; Soft/Punch/Color at 12 dB; Airwindows Drive at A=.85 and Density at A=.6.
Outputs are RMS matched with shared peak headroom. The reference designs are
full-band and these settings are **not equivalent distortion settings**; the
files are listening references, not a scored comparison or superiority claim.

The 36 individual WAV files, a dry/Punch/Color bass sequence, and exact source,
settings, offset, fade, gain and hash records live in
`reports/saturator-research/listening/`. EBU audio is private R&D material, not a
shipping asset or licensed under this repository's MIT license. No agent
listening verdict is claimed. Bitwig recall and controlled production listening
remain required before describing the plugin as professionally qualified.


## Host, compatibility and CPU qualification

All 28 release CTest suites pass, with affected suites rerun after the final
UI/state test updates. All four saturator ASan/UBSan suites pass. The initial
sanitizer host test exposed a stale plugin artifact; explicit target dependencies
now ensure targeted builds rebuild the loaded CLAP. External clap-validator
passes 36 checks with zero failures/warnings, eight optional skips, and all five
retained fuzz seeds. All prior saturator measurement scripts pass. 24 stereo
cases compared with the retained 0.2 renderer have a maximum difference of
8.33e-16; original 0.1 automation fixtures also pass.

Paired actual-CLAP CPU measurements on the Ryzen 5 5600GT, 48 kHz, 64 frames,
one instance, editor closed, median of three 20-second runs per default mode:

| Mode | 0.2 | 0.3 |
| --- | ---: | ---: |
| Factory default | 10.59% | 10.83% |
| Mono default | 5.83% | 5.90% |
| Exact silence | 0.219% | 0.229% |

Percentages represent one core. Factory default comparison intentionally uses
Soft in 0.2 and Punch in 0.3. A 120-second Color run uses 12.71%; automation
across all six styles and controls uses 29.64%. Their maximum thread-processing
times are 402/944 µs, below the 1333 µs buffer period. However, wall time exceeded
the period 13/29 times, reaching 3.26/5.41 ms. Scheduling interruptions are a
plausible contributor to that difference; this is not proof of reliable DAW
scheduling. These unpaced measurements retain every observation in
`reports/saturator-research/callbacks.json` and do not establish a maximum safe
instance count. No guarded C++ allocations occurred in audio processing.

Installed artifact SHA-256:
`851e39f1c16ca026d6a8d68cafc9cce916eea33e979a4df7381b78734307f796`.
