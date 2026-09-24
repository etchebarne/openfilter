# Third-party notices

## Wordmark typography

The OpenFilter wordmark artwork derives from Unbounded (weight 600), with custom
letter details and spacing. Copyright 2022 The Unbounded Project Authors
(https://github.com/googlefonts/unbounded), SIL Open Font License 1.1.
The full font notice is retained in `assets/branding/Unbounded-OFL.txt`.
Only outlined logo artwork is included; no font software is bundled.

CLAP headers (a47f6badb49d948fd009998f28309cdab78979c9) and clap-helpers
(55a5dd5d1db9c87b32f407e387f64676d27e10b1) both use the following license:

MIT License

Copyright (c) 2021 Alexandre BIQUE

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## DSP references

The independently implemented trapezoidal state-variable filter and output-mixing
formulae follow Andrew Simper / Cytomic's published technical papers:
https://cytomic.com/technical-papers/
The author publishes this knowledge in the public domain.

The independent measurement reference follows Robert Bristow-Johnson's Audio EQ
Cookbook equations: https://www.w3.org/TR/audio-eq-cookbook/
See docs/research.md for the full research bibliography.

## Development-only tools

clap-validator revision b2f1d9b79b1d264a5747f46707d72b1aa40a02ef is built
separately under build/tools and is MIT licensed. Its notices remain in its source
tree. Python/CMake/Ninja/clang-format dependencies are development tools installed
in .venv and are not bundled into the CLAP binary. No JUCE dependency is included.

## Pugl (native window support)

Revision b7637149ebe53124e5be90559e02a0185bbcbd73, statically linked.
https://github.com/lv2/pugl

Copyright 2011-2026 David Robillard <d@drobilla.net>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

## System graphics libraries

Cairo is dynamically linked from the system (LGPL-2.1-only OR MPL-1.1;
https://www.cairographics.org/). Xlib is dynamically linked under its MIT/X11
terms (https://www.x.org/). These libraries and fonts are not vendored here;
retain their distribution notices when packaging them. Noto Sans is selected
through the system font configuration, with fallback if unavailable.

## Elliptic cut prototype

The normalized mathematical pole/zero data in Brickwall.hpp is generated with
SciPy's ellipap (development tool, BSD-3-Clause). No SciPy implementation code
is included in the plugin. tools/design_brickwall.py reproduces the values;
docs/research.md links the design and independent measurement references.

## Compressor research

The original feed-forward compressor uses the standard quadratic soft-knee
characteristic and dB envelope concepts discussed in Giannoulis, Massberg and
Reiss, "Digital Dynamic Range Compressor Design—A Tutorial and Analysis",
JAES 60(6), 2012. Research reference:
https://eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf
No third-party compressor source code is included. FabFilter Pro-C documentation
informed workflow research only; no FabFilter code, graphics or algorithms were
copied. See docs/compressor-plan.md for the reference links.

## Limiter research

OpenFilter's limiter and fractional-delay detector are original implementations.
Research references include ITU-R BS.1770-5 Annex 2 (peak estimation), the
Giannoulis/Massberg/Reiss envelope tutorial cited above, and FabFilter Pro-L 2
user documentation (workflow and control semantics only). No third-party
limiter implementation, vendor coefficients or artwork is included.
See docs/limiter-dsp.md for source links and qualification boundaries.

Limiter 0.3's half-band resamplers and safety filters are original windowed-sinc
implementations. Development-only cross-checks call system libebur128 1.2.6
(MIT, https://github.com/jiixyj/libebur128/tree/v1.2.6); that library is neither
bundled nor linked into the plugin. SciPy supplies independent FIR/convolution
references in measurement scripts.

## Reverb research attribution

The original Refined reverb implementation is informed by Julius O. Smith's
Physical Audio Signal Processing (delay interpolation), Schlecht and Habets'
work on time-varying unitary feedback matrices and scattering, Fagerström et al.
on velvet-noise FDNs, and Dal Santo et al. on FDN coloration/density optimization.
No source code from these publications was incorporated. Full paper links,
implementation distinctions, rejected experiments and measurements are retained
in docs/reverb-quality.md. FabFilter documentation is a workflow/quality
reference; no FabFilter code, audio or graphics is included.
