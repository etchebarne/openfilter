#!/usr/bin/env python3
"""Reproduce the fixed, normalized analog prototype used in Brickwall.hpp.

SciPy is a development tool only. Runtime cutoff changes use a bilinear mapping
of these poles and zeros, with no filter-design solver on the audio thread.
"""
import numpy as np
from scipy import signal

z, p, gain = signal.ellipap(20, .01, 100)
poles = sorted((v for v in p if v.imag > 0), key=abs)
zeros = sorted((abs(v) for v in z if v.imag > 0), reverse=True)
for pole, zero in zip(poles, zeros):
    print(f"    {{{abs(pole):.17g}, {-2*pole.real/abs(pole):.17g}, {(abs(pole)/zero)**2:.17g}}},")
print(f"Passband normalization: {10**(-.01/20):.17g}")
w = np.linspace(1, 1.1, 100001)
_, h = signal.freqs_zpk(z, p, gain, w)
print(f"First -100 dB: {w[np.flatnonzero(abs(h) <= 1e-5)[0]]:.6f} * cutoff")
