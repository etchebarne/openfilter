# First EQ plan

Implementation progress and evidence are tracked in [status.md](status.md).
The static baseline, loadable CLAP alpha and interactive editor now exist.
Matched-filter design comparison, band audition, Bitwig listening/recall
validation and release gates remain open. See editor.md for the delivered UI.

Deliver a useful, dependable static parametric EQ before adding dynamic bands,
linear-phase processing, or spectral features. These are staged deliverables,
not a claim that the first build is production-ready.

## Intended first release

- Up to 24 fixed band slots, initially inactive. Create/remove bands through the
  editor without changing parameter IDs.
- Bell, low/high shelf, low/high cut, and notch filters. Cuts support 12/24/48
  dB/oct and the independently measured elliptic Brickwall mode; define cutoff and resonance semantics for each slope.
- Zero added latency for static minimum-phase processing.
- Stereo and mono operation; per-band stereo, left, right, mid, and side routing
  by the first usable release. Specify band ordering when mixing routing modes.
- Output trim, smooth global and band bypass, band audition, A/B, and undo/redo.
- Host automation, supported global CLAP modulation, and dependable session recall.
- Resizable graph editor, accurate response curves, numeric entry, pre/post
  analyzer, and input/output meters. Visual styling can evolve after interaction
  and audio behavior are proven.

Initial proposed controls: frequency 10 Hz–30 kHz subject to a safe sample-rate
dependent upper limit, bell/shelf gain ±24 dB, and Q 0.1–40 where meaningful.
Confirm limits with numerical and modulation tests. Define shelf Q independently
from cutoff slope; do not assume a generic Q control has identical meaning for
every shape. Stored targets and effective rate-limited values must be distinct
so changing sample rate does not destructively rewrite a preset.

## Filter design investigation

Use RBJ cookbook filters as a reference baseline. Compare matched-magnitude
designs, including Vicanek's published methods, particularly for high-frequency
bells and shelves at 44.1/48 kHz. Do not assume a plain bilinear-transform EQ
matches an analog target near Nyquist.

Evaluate a trapezoidal state-variable realization for parameter movement using
Cytomic's papers. Realization and frequency-response design are separate choices:
an SVF alone does not remove frequency warping. Benchmark candidate designs for
response error, numerical noise, extreme Q, coefficient cost, and fast modulation.
Do not treat interpolation of arbitrary recursive-filter coefficients as a
complete automation solution. Exercise state transitions and shape changes too.

Select the design from these results before freezing released preset semantics.
Use the actual digital transfer function for the displayed response. Plotting an
ideal analog curve over a different implemented filter is unacceptable.

No blanket oversampling requirement for a static linear EQ. Consider it only if
measurements justify a specific benefit and account for its CPU/latency cost.
Dynamic and nonlinear processing require their own aliasing investigations.

## Milestones

1. **Foundation and design measurements.** Build independent DSP and tests;
   compare baseline/matched filter designs; create native CLAP pass-through with
   parameters and versioned state. Demonstrate scan/load and recall in Bitwig.
2. **Loadable EQ alpha.** Implement bands, basic shapes, gain, smoothing, bypass,
   and timestamped events. Use Bitwig's parameter controls first. Validate the
   binary, stress automation, and measure CPU with many instances.
3. **Usable editor and routing.** Prototype native window embedding, then build
   the graph, analyzer, numerical controls, audition, M/S and L/R routing, A/B,
   and undo. Test UI/host changes concurrently and repeated editor open/close.
4. **Release candidate.** Complete measurement matrix, host regression sessions,
   listening comparisons, packaging, reproducible builds, and documentation.

Dynamic EQ follows the static release. Linear phase follows only with a defined
use case, latency budget, and pre-ringing tests. Analog phase matching is a
separate investigation; verify its response before making matching claims.

## Evidence required for release

The following are proposed acceptance criteria, not completed test results.

| Area | Evidence |
| --- | --- |
| Static accuracy | Compare impulse-derived transfer functions against independent analytical references across supported shapes and rates. Initial target: ≤0.01 dB implementation/reference error where response is above −60 dB, plus absolute-error checks near nulls. Measure analog-target deviation separately. |
| Identity and routing | Exact unchanged samples for the fully inactive, unity-gain path; M/S round-trip residual below −120 dBFS on bounded test signals; no unintended channel leakage. |
| Rates and blocks | 44.1, 48, 88.2, 96, 176.4, 192 kHz; sample blocks 1, 17, 64, 257, 1024 and the advertised maximum; reject unsupported configurations cleanly. |
| Automation | Event-offset tests, base/modulation separation, UI gestures, rapid frequency/Q/gain sweeps, band toggles and filter-type transitions. Compare outputs across block partitions using the same sample-timed event sequence. |
| Stability | Extreme supported settings, long decays, silence/denormals, large finite input, reset/reactivation, malformed state, and randomized valid automation. No unexplained NaN/Inf, runaway state, or stuck output. |
| Real time | Instrument callback allocation/locking, bounded queues, and worst callback timings on a documented machine. Record multi-instance cost with editor/analyzer open and closed. Set CPU budget from the user's machine before claiming performance. |
| Host contract | `clap-validator` plus reproducible fuzz cases; Bitwig scan, duplicate, bypass, save/reopen, modulation, export, and sample-rate changes. Validator success does not replace host tests. |
| Session compatibility | Stable plugin/parameter IDs, state round trips, malformed-state rejection without partial application, and fixture-based migrations after the first released schema. |
| Listening | Level-matched comparisons on vocals, drums, bass, full mixes and transient-rich material; sweep automation audibly. Match Q conventions and actual curves before interpreting differences from a reference EQ. |

Use licensed local reference plugins when available. Maintain a small reproducible audio corpus
with redistribution permission or generated signals. Save benchmark conditions
and measurement reports alongside each release.
