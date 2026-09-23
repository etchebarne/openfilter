# OpenFilter

An open-source suite of CLAP audio effects with a parametric EQ and compressor.
First target: Linux and Bitwig Studio. C++20 / CMake, with independent DSP and a
shared native CLAP integration foundation.

Next effects: limiter, reverb, multiband compressor, de-esser,
gate/expander, and saturator/distortion.

Status: **EQ editor alpha (0.3.3)** and **Compressor alpha (0.1.2)**.
The [compressor](docs/compressor.md) adds Peak/RMS detection, knee/range,
hold, auto release, stereo linking, internal/external sidechain with detector HP,
0–10 ms lookahead, parallel mix, gain-reduction history and transfer-curve editing.
It shares the EQ’s native UI materials, exact entry, A/B and undo workflows.
Build output: `build/release/plugins/OpenFilterCompressor.clap`. Install only the
compressor with `bash tools/install-compressor-local.sh`.

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

- [Compressor guide and validation boundaries](docs/compressor.md)
- [Compressor audio contract and plan](docs/compressor-plan.md)
- [Suite UI and interaction conventions](docs/ui-conventions.md)
- [Development workflow](docs/development.md) and [agent rules](AGENTS.md)
- [Current status and measured limitations](docs/status.md)
- [Architecture and technology recommendation](docs/architecture.md)
- [EQ scope, milestones, and validation](docs/eq-plan.md)
- [Parameter, audio, and state contracts](docs/eq-contract.md)
- [Research and primary sources](docs/research.md)

Current code is [MIT licensed](LICENSE); dependency notices and research
attribution are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
