# OpenFilter

An open-source suite of CLAP audio effects with a parametric EQ, compressor, limiter, reverb, de-esser, gate/expander, and multiband saturator.
First target: Linux and Bitwig Studio. C++20 / CMake, with independent DSP and a
shared native CLAP integration foundation.

Next effect: multiband compressor.

Status: **EQ editor alpha (0.3.3)** and **Compressor alpha (0.1.2)**, **Limiter qualification candidate (0.3.0)**, **Reverb alpha (0.2.0)**, **De-Esser alpha (0.1.0)**, **Gate alpha (0.1.0)**, and **Saturator alpha (0.1.1)**.
The [compressor](docs/compressor.md) adds Peak/RMS detection, knee/range,
hold, auto release, stereo linking, internal/external sidechain with detector HP,
0–10 ms lookahead, parallel mix, gain-reduction history and transfer-curve editing.
It shares the EQ’s native UI materials, exact entry, A/B and undo workflows.
Build output: `build/release/plugins/OpenFilterCompressor.clap`. Install only the
compressor with `bash tools/install-compressor-local.sh`.

**Limiter:** A separate dual-stage limiter with Clean/Punch/Dense voicings,
adjustable lookahead, attack/release, separate transient/release linking, and
reconstructed-peak protection/meters. Its graph-first editor follows the Pro-L
workflow with suite materials. Old states migrate to Legacy mode. Latency is
now 914 samples at 48 kHz. Modern styles use 4× audio oversampling at normal
music rates; LUFS and dither remain optional future work.
See the [limiter guide](docs/limiter.md) before using it on delivery masters.
Build output: `build/release/plugins/OpenFilterLimiter.clap`. Install only the
limiter with `bash tools/install-limiter-local.sh`.

**Reverb alpha (0.2.0):** A separate algorithmic reverb with a Space-centered
editor, six-band decay shaping and six-band wet post EQ, original Modern /
Vintage / Plate voicings, predelay sync, ducking, gate, freeze and mix lock.
Adds dense input diffusion, energy-preserving modulation and smoother parameter
transitions; older saved sessions retain their original engine. See the
[audio investigation](docs/reverb-quality.md). This is a measured alpha; Bitwig listening and full Pro-R feature parity are pending.
See the [reverb guide](docs/reverb.md) and [audio contract](docs/reverb-plan.md).
Build output: `build/release/plugins/OpenFilterReverb.clap`. Install only the
reverb with `bash tools/install-reverb-local.sh`.

**De-Esser alpha (0.1.0):** Vocal/Allround detection, adjustable detection band,
threshold/range, wide-band and minimum-phase split-band processing, audition,
stereo link, external sidechain and 0–15 ms lookahead. Its graph-first native
editor uses suite controls, A/B, undo and exact entry. See the
[de-esser guide and qualification limits](docs/deesser.md).
Build output: `build/release/plugins/OpenFilterDeesser.clap`. Install only this
effect with `bash tools/install-deesser-local.sh`.

**Gate alpha (0.1.0):** A separate downward gate/expander with ratio/knee/range,
opening attack, closing release, hold/hysteresis, Peak/RMS detection, linked
stereo, filtered internal/external sidechain and audition. Adds 0–10 ms lookahead
with fixed 10 ms latency, parallel mix and wet/dry gain/balance. The native
editor follows the supplied Pro-G control arrangement using the suite's own
materials and G wordmark. See the [gate guide](docs/gate.md) and
[audio contract](docs/gate-plan.md). Bitwig and listening qualification are pending.
Build output: `build/release/plugins/OpenFilterGate.clap`. Install only this
effect with `bash tools/install-gate-local.sh`.

**Saturator alpha (0.1.1):** Three bands with phase-compensated crossovers,
four original saturation curves, up to 32× oversampling, per-band drive/mix,
linked dynamics, four tone filters, solo/mute and global drive compensation.
The spectrum-led editor follows Saturn's workflow with suite materials and the
approved S wordmark. See the [guide and qualification limits](docs/saturator.md)
and [audio contract](docs/saturator-plan.md). Bitwig, listening, and further real-time qualification remain pending; the optimized engine retains the
same filters and oversampling. See the [CPU and quality investigation](docs/saturator-quality.md).
Build output: `build/release/plugins/OpenFilterSaturator.clap`. Install only this
effect with `bash tools/install-saturator-local.sh`.

**EQ:** The plugin includes a resizable graphical
editor with draggable bands, exact value entry, a pre/post spectrum analyzer,
stereo meters, A/B comparison and undo/redo. Graphite surfaces, raised
dials, spacious contextual controls and separate meter scales define the shared UI.
This remains an alpha with measured limitations; see the project status below.

Implemented: 24 bands, bell/shelves/cuts/notch, 12/24/48 dB/oct and Brickwall cuts, per-band
stereo/L/R/M/S routing, mono and stereo buses, output gain, smoothed bypass and
automation, global CLAP modulation, and versioned state. All bands start disabled.

Build dependencies: C++20, pkg-config, Cairo and X11 development packages,
and Fontconfig development files for tests. Noto Sans is the preferred system font.
On Debian/Ubuntu: `libcairo2-dev libx11-dev libfontconfig1-dev fonts-noto-core`.

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
export PATH="$PWD/.venv/bin:$PATH"
cmake --preset release
cmake --build --preset release -j 4
ctest --preset release
python tools/measure_eq.py
bash tools/install-local.sh
```

Output: `build/release/plugins/OpenFilterEQ.clap`. Local install: `~/.clap/OpenFilterEQ.clap`.
In Bitwig, load **OpenFilter EQ**, open its editor, and click empty graph space
to create a band. Drag for frequency/gain; scroll for Q. Double-click value controls to reset;
click a numeric readout for exact entry. For an ultra-steep cut, select Low Cut
or High Cut and choose **Brickwall** from its slope menu. Restart the plugin
process or Bitwig after replacing an already loaded binary. See the
[editor guide](docs/editor.md) for controls and display conventions. The [development guide](docs/development.md) covers toolchain
requirements, validation, installation, and the Bitwig smoke test.

- [Limiter guide and validation boundaries](docs/limiter.md)
- [Limiter audio contract and plan](docs/limiter-plan.md)
- [Compressor guide and validation boundaries](docs/compressor.md)
- [Compressor audio contract and plan](docs/compressor-plan.md)
- [Suite UI and interaction conventions](docs/ui-conventions.md)
- [Standalone SVG wordmarks and native UI usage](assets/branding/README.md)
- [Development workflow](docs/development.md) and [agent rules](AGENTS.md)
- [Current status and measured limitations](docs/status.md)
- [Architecture and technology recommendation](docs/architecture.md)
- [EQ scope, milestones, and validation](docs/eq-plan.md)
- [Parameter, audio, and state contracts](docs/eq-contract.md)
- [Research and primary sources](docs/research.md)

Current code is [MIT licensed](LICENSE); dependency notices and research
attribution are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
