# Development contract

Read README.md, docs/development.md, docs/architecture.md, and the relevant plugin
plan before changing code. First target is Linux / Bitwig, CLAP only.

- Keep DSP independent of CLAP and UI. Share proven primitives, not speculative
  frameworks. Each plugin lives under plugins/<effect> and builds separately.
- Never allocate, lock, log, access files, or call GUI code on the audio thread.
  Event handling, state handoff, and coefficient updates obey the same rule.
- Keep parameter IDs, units, enumeration values, and plugin IDs stable. Document
  any state-schema or sound changes and add migration/regression coverage.
- Process host events at their sample offsets. Modulation never overwrites the
  stored base value. Don't silently make smoothing depend on block size.
- Test actual audio, not just coefficient-generation code. Use independent
  reference equations, partition-invariance tests, and host-level tests.
- Run CTest and the measurement script after DSP changes; run clap-validator
  after CLAP changes. State which checks could not run and why.
- UI changes must run ui_tests and gui_host_tests (use xvfb-run without DISPLAY).
  Render editor_preview and inspect normal, compact, 2x, menu and 24-band views.
  Preserve begin/value/end gestures under host backpressure. Read docs/editor.md
  and docs/ui-conventions.md. Across all plugins, double-click resets a value to
  its descriptor default; never use it to open numeric entry.
- Target professional audio quality and clear workflows. Never describe a
  baseline filter as analog-matched without measurements. Use original code
  and graphics; retain dependency and research attribution.
- Update docs/status.md each milestone. Separate implemented, tested, and pending
  work. Do not mark Bitwig validation complete based on a synthetic test host.
- Pin dependencies to immutable revisions. Do not commit build outputs or tools.
- Build/install only this project's CLAP artifact. Do not modify other plugins
  or existing DAW projects when testing.
