#!/usr/bin/env python3
"""Independent NumPy/SciPy reference measurements of actual C++ output.

No plugin coefficient/transfer helper is imported. Static curves are piecewise
quadratics, HP/RMS are scipy.signal.lfilter, and the gain envelope is integrated
independently in dB. Binary render output also contains original stimulus.
"""
import json
import math
import pathlib
import subprocess
import time
import numpy as np
from scipy.signal import lfilter

ROOT = pathlib.Path(__file__).resolve().parents[1]
RENDER = ROOT / "build/release/compressor_render"
DEFAULT = [0, 0, -18, 4, 10, 150, 6, 60, 0, 100, 0, 0, 0, 0, 100, 0, 0, 0]


def render(rate, signal, settings, seconds=1.):
    args = [str(RENDER), str(rate), str(round(rate * seconds)), signal]
    for key, value in settings.items():
        args.extend([str(key), str(value)])
    return np.frombuffer(subprocess.check_output(args), dtype='<f8').reshape(-1, 7)


def reference(data, rate, settings):
    p = DEFAULT.copy()
    for i, v in settings.items():
        p[i] = v
    latency = math.ceil(rate / 100)
    audio = np.pad(data[:, :2], ((latency, 0), (0, 0)))[:len(data)]
    source = data[:, 2:4] if p[17] else data[:, :2] * 10 ** (p[10] / 20)
    if p[13]:
        a = math.exp(-2 * math.pi * p[13] / rate)
        source = lfilter([a, -a], [1, -a], source, axis=0)
    delay = latency - p[11] * rate / 1000
    positions = np.arange(len(data)) - delay
    source = np.column_stack([np.interp(positions, np.arange(len(data)), source[:, c], left=0) for c in range(2)])
    d = math.exp(-100 / rate)
    if p[15]:
        level = np.sqrt(lfilter([1-d], [1, -d], source**2, axis=0))
    else:
        level = np.empty_like(source)
        last = np.zeros(2)
        for n in range(len(source)):
            last = np.maximum(np.abs(source[n]), last*d)
            level[n] = last
    x = 20 * np.log10(np.maximum(1e-15, level)) - p[2]
    attenuation = np.maximum(x, 0) * (1 - 1 / p[3])
    if p[6]:
        mask = np.abs(x) < p[6] / 2
        attenuation[mask] = (x[mask] + p[6] / 2)**2 * (1 - 1/p[3]) / (2*p[6])
    attenuation = np.minimum(attenuation, p[7])
    attenuation = attenuation * (1 - p[14]/100) + np.max(attenuation, axis=1)[:, None] * p[14]/100
    envelope = np.zeros_like(attenuation)
    a = math.exp(-1000/(rate*p[4]))
    slow_decay = math.exp(-4/rate)
    for c in range(2):
        g = slow = 0.
        hold = 0
        for n, target in enumerate(attenuation[:, c]):
            slow = slow_decay * slow + (1 - slow_decay) * g
            if target >= g:
                g = a*g + (1-a)*target
                hold = int(p[12]*rate/1000)
            elif hold:
                hold -= 1
            else:
                r = math.exp(-1000/(rate*p[5]*(1+p[16]*3*min(1., slow/12))))
                g = r*g + (1-r)*target
            envelope[n, c] = g
    wet = audio * 10 ** ((p[10]+p[8]-envelope)/20)
    mixed = (audio*10**(p[10]/20)*(1-p[9]/100)+wet*p[9]/100)*10**(p[1]/20)
    return mixed*(1-p[0])+audio*p[0]


def main():
    started = time.time()
    cases = []
    for rate in [44100, 48000, 88200, 96000, 176400, 192000]:
        for detector in [0, 1]:
            settings = {15: detector, 4: 3, 5: 65, 6: 12, 11: 3.17, 14: 47}
            data = render(rate, 'step', settings, .85)
            expected = reference(data, rate, settings)
            error = float(np.max(np.abs(data[:, 4:6]-expected)))
            assert error < 2e-10, (rate, detector, error)
            cases.append(dict(rate=rate, detector=detector, signal='step', residual=error))
    for settings in [{17: 1, 13: 120}, {16: 1, 12: 80, 11: 10}, {7: 3, 9: 37, 8: 6, 1: -3, 10: 4}, {3: 1}, {0: 1}, {15: 1, 13: 1000, 17: 1}, {6: 0, 4: .1, 5: 10}, {14: 0, 11: 10}]:
        data = render(48000, 'sine', settings, .7)
        error = float(np.max(np.abs(data[:, 4:6]-reference(data, 48000, settings))))
        assert error < 2e-10, (settings, error)
        cases.append(dict(rate=48000, settings=settings, signal='sine', residual=error))
    static_error = 0
    for knee in [0, 6, 36]:
        for threshold in [-36, -9, -6, -3, 0]:
            for ratio in [1, 2, 4, 20]:
                # Constant full-scale sidechain -> known input level of 20 log10(.8).
                settings = {17: 1, 2: threshold, 3: ratio, 6: knee, 4: .1}
                data = render(48000, 'dc', settings, .2)
                x = 20*np.log10(.8)-threshold
                gain_db = -(1-1/ratio)*(max(0, x) if knee==0 or abs(x)>=knee/2 else (x+knee/2)**2/(2*knee))
                measured = 20*np.log10(data[-1, 4]/.5)
                error = abs(measured-gain_db)
                static_error = max(static_error, error)
                assert error < 1e-9
    # Coherent 997 Hz sine. Record THD+N for stated settings, not a blanket quality claim.
    data = render(48000, 'sine', {14: 0}, 2.)
    segment = data[-48000:, 4]
    spectrum = np.fft.rfft(segment)
    fundamental = abs(spectrum[997])**2
    residual = max(0., float(np.sum(abs(spectrum[1:])**2)-fundamental))
    thdn = 10*np.log10(max(1e-30, residual/fundamental))
    assert thdn < -60, thdn
    bass_thdn = {}
    for name, settings in [('default_times', {14: 0}), ('fast_times', {14: 0, 4: .1, 5: 10})]:
        bass = render(48000, 'bass', settings, 2.)[-48000:, 4]
        bins = np.fft.rfft(bass)
        fundamental_power = abs(bins[55])**2
        other_power = max(0., float(np.sum(abs(bins[1:])**2)-fundamental_power))
        bass_thdn[name] = float(10*np.log10(max(1e-30, other_power/fundamental_power)))
    result = dict(bass_55hz_thdn_db=bass_thdn, reference_cases=len(cases), static_cases=60, max_audio_residual=max(c['residual'] for c in cases), max_static_error_db=static_error, default_997hz_thdn_db=float(thdn), cases=cases, seconds=time.time()-started)
    report = ROOT/'reports/compressor-measurements.json'
    report.parent.mkdir(exist_ok=True)
    report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='cases'}, indent=2))

if __name__ == '__main__':
    main()
