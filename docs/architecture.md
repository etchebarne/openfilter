# Architecture and implementation direction

Research date: 2026-09-22. Confirmed first platform and host: Linux / Bitwig.
The native CLAP/DSP foundation is now implemented for EQ, compressor, limiter and reverb. See status.md and
development.md for current evidence and workflow. The native editor is implemented;
see editor.md for interaction, threading, and display contracts.

## Recommended stack

Use C++20, CMake, and the official CLAP headers with `clap-helpers`. Build one
separate `.clap` artifact per effect. The suite shares source libraries, without
requiring a separately installed OpenFilter runtime.

A native CLAP integration gives direct ownership of timestamped parameter events,
the distinction between base values and modulation, state, ports, and lifecycle.
The cost is that we must implement and test that integration ourselves. Keep it
small and shared; do not build a general-purpose plugin framework.

Use plain C++ for DSP and parameter definitions. DSP tests and measurement tools
must run without a GUI, Bitwig, or a plugin wrapper. Python/NumPy/SciPy are suitable
for independent reference measurements, not the audio processing path.

The editor uses Pugl (ISC) for native window embedding and Cairo for vector
rasterization. The initial backend is embedded X11, driven by the host's CLAP
timer support at roughly 30 Hz. Drawing/FFT run on the main thread. This replaces
the initial JUCE recommendation after implementing and testing the smaller Pugl
integration. Shared UI primitives live in `libs/ui`; EQ-specific layout and
transfer-response plotting remain in `plugins/eq`. `docs/ui-conventions.md`
defines suite-wide behavior: double-click resets descriptor defaults, with
separate exact-entry actions. Shared theme and click handling live in `libs/ui`.

This choice keeps our source MIT licensed and avoids adopting a second plugin
wrapper. Cairo/X11 are system dynamic libraries. The tradeoff is that keyboard
navigation and accessibility must be implemented explicitly; full screen-reader
support and other platforms are not yet provided. Dependencies and their terms
are recorded in THIRD_PARTY_NOTICES.md.

## Alternatives considered

| Approach | Advantage | Reason not to select it initially |
| --- | --- | --- |
| JUCE AudioProcessor + clap-juce-extensions | Established path with existing users and extensive framework facilities | Adds an abstraction between our CLAP events and DSP; the adapter is unofficial and its documented heading covers JUCE 6–8, whereas current JUCE master references 9. Pin and verify compatibility if choosing this route. |
| DPF with DGL | C++ framework, CLAP output, permissive ISC framework license | Worth a prototype if permissive licensing is important; verify event/modulation coverage and custom-editor requirements first. |
| Rust plugin framework | Attractive language safety and viable CLAP ecosystem | C++ is the recommendation for this project's DSP and native GUI ecosystem; Rust is not inherently lower quality. |
| Native CLAP + helpers | Direct format control and independent DSP | Selected; requires more explicit lifecycle, threading, and GUI integration work. |

Framework choice does not establish audio quality. Filter design, automation
behavior, numerical implementation, and measurement determine that.

## Proposed layout

```text
openfilter/
  CMakeLists.txt
  CMakePresets.json
  cmake/                     # compiler options, pinned dependencies, packaging
  libs/
    dsp/                     # filters, smoothers, gain, metering; no GUI or CLAP
    parameters/              # stable IDs, ranges, units, parameter mappings
    plugin/                  # shared CLAP lifecycle, event and state support
    ui/                      # shared controls, scales, plots, themes
  plugins/
    eq/                      # EQ engine composition, parameters, entry, editor
    compressor/              # create when work starts
    limiter/
    reverb/
    multiband/
    deesser/
    gate/
    distortion/
  tests/                     # DSP, state, host-contract and regression tests
  tools/                     # offline render, measurement, benchmark utilities
  docs/
```

Create shared pieces as the EQ needs them. Extract additional abstractions when
another effect demonstrates the need. Avoid speculative dynamics/reverb code.

Dependency direction: plugin composes shared libraries; DSP depends on neither
CLAP nor UI; UI reads snapshots and sends parameter gestures through the shared
parameter bridge. UI rendering and FFT work never run in the audio callback.

## Audio and host contracts

- Preallocate during activation. No locks, allocation/deallocation, file access,
  logging, or GUI calls in processing. Bound queues and handle overflow explicitly.
- Apply events at their sample offsets. Keep host base values and modulation
  separate; ending modulation restores the underlying value. Smooth deliberately
  without silently converting every event to a block-boundary update.
- Use double-precision filter coefficients and state initially, supporting float
  host buffers and testing double buffers if advertised. Precision helps numerical
  robustness; it is not a substitute for a sound filter design.
- Reserve fixed band slots and permanent parameter IDs. Adding, deleting, or
  visually rearranging bands must not renumber automation targets.
- Serialize versioned base state, not transient modulation or live filter history.
  Validate incoming data and publish coherent state changes to the audio thread.
- Handle reset, bypass, reactivation, sample-rate changes, in-place buffers, quiet
  tails, and offline rendering deliberately. Report latency and tails accurately.
