# Development workflow

## Scope and source organization

The current milestone is a native CLAP EQ alpha with a custom Linux editor.
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
