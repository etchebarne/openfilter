#!/usr/bin/env python3
"""Independent actual-audio reference and analytic transfer measurements.

Reference uses NumPy/SciPy filters and independent piecewise static equations;
no C++ coefficients, helpers or results are used to compute expected samples.
"""
import json
import math
import pathlib
import subprocess
import time
import numpy as np
from scipy.signal import lfilter

ROOT = pathlib.Path(__file__).resolve().parents[1]
RENDER = ROOT / 'build/release/gate_render'
DEFAULT = [0, 0, -36, 4, 1, 150, 6, 60, 0, 100, 0, 0, 25, 0, 100, 0, 3, 0,
           20000, 0, 0, 0, 0]


def render(rate, signal, settings, seconds=1.):
    args = [str(RENDER), str(rate), str(round(rate * seconds)), signal]
    for key, value in settings.items():
        args.extend([str(key), str(value)])
    return np.frombuffer(subprocess.check_output(args), dtype='<f8').reshape(-1, 7)


def reference(data, rate, settings):
    p = DEFAULT.copy()
    for i, value in settings.items():
        p[i] = value
    latency = math.ceil(rate / 100)
    audio = np.pad(data[:, :2], ((latency, 0), (0, 0)))[:len(data)]
    source = data[:, 2:4] if p[17] else data[:, :2] * 10 ** (p[10] / 20)
    if p[13]:
        a = math.exp(-2 * math.pi * min(p[13], rate * .45) / rate)
        source = lfilter([a, -a], [1, -a], source, axis=0)
    if p[18] < 20000:
        a = math.exp(-2 * math.pi * min(p[18], rate * .45) / rate)
        source = lfilter([1-a], [1, -a], source, axis=0)
    listen = np.pad(source, ((latency, 0), (0, 0)))[:len(data)]
    positions = np.arange(len(data)) - (latency - p[11] * rate / 1000)
    source = np.column_stack([np.interp(positions, np.arange(-1, len(data)), np.r_[0., source[:, c]], left=0) for c in range(2)])
    decay = math.exp(-200 / rate)
    if p[15]:
        level = np.sqrt(lfilter([1-decay], [1, -decay], source**2, axis=0))
    else:
        level = np.empty_like(source)
        previous = np.zeros(2)
        for n in range(len(source)):
            previous = np.maximum(abs(source[n]), previous * decay)
            level[n] = previous
    level = level * (1 - p[14]/100) + level.max(axis=1)[:, None] * p[14]/100
    level = 20 * np.log10(np.maximum(1e-15, level))
    x = p[2] - level
    attenuation = np.maximum(x, 0) * (p[3] - 1)
    if p[6]:
        mask = abs(x) < p[6] / 2
        attenuation[mask] = (p[3]-1) * (x[mask] + p[6]/2)**2 / (2*p[6])
    attenuation = np.minimum(attenuation, p[7])
    envelope = np.zeros_like(attenuation)
    opening = 0 if p[4] == 0 else math.exp(-1000 / (rate*p[4]))
    closing = math.exp(-1000 / (rate*p[5]))
    for c in range(2):
        g = min(p[7], (p[3]-1) * (p[2] + 300))
        remaining = 0
        armed = False
        for n, target in enumerate(attenuation[:, c]):
            if level[n, c] >= p[2] + p[6]/2 - (p[16] if armed else 0):
                armed = True
                remaining = int(p[12]*rate/1000)
            elif remaining > 0:
                remaining -= 1
            else:
                armed = False
            if armed:
                target = 0.
            coefficient = opening if target < g else closing
            g = coefficient*g + (1-coefficient)*target
            envelope[n, c] = g
    wet_balance = 1 - np.maximum(0, np.array([p[20], -p[20]]) / 100)
    dry_balance = 1 - np.maximum(0, np.array([p[21], -p[21]]) / 100)
    wet = audio * 10**((p[10]+p[8]-envelope)/20) * wet_balance
    dry = audio * 10**((p[10]+p[22])/20) * dry_balance
    mixed = wet * p[9]/100 + dry * (1-p[9]/100)
    selected = (mixed*(1-p[19]) + listen*p[19]) * 10**(p[1]/20)
    return selected*(1-p[0]) + audio*p[0]


