# Development workflow

## Scope and source organization

The suite includes native CLAP EQ, compressor, limiter and reverb alphas with custom Linux editors.
See status.md for tested capabilities and outstanding work. See eq-plan.md for
the intended first release. Audio quality claims require measurements and
listening validation; interface design alone does not establish sound quality.

`libs/dsp` contains small audio primitives; `libs/parameters` contains reusable
parameter descriptors; `libs/plugin` owns shared concurrency and stream helpers.
`plugins/eq` owns EQ parameters, engine composition, CLAP entry and its own CMake
targets. `tools` contains offline measurements/install scripts. `tests` exercises
the engine and the actual shared library. `libs/ui` owns drawing and analyzer
primitives; the EQ editor stays in `plugins/eq`.
Create other effect directories only when beginning those effects.

The present source is MIT licensed. CLAP dependencies are MIT licensed, with
notices retained in THIRD_PARTY_NOTICES.md. No JUCE code is included. Resolve
licensing for the combined distribution before adopting its GUI modules; the
current MIT license does not change JUCE's AGPL/commercial terms.

## Reproducible local setup

Required: Linux, Git, pkg-config, Cairo/X11 development packages, Fontconfig
development files for tests, and Noto Sans (recommended). On Debian/Ubuntu install
`libcairo2-dev libx11-dev libfontconfig1-dev fonts-noto-core xvfb`. Also required: a C++20 compiler (GCC or Clang), Python 3.12 or newer, and
Rust/Cargo for the external validator. The sanitizer preset uses Clang and its
AddressSanitizer/UndefinedBehaviorSanitizer runtimes. Build tools are local:

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
export PATH="$PWD/.venv/bin:$PATH"
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release
python tools/measure_eq.py
bash tools/build-validator.sh
bash tools/check-clap.sh
```

The first configure downloads exact CLAP/helper/Pugl Git revisions. Subsequent builds
reuse them. Do not replace SHA pins with moving branches. Review API changes,
licenses and validator results when updating a pin. Python tools are version
pinned in tools/requirements.txt; Rust uses the validator's committed lockfile.

For memory/undefined-behavior checks:

```sh
cmake --preset sanitize
cmake --build --preset sanitize -j 4
ctest --preset sanitize
```

For a bounded fuzz run and DSP timing:

```sh
build/tools/clap-validator/target/release/clap-validator fuzz -j 2 -d 60s build/release/plugins/OpenFilterEQ.clap
build/release/eq_render --benchmark
```

`tools/check-clap.sh` runs validation serially to reduce timing-test noise, then retained regression seeds from
tests/fuzz-seeds.txt. Save failing seeds and reduce them to a regression test. The test host guards
C++ new/delete in audio processing and active parameter flush; this does not
instrument every libc allocation or prove scheduling behavior. The benchmark
measures aggregate DSP throughput, not worst callback latency or DAW capacity.

For editor verification, run `build/release/editor_preview` and inspect generated
screenshots in `reports/ui/` at normal, compact, and 2× sizes. Native host tests
use their own X11 window and do not touch DAW projects. Without `DISPLAY`, use
`xvfb-run -a ctest --preset release` (and the sanitizer equivalent); otherwise
the native GUI test is explicitly skipped. The GUI test tears down process-wide
Cairo/Fontconfig caches only after all plugin instances have been destroyed.
Never perform global graphics-cache cleanup from a plugin instance. The CLAP
contract host links Cairo as a process-lifetime dependency too: Ubuntu 24.04
reports graphics initialization leaks on a bare Cairo dlopen/dlclose cycle,
even without a plugin. Both test hosts keep leak detection enabled.

## Change sequence

1. State the audible/behavioral requirement and its measurement or host test.
2. Implement the smallest coherent change in its owning library/plugin.
3. Add a regression for changed DSP, event timing, serialization, or concurrency.
4. Run CTest; run independent measurements for DSP changes and the external
   validator for host-contract changes. Use sanitizers for lifetime/buffer work.
5. Compare listening material at matched levels when changing sound. Record
   sample rate, block size, settings, reference version, and machine for results.
6. Update status.md and any changed contract/decision. Format C++ with the pinned
   clang-format. Never quietly change an existing parameter's meaning.

GitHub Actions contains a Linux release/sanitizer workflow. It has not been run
on GitHub until the repository is pushed; local equivalents are run separately.

## Audio-thread and state ownership

While active, the audio thread owns the engine, base parameters, and transient
modulation. `process` and `params.flush` are serialized by CLAP. Main-thread
queries read a coherent triple-buffered snapshot. Main-thread loads validate an
entire state before posting it through a bounded SPSC queue. A full queue returns
failure without partially applying the new state. The accepted state is visible
to main-thread queries immediately and reaches DSP at the next callback boundary.

Pending loads clear modulation and set smoothed targets. They do not import
filter histories. Activation/reset initializes filter state; resets also clear
modulation. Inactive loads apply immediately. Destruction occurs after processing
has stopped. Neither snapshot publication nor audio-side state consumption waits
for the main thread. The UI uses a separate gesture/event bridge; never write the engine from GUI
callbacks. See editor.md for queue backpressure, snapshots, and analyzer ownership.

## Install and Bitwig smoke test

```sh
bash tools/install-local.sh
```

This installs only OpenFilterEQ.clap under ~/.clap. Use `CLAP_INSTALL_DIR` for an
alternate directory. Bitwig should discover **OpenFilter EQ**. All bands start
disabled. Its CLAP remote pages expose Global controls and Band 01–24; enable a
band before adjusting it. The custom editor also creates enabled bands by
clicking empty graph space; see editor.md and ui-conventions.md. Restart the plugin process or Bitwig
when replacing an already loaded binary.

Use a new scratch Bitwig project for verification:

- Load the plugin, enable a bell, and change Frequency/Gain/Q while playing.
- Automate parameters and attach a Bitwig modulator; removing modulation should
  reveal the saved base value.
- Save/reopen, duplicate the device, and compare settings and audio exports.
- Exercise bypass, mono/stereo routing, sample-rate changes, and offline export.
- Confirm no scan errors, audio dropouts, or hanging/crashing plugin instances.

Record the Bitwig version and exact results. Synthetic-host or scanner success
alone must not be reported as a completed Bitwig listening/recall test.


## Compressor development

`plugins/compressor` builds independently as `OpenFilterCompressor.clap`. Its
engine has no CLAP/UI dependency. It reuses the proven parameter descriptors,
sample ramps, SPSC/triple buffers, stream utilities and native drawing materials.
Its own 100 Hz meter tap transfers peak history without FFTs or allocation on the
audio thread. See compressor-plan.md for permanent parameter IDs and schema 1.

CTest includes four compressor suites alongside the four EQ suites. Run
`tools/measure_compressor.py` and `tools/check-clap.sh
build/release/plugins/OpenFilterCompressor.clap` after relevant changes, as well
as the existing suite checks. `compressor_editor_preview` renders normal,
compact, 2x, menu, numeric-entry and Help views under reports/compressor-ui.
`compressor_render --benchmark` reports aggregate engine throughput. These tools
are not a substitute for the Bitwig checklist in compressor.md.

Use `bash tools/install-compressor-local.sh` to install only the compressor.
The original `tools/install-local.sh` remains the EQ-only installer.


## Limiter development

`plugins/limiter` builds separately as `OpenFilterLimiter.clap`; its engine has
no CLAP/UI dependency. It shares the tested ramps, parameter descriptors,
concurrency/stream utilities and native materials, with effect-specific predictive
limiting and meter history. See limiter-plan.md for the ceiling proof, stable
IDs and the original schema 1 contract. The 0.3 engine adds audio-path oversampling,
a Nyquist safety filter and efficient reconstructed-peak protection around the
dual-stage gain computer. Read limiter-dsp.md for sound/latency changes and the
schema 1 migration; current saved state remains schema 2.

Run `ctest --preset release`, `python tools/measure_limiter.py`, and
`bash tools/check-clap.sh build/release/plugins/OpenFilterLimiter.clap` after
relevant changes. The sanitizer preset includes all four limiter suites.
`limiter_editor_preview` writes normal, compact, 2x, menu, entry, Help and
collapsed-panel previews to `reports/limiter-ui`.
`limiter_render --benchmark` gives a short timing probe.
`limiter_benchmark 120 4 48000 256` measures 120 seconds of audio through four
instances, with automation, overload/queue stress and separate wall/thread CPU
statistics. A fifth argument `paced` runs at actual callback cadence and counts
late scheduler wakeups separately; `control` uses fixed arithmetic instead of
DSP, and `ftz` enables process-local SSE flush-to-zero for diagnosis on x86.
Run timing probes without competing builds/tests; a regular desktop
process cannot certify real-time DAW scheduling.
`python tools/measure_limiter_quality.py` adds transfer, high-rate convergence,
long reconstruction and external-meter gates. It requires the test-only system
package libebur128 1.2.6 (`libebur128-1` on Ubuntu); no plugin linkage is added.
`bash tools/install-limiter-local.sh` installs only the limiter artifact.


## Reverb development

`plugins/reverb` builds `OpenFilterReverb.clap` independently. Engine owns its
fixed delay/diffusion/filter storage and has no CLAP/UI dependency. Its adapter
reuses the proven queue/snapshot/state-stream model; the editor uses the shared
native painter, FFT tap, interaction helpers and approved R artwork. No new
runtime dependency is added. See reverb-plan.md and reverb.md.

Run CTest, `python tools/measure_reverb.py`, and
`bash tools/check-clap.sh build/release/plugins/OpenFilterReverb.clap`.
`reverb_editor_preview` writes normal, compact, 2x, menu, exact-entry, selected,
Help and all-band views to reports/reverb-ui. Also render the EQ's 24-band view
when changing shared branding/materials. `tools/install-reverb-local.sh`
installs only this project's reverb. Bitwig validation remains separate.

Reverb 0.2 sound qualification additionally runs `tools/measure_reverb_quality.py
--check` and `tools/qualify_reverb.py`. `reverb_benchmark rate block seconds
default|dense|automated [instances]` records thread CPU and callback wall times.
Run timing probes serially without other builds/tests. The matched listening
pack and the limits of these diagnostics are documented in reverb-quality.md.


## De-esser development

`plugins/deesser` builds independently as `OpenFilterDeesser.clap`. Its DSP
owns fixed audio/request/audition rings and composes the proven TPT filters;
there is no CLAP or UI dependency. The adapter follows the suite's bounded
snapshot/state/gesture queues. See deesser-plan.md for schema 1 and audio semantics.

Run CTest, `python tools/measure_deesser.py`, and
`bash tools/check-clap.sh build/release/plugins/OpenFilterDeesser.clap`.
`deesser_editor_preview` writes normal, compact, 2x, menu, entry and Help views
under reports/deesser-ui. Re-render EQ including the 24-band view after shared
branding changes. `deesser_render --benchmark` reports aggregate throughput.
`tools/install-deesser-local.sh` installs only the de-esser. Listening and Bitwig
qualification remain distinct from synthetic-host checks.


## Gate development

`plugins/gate` builds `OpenFilterGate.clap` with separate engine/editor targets.
See gate-plan.md for timing, hysteresis, filter, balance and state semantics.
Run CTest, `.venv/bin/python tools/measure_gate.py`, and
`bash tools/check-clap.sh build/release/plugins/OpenFilterGate.clap`.
The sanitizer preset includes its four suites. `gate_editor_preview` writes
normal, compact, 2x, menu, entry, sidechain-menu, collapsed and Help views under
`reports/gate-ui`. `gate_render --benchmark` measures aggregate throughput.
`tools/install-gate-local.sh` installs only the gate. Bitwig and musical listening
remain separate from synthetic-host validation.


## Saturator development

`plugins/saturator` builds a separate `OpenFilterSaturator.clap`. Its independent
engine composes the suite's sparse half-band resampler (extracted unchanged
from the limiter into `libs/dsp/HalfBand.hpp`), TPT filters and ramps. No new
runtime dependencies are added. See saturator-plan.md and saturator.md.

Run CTest, `.venv/bin/python tools/measure_saturator.py`, and
`bash tools/check-clap.sh build/release/plugins/OpenFilterSaturator.clap`.
The sanitizer preset includes four saturator suites. `saturator_editor_preview`
writes normal, compact, 2×, preset/style menu, entry, disabled and Help views.
Also inspect EQ's 24-band render after shared branding changes.
`saturator_render --benchmark` gives aggregate throughput. Run profiling without
competing tools; throughput does not certify callback deadlines or DAW capacity.
The render CLI accepts sample rate followed by parameter-ID/value pairs and
reads/writes interleaved stereo little-endian float64 audio on stdin/stdout.
`tools/install-saturator-local.sh` installs only the saturator.

`tools/saturator_listening.py` generates deterministic synthetic bass/chord/drum
material through dry and all four styles, RMS-matched for initial audition.
The WAVs and exact matching gains are saved in reports/saturator/listening.
This material supplements, and does not replace, real-stem listening qualification.


Saturator 0.1.1 also runs `tools/measure_saturator_quality.py`: 576 coherent-sine
cases, audible-band/full-band residue reported separately, plus independent
64×/128× convergence on a multitone/noise signal. `tools/investigate_saturator_adaa.py`
is an offline research comparison, not the shipping processor.
`tools/benchmark_saturator.py [older.clap]` measures the actual CLAP binary serially,
including dense mid-block automation, multiple instances, mono and exact silence.
It runs an additional 120-second automated soak with finite-output and C++
allocation guards. Do not run other builds/tests during timing probes. This
unpaced desktop host does not certify Bitwig scheduling or instance capacity.
`tools/saturator_benchmark rate block seconds mode instances` is the lower-level
engine-only probe; `mode` is default/dense/automated/silence/tail/mono/rounded/asymmetric.

Optional real-recording evaluation: `.venv/bin/python tools/saturator_program_material.py
/path/to/sqam.zip --baseline /path/to/older-saturator-render` reads a locally
downloaded EBU SQAM archive using system libsndfile. It checks old/new outputs
and creates 30 latency-aligned, RMS-matched WAVs plus a source/settings manifest
under `reports/saturator-cpu/program-material`. Read the EBU R&D-use terms before
obtaining the archive; neither the recordings nor these renders are shipping
assets. This numerical check and render preparation do not complete listening QA.


Saturator 0.2 adds `tools/measure_saturator_drive.py`: output-level/harmonic-growth
regressions, independent Auto-level audio reference, high-frequency alias matrix,
anti-phase stereo and quiet/loud/pause/restart checks. Run it after DSP changes.
The earlier measurement scripts explicitly select Auto level Off to retain their
0.1 reference equations. The program-material tool selects legacy gain when a
baseline renderer is supplied, and the corrected mode for normal audition packs.

Saturator 0.3 also requires `python tools/measure_saturator_character.py`.
`python tools/research_saturators.py --sources /path/to/SQAM/flacs` builds a
pinned offline Airwindows source harness and private R&D audition WAVs under
ignored reports; it does not install other plugins. See saturator-research.md.

Suite readout changes must exercise all seven actual editors with
`tests/ReadoutTest.hpp`: click/release entry, movement threshold, vertical drag,
Shift changes without jumps, undo/redo, descriptor reset and cancellation at
0.75×, 1× and 2× scale. Native GUI-host suites additionally record a footer
readout drag through a zero-capacity output queue, then accept its begin/value/end
across separate flushes. A pending click must not emit a host gesture.
