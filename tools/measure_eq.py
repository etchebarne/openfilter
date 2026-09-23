#!/usr/bin/env python3
"""Compare rendered C++ audio with independent RBJ/SciPy reference filters.

The FFT comparison uses identical finite impulse windows on both sides. It tests
implementation agreement, not infinite-tail frequency resolution or analog fit.
"""
import argparse
import json
import math
import pathlib
import subprocess

import numpy as np
from scipy import signal


def cookbook(shape, rate, hz, gain, q):
    w = 2 * np.pi * min(hz, rate * 0.475) / rate
    c, s = np.cos(w), np.sin(w)
    a = 10 ** (gain / 40)
    alpha = s / (2 * q)
    if shape == 0:
        b, d = [1 + alpha * a, -2 * c, 1 - alpha * a], [1 + alpha / a, -2 * c, 1 - alpha / a]
    elif shape == 1:
        t = 2 * math.sqrt(a) * alpha
        b = [a * ((a + 1) - (a - 1) * c + t), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - t)]
        d = [(a + 1) + (a - 1) * c + t, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - t]
    elif shape == 2:
        t = 2 * math.sqrt(a) * alpha
        b = [a * ((a + 1) + (a - 1) * c + t), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - t)]
        d = [(a + 1) - (a - 1) * c + t, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - t]
    elif shape == 3:
        b, d = [(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    elif shape == 4:
        b, d = [(1 - c) / 2, 1 - c, (1 - c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    else:
        b, d = [1, -2 * c, 1], [1 + alpha, -2 * c, 1 - alpha]
    return np.array(b + d) / d[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--renderer", type=pathlib.Path, default=pathlib.Path("build/release/eq_render"))
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("reports/eq-measurements.json"))
    args = parser.parse_args()
    impulse = np.zeros(8192)
    impulse[0] = 1
    cases = []
    for rate in [44100, 48000, 88200, 96000, 176400, 192000]:
        for shape in range(6):
            for hz in [40, 1000, 18000]:
                for q in [0.1, math.sqrt(0.5), 4, 40]:
                    for slope in ([0, 1, 2] if shape in [3, 4] else [0]):
                        gain = 12 if q < 1 else -12
                        raw = subprocess.check_output([str(args.renderer.resolve()), str(rate), str(shape), str(hz), str(gain), str(q), str(slope), str(len(impulse))])
                        actual = np.frombuffer(raw, dtype=np.float64)
                        stages = 2 ** slope if shape in [3, 4] else 1
                        sections = []
                        for stage in range(stages):
                            section_q = q
                            if stages > 1:
                                section_q *= math.sqrt(2) / (2 * math.cos(math.pi * (2 * stage + 1) / (4 * stages)))
                            sections.append(cookbook(shape, rate, hz, gain, min(40, max(0.1, section_q))))
                        expected = signal.sosfilt(sections, impulse)
                        assert actual.shape == expected.shape and np.isfinite(actual).all()
                        residual = float(np.max(np.abs(actual - expected)) / max(1.0, np.max(np.abs(expected))))
                        a_fft, e_fft = np.abs(np.fft.rfft(actual)), np.abs(np.fft.rfft(expected))
                        use = e_fft > 0.001
                        error = float(np.max(np.abs(20 * np.log10(a_fft[use] / e_fft[use]))))
                        assert residual < 1e-8, (rate, shape, hz, q, slope, residual)
                        assert error < 0.01, (rate, shape, hz, q, slope, error)
                        cases.append(dict(rate=rate, shape=shape, hz=hz, gain=gain, q=q, slope=slope, normalized_peak_residual=residual, windowed_response_error_db=error))

    # Independent SOS implementation of the new elliptic mode. Compare identical
    # finite impulse windows even when a low-frequency tail has not fully decayed.
    brickwall = []
    long_impulse = np.zeros(65536)
    long_impulse[0] = 1
    for rate in [44100, 48000, 88200, 96000, 176400, 192000]:
        for shape in [3, 4]:
            for hz in [10, 146.9, 1000, 18000, 30000]:
                cutoff = min(hz, .475 * rate)
                sos = signal.ellip(20, .01, 100, cutoff, fs=rate,
                                   btype="highpass" if shape == 3 else "lowpass", output="sos")
                expected = signal.sosfilt(sos, long_impulse)
                for q in [.1, 40.]:
                    raw = subprocess.check_output([str(args.renderer.resolve()), str(rate), str(shape), str(hz), "0", str(q), "3", str(len(long_impulse))])
                    actual = np.frombuffer(raw, dtype=np.float64)
                    residual = float(np.max(np.abs(actual - expected)))
                    assert np.isfinite(actual).all() and residual < 1e-8, (rate, shape, hz, q, residual)
                    brickwall.append(dict(rate=rate, shape=shape, frequency=hz, q=q, peak_residual=residual))
    # Reference magnitude specification in the prewarped analog frequency domain.
    # The C++ samples above verify this reference against actual processing.
    pass_w = np.linspace(.0001, 1, 100001)
    stop_w = np.geomspace(1.017, 1e5, 100001)
    z, p, k = signal.ellipap(20, .01, 100)
    pass_db = 20*np.log10(np.abs(signal.freqs_zpk(z, p, k, pass_w)[1]))
    stop_db = 20*np.log10(np.abs(signal.freqs_zpk(z, p, k, stop_w)[1]))
    assert pass_db.min() >= -.010001 and pass_db.max() <= .000001
    assert stop_db.max() <= -99.999
    brickwall_report = dict(cases=len(brickwall), max_peak_residual=max(c["peak_residual"] for c in brickwall),
        passband_db=[float(pass_db.min()), float(pass_db.max())], stopband_max_db=float(stop_db.max()),
        stopband_start_ratio=1.017, results=brickwall)

    # Explicitly characterize the baseline's analog-matching limitation.
    frequencies = np.geomspace(1000, 21000, 200)
    sos = cookbook(0, 48000, 15000, 12, 1)
    _, digital = signal.sosfreqz([sos], worN=frequencies, fs=48000)
    s = 1j * frequencies / 15000
    a = 10 ** (12 / 40)
    analog = (s*s + a*s + 1) / (s*s + s/a + 1)
    gap = np.abs(20 * np.log10(np.abs(digital) / np.abs(analog)))
    report = dict(
        description="8192-sample C++ SVF impulse vs independent RBJ equations and SciPy SOS filtering",
        cases=len(cases), max_normalized_peak_residual=max(c["normalized_peak_residual"] for c in cases),
        max_windowed_response_error_db=max(c["windowed_response_error_db"] for c in cases),
        analog_fit_example=dict(rate=48000, frequency=15000, gain_db=12, q=1, measured_band_hz=[1000,21000], max_deviation_db=float(gap.max()), note="Baseline warping is expected; this is not an analog-matched filter."),
        results=cases, brickwall=brickwall_report,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    summary = {k: v for k, v in report.items() if k not in ("results", "brickwall")}
    summary["brickwall"] = {k: v for k, v in brickwall_report.items() if k != "results"}
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
