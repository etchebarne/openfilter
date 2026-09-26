# Saturator 0.2.0 — Drive level correction

Version note: 0.3.1 changes the factory Drive comp. amount to **0%**. The
matched-level measurements and auditions below explicitly use **100%** to keep
the research conditions reproducible. Existing saved values are preserved;
see [the current guide](saturator.md) for the untrimmed default workflow.

Historical milestone: the user subsequently rejected this sound. Version 0.3
adds calibrated voices after a [source study and audio comparison](saturator-research.md).
The measurements below qualify the 0.2 gain correction, not musical acceptance.

The 0.1 default applied `shape(input × drive) / drive`. Once the waveshaper
approached its ceiling, the division kept reducing output. This generated
harmonics but made a selected band fade as Drive rose. The previous numerical
and alias tests verified that formula without testing the intended control
behavior. That was a missing product requirement in the test suite.

Reproduction at 48 kHz, approximately 1 kHz in a soloed mid band, Soft style,
−12 dBFS input peak and 100% compensation:

| Drive | Previous output RMS |
| --- | ---: |
| 0 dB | −15.15 dBFS |
| 12 dB | −16.80 dBFS |
| 24 dB | −24.79 dBFS |
| 36 dB | −36.19 dBFS |

## Corrected behavior

New instances enable **Auto level**. Each band compares smoothed dry/input
power with saturated power and applies one shared gain to both channels.
Asymmetric DC is excluded from that comparison. The matcher precedes tone,
mix and band Level. Drive still changes the same oversampled nonlinear curves;
the correction changes their gain staging. Drive compensation at 100% requests
full power matching; 0% leaves Drive's added level untrimmed.

Four-pole power/mean detectors and a smoothed gain avoid following individual
audio cycles. The match adapts over a few tenths of a second. It can affect
attacks and changing envelopes; it does not promise instantaneous level
invariance, perceptual loudness equality, or peak limiting. Quiet input is not
normalized to a fixed absolute loudness. The same gain is used for both channels,
with energy measured separately so anti-phase audio does not disappear.

**Existing projects:** schema-1 states load with Auto level Off, retaining the
old audio and automation. Enable **Auto level** in the footer to adopt the fix,
then save. Schema 2 appends parameter ID 43; IDs 0–42 and the plugin ID remain
unchanged. State load/re-save and actual legacy audio are regression-tested.
After installing, restart the plugin process or Bitwig to load the new binary.

The official [Saturn band-control documentation](https://www.fabfilter.com/help/saturn/using/bandcontrols)
also treats Drive as driving a clipping stage while managing output level.
Our matcher is an original implementation, not a reproduction of FabFilter's
proprietary gain law or models.

## Measured regression coverage

`tools/measure_saturator_drive.py` records all stimuli and results under
`reports/saturator-drive/measurements.json`:

- 240 Drive/level cases: 44.1/48 kHz, all four styles, input peaks −24/−12/−6
  dBFS, low/mid tones near 55 Hz and 1 kHz, and 0/6/12/24/36 dB drive.
  Worst settled RMS drift versus 0 dB Drive: **0.00649 dB**.
- At 24 dB drive, absolute harmonic energy increases by at least **13.36 dB**
  across that matrix. Asymmetric already has even harmonics at 0 dB Drive, so
  its relative growth is smaller; the test also requires substantial absolute
  distortion. Thresholds are >12 dB growth and THD above −30 dB at 24 dB Drive.
- 96 additional alias cases: 44.1/48/96/192 kHz, all styles, −6 dBFS input,
  12/24 dB Drive, tones near 7/13/19 kHz. Worst unwanted residue in
  20 Hz–20 kHz: **−102.77 dBc**; gate −90 dBc.
- Twelve independent SciPy references include random audio, tone and both
  dynamics directions across three rates. Peak difference: **2.23e-10**.
- Loud/quiet/pause/restart and anti-phase stereo checks; compensation at zero
  must match the original untrimmed processor. Host tests exercise mode changes
  at exact sample offsets and compare different block partitions.
- Original gain mode retains its golden automation fixture and the prior
  independent/reference and 576-case quality scripts, which now explicitly
  select Auto level Off. Their previous quality limits remain applicable.

Thirty newly rendered, RMS-matched EBU SQAM clips are available locally under
`reports/saturator-drive/listening/`. Their manifests include source offsets,
hashes, parameters and matching gains. They are private R&D evaluation material,
not shipping assets or a completed human listening review.

All 28 release CTest suites and all four saturator ASan/UBSan suites passed.
External clap-validator passed 36 checks, with zero failures/warnings and eight
unsupported optional checks skipped; all five retained fuzz seeds passed. UI
normal/compact/2×/menu/Help and the required EQ normal/compact/2×/menu/24-band
views were rendered and inspected. Native tests used DISPLAY=:0 without skips.

On the same Ryzen 5 5600GT, 48 kHz/64 frames, editor closed, median default
stereo CPU over three paired runs was **9.68% → 10.41% of one core** (0.1.1 →
0.2.0). Mono was 4.98% → 5.67%; silence 0.19% → 0.21%. A 120-second automated
run measured 22.15% CPU, 400.26 µs p99 and 859.63 µs maximum wall time, with zero
1.333 ms deadline exceedances and no guarded C++ allocations. These are unpaced
synthetic-host measurements, not Bitwig scheduling guarantees. Raw records:
`reports/saturator-drive/callbacks.json`.

The level-step test settled within 0.003 dB after changes, had exact anti-phase
channel symmetry, and decayed below 3.2e-17 during the measured pause. This
numerical test does not replace listening for adaptation on production material.

Bitwig listening/recall and representative production sessions remain pending.
The extreme-drive aliasing limitation documented in saturator-quality.md is not
removed by level matching; raising its level can make existing artifacts more
audible. This correction addresses the Drive workflow, not a claim of complete
professional qualification.