- Pin dependencies to immutable revisions and retain notices. Keep clean builds
  reproducible; enable only CLAP product targets.

## Later suite development

Recommended sequence after the EQ: compressor, gate, de-esser, multiband
compressor, saturator, limiter, reverb. This is a reuse-based proposal, not a fixed
commitment. Dynamics can share detectors and envelopes; multiband effects need
verified crossovers; saturation and limiting need anti-aliasing and lookahead
infrastructure. Reverb remains a substantial independent design/listening effort.


## Compressor addition (0.1.0)

The second effect lives in plugins/compressor with separate engine, editor,
parameters, CLAP entry and artifact. The integration follows the EQ's tested
ownership model and shares its small concurrency/stream and drawing primitives.
DSP uses fixed rings for the maximum 10 ms lookahead at supported rates; the
host-reported delay is constant during activation, including bypass. Its meter
tap is a bounded SPSC queue of 10 ms peak/reduction frames, consumed on the main
thread. It introduces no dependencies and changes no EQ parameter or sound.
See compressor-plan.md for equations, state and validation contracts.


## Limiter addition (0.1.0)

`plugins/limiter` owns a separate sample-peak engine, parameters, editor and CLAP
entry. Its fixed-storage sliding-minimum gain computer and moving-average
attack run independently of the host/UI. Five-millisecond delay is constant
through bypass and all automation; input gain travels with delayed audio.
The CLAP adapter retains the established snapshot, queued-state and gesture
backpressure model, with one main audio bus and no sidechain. The editor uses
the original L wordmark and shared graphite materials. See limiter-plan.md for
proof, measurement gates, and remaining true-peak/loudness work.

## Limiter dual-stage revision (0.2.0)

The limiter now retains LegacyEngine for old-state recall and uses Engine for
independent transient and sustained envelopes, original voicings, stable-latency
lookahead blending, and a post-gain reconstructed-peak guard. A separate final
output detector supplies true-peak meters. All processing/storage remains owned
by the limiter and independent of CLAP/UI. See limiter-dsp.md for the precise
signal path, schema 2 migration and increased fixed delay.

## Limiter oversampled revision (0.3.0)

Engine now composes ModernEngine at 4× (through 192 kHz host rate), half-band
resamplers, a Nyquist safety FIR and post-decimation sample/true-peak guards.
LegacyEngine stays at the host rate with aligned padding. Peak estimation uses
cascaded sparse half-band interpolation. Minimum queues bound replacement work
with binary search. No shared DSP abstraction or new runtime dependency is added.
Schema 2 is retained; modern sound and fixed latency change explicitly. See
limiter-dsp.md for numerical definitions and user acceptance gates.


## Reverb addition (0.1.0)

The fourth effect owns an original eight-line Hadamard feedback-delay network,
four stereo input allpass diffusion stages, fractional predelay and modulated
tank reads, six loop-loss EQ bands per line and six wet post-EQ bands. Fixed
storage covers 1–768 kHz without audio-thread allocation. There is no added dry
latency. A conservative infinite tail supports freeze automation. Snapshot,
state queue, gesture backpressure and native editor lifecycle follow the existing
plugins. The shared analyzer performs FFTs only on the main thread. See
reverb-plan.md for loss bounds, approximation limits and the schema 1 contract.

## Reverb refined engine (0.2.0)

New instances add three lossless feedforward delay/mixing stages before the
eight-line tank, integer tank reads with crossfaded geometry changes, and
energy-preserving matrix modulation. Twelve signed early reflections per
channel replace the single-copy early path. Two bounded filter branches blend
shape changes. Fixed buffers and DSP remain independent of the host/UI.
Schema 2 carries an engine revision; schema 1 sessions retain the old algorithm
and resave as Legacy. All 80 parameter IDs and the plugin ID remain stable.
See reverb-quality.md for research, actual-audio results and remaining gates.


## De-esser addition (0.1.0)

The fifth effect owns independent DSP, parameters, editor and CLAP entry under
`plugins/deesser`. Detector HP/LP and a dynamic high shelf reuse the proven TPT
SVF primitive. Fixed rings provide constant 15 ms audio latency with fractional
request lookahead. A spectral-balance heuristic weights Vocal detection;
Allround uses absolute filtered RMS. Mode changes ramp without new allocations.
The native editor reuses suite materials and its approved DS artwork. State and
host/UI ownership follow the compressor's tested queue/snapshot model. See
deesser-plan.md for equations and the minimum-phase/linear-phase distinction.


## Gate addition (0.1.0)

The sixth effect lives under `plugins/gate`. It owns independent downward
expansion, hysteresis/hold, stereo detector linking and latency-aligned program /
sidechain rings. Peak/RMS, detector HP/LP, sidechain audition and parallel wet/dry
balance use fixed storage. CLAP state/event/UI ownership follows the tested
compressor adapter; existing ramp, queue, snapshot, stream and drawing primitives
are reused. The reference-inspired control arrangement uses original suite
materials and the approved G artwork. See gate-plan.md for stable IDs, schema 1,
equations, finite rate limits and the qualification boundary.