def main():
    started = time.time()
    cases = []
    for rate in [44100, 48000, 88200, 96000, 176400, 192000]:
        for detector in [0, 1]:
            settings = {15: detector, 2: -25, 4: 3, 5: 65, 6: 12, 11: 3.17, 14: 47, 12: 35, 16: 4}
            data = render(rate, 'step', settings, .85)
            error = float(np.max(abs(data[:, 4:6]-reference(data, rate, settings))))
            assert error < 2e-10, (rate, detector, error)
            cases.append(dict(rate=rate, detector=detector, signal='step', residual=error))
    settings_cases = [{17: 1, 13: 120, 18: 3500}, {16: 12, 12: 80, 11: 10},
                      {7: 3, 9: 37, 8: 6, 22: -3, 1: -3, 10: 4, 20: -75, 21: 50},
                      {3: 1}, {0: 1}, {15: 1, 13: 1000, 17: 1},
                      {6: 0, 4: 0, 5: 1, 12: 0, 16: 0}, {14: 0, 11: 10},
                      {19: 1, 17: 1, 13: 150, 18: 2500}, {9: 0}, {7: 0}]
    for settings in settings_cases:
        data = render(48000, 'sine', settings, .7)
        error = float(np.max(abs(data[:, 4:6]-reference(data, 48000, settings))))
        assert error < 2e-10, (settings, error)
        cases.append(dict(rate=48000, settings=settings, signal='sine', residual=error))
    static_error = 0
    static_count = 0
    # External DC has known level. Sweep threshold around it to enter both knee halves.
    for knee in [0, 6, 30]:
        for threshold in [-30, -12, -6, -3, 0]:
            for ratio in [1, 2, 4, 100]:
                settings = {17: 1, 2: threshold, 3: ratio, 6: knee, 4: 0, 5: 1, 12: 0, 16: 0}
                data = render(48000, 'dc', settings, .2)
                distance = threshold - 20*np.log10(.8)
                if knee and abs(distance) < knee/2:
                    attenuation = (ratio-1)*(distance+knee/2)**2/(2*knee)
                else:
                    attenuation = (ratio-1)*max(0, distance)
                expected = -min(60, attenuation)
                measured = 20*np.log10(data[-1, 4]/.5)
                error = abs(measured-expected)
                static_error = max(static_error, error)
                assert error < 1e-9, (settings, measured, expected)
                static_count += 1
    # An open gate must preserve the audio; report residual after settling.
    transparent = render(48000, 'sine', {2: -60}, 2.)
    error = float(np.max(abs(transparent[-48000:, 4:6] - transparent[-48480:-480, :2])))
    assert error < 1e-12
    # Diagnostics for an intentionally continuously expanding steady sine.
    # Fast opening/closing may distort bass; these are observations, not parity claims.
    distortion = {}
    for signal, frequency in [('sine', 997), ('bass', 55)]:
        for label, attack, release in [('normal', 1, 150), ('fast', 0, 1)]:
            samples = render(48000, signal, {2: -3, 4: attack, 5: release, 12: 0, 16: 0, 14: 0}, 3.)[-48000:, 4]
            bins = np.fft.rfft(samples)
            fundamental = abs(bins[frequency])**2
            other = max(1e-30, np.sum(abs(bins[1:])**2) - fundamental)
            distortion[f'{frequency}hz_{label}_thdn_db'] = float(10*np.log10(other/fundamental))
    result = dict(reference_cases=len(cases), static_cases=static_count,
                  max_audio_residual=max(c['residual'] for c in cases),
                  max_static_error_db=static_error, open_gate_residual=error,
                  expanding_sine_diagnostics=distortion, cases=cases, seconds=time.time()-started)
    report = ROOT/'reports/gate-measurements.json'
    report.parent.mkdir(exist_ok=True)
    report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k != 'cases'}, indent=2))

if __name__ == '__main__':
    main()
