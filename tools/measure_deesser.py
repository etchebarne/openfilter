#!/usr/bin/env python3
"""Measure actual rendered audio against independent SciPy/RBJ equations.

No C++ coefficient helpers are imported. Reports are diagnostics, not a claim
of perceptual parity or completed Bitwig qualification.
"""
from pathlib import Path
import json
import math
import subprocess
import numpy as np
from scipy.signal import butter, sosfilt, lfilter, freqz

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'build/release/deesser_render'
DEFAULT = [0, 0, -30, 6, 6000, 14000, 5, 100, 0, 1, 0, 0, .5, 80, 0]


def render(rate, source, settings):
    args = [str(RENDER), str(rate)]
    for i, v in settings.items():
        args += [str(i), str(v)]
    output = subprocess.check_output(args, input=np.asarray(source, dtype='<f8').tobytes())
    return np.frombuffer(output, dtype='<f8').reshape(-1, 8)


def reference(rate, source, settings):
    p = DEFAULT.copy()
    for i, v in settings.items():
        p[i] = v
    count = len(source)
    latency = math.ceil(rate * .015)
    delay = lambda x: np.pad(x, ((latency, 0), (0, 0)))[:count]
    program = source[:, :2] * 10**(p[14]/20)
    x = source[:, 2:4] if p[11] else program
    lo = np.clip(min(p[4:6]), 1, rate*.475-10)
    hi = np.clip(max(p[4:6]), lo+10, rate*.475)
    band = sosfilt(butter(2, lo, btype='highpass', fs=rate, output='sos'), x, axis=0)
    band = sosfilt(butter(2, hi, btype='lowpass', fs=rate, output='sos'), band, axis=0)
    a = math.exp(-1000/rate)
    power = lfilter([1-a], [1, -a], band**2, axis=0)
    broad = lfilter([1-a], [1, -a], x**2, axis=0)
    over = 10*np.log10(np.maximum(1e-30, power))-p[2]
    knee = np.where(over <= -3, 0, np.where(over < 3, (over+3)**2/12, over))
    balance = 10*np.log10(np.maximum(1e-30, power)/np.maximum(1e-30, broad))
    t = np.clip((balance+18)/12, 0, 1)
    request = np.minimum(p[3], .875*knee)*(p[8]+(1-p[8])*t*t*(3-2*t))
    at = np.arange(count)-(latency-p[6]*rate/1000)
    request = np.column_stack([np.interp(at, np.arange(count), request[:, c], left=0) for c in range(2)])
    request = request*(1-p[7]/100)+np.max(request, axis=1)[:, None]*p[7]/100
    attack, release = math.exp(-1000/(rate*p[12])), math.exp(-1000/(rate*p[13]))
    envelope = np.zeros_like(request)
    for c in range(2):
        last = 0
        for n, target in enumerate(request[:, c]):
            coeff = attack if target > last else release
            last = coeff*last+(1-coeff)*target
            envelope[n, c] = min(p[3], last)
    # Full independent transient reference is wide band / audition / bypass.
    wet = delay(band) if p[10] else delay(program)*10**(-envelope/20)
    expected = delay(source[:, :2]) if p[0] else wet*10**(p[1]/20)
    return expected, np.max(envelope, axis=1)


def rbj_high_shelf(rate, frequency, cut, probe):
    a = 10**(cut/40)
    w = 2*np.pi*frequency/rate
    c, sn = np.cos(w), np.sin(w)
    alpha = sn/np.sqrt(2)
    b = a*np.array([(a+1)+(a-1)*c+2*np.sqrt(a)*alpha,
                     -2*((a-1)+(a+1)*c), (a+1)+(a-1)*c-2*np.sqrt(a)*alpha])
    d = np.array([(a+1)-(a-1)*c+2*np.sqrt(a)*alpha,
                  2*((a-1)-(a+1)*c), (a+1)-(a-1)*c-2*np.sqrt(a)*alpha])
    return abs(freqz(b, d, worN=[2*np.pi*probe/rate])[1][0])


def main():
    cases = []
    rng = np.random.default_rng(91)
    for rate in [22050, 44100, 48000, 88200, 96000, 192000]:
        n = round(rate*.38)
        t = np.arange(n)/rate
        burst = (t>.08)&(t<.23)
        x = .13*np.sin(2*np.pi*220*t)+burst*.5*rng.uniform(-1, 1, n)
        sc = .6*np.sin(2*np.pi*min(8000, rate*.3)*t)*burst
        source = np.column_stack([x, -.27*x, sc, sc*.2])
        for settings in [{9: 0, 6: 3.17, 7: 37}, {9: 0, 8: 1, 11: 1, 6: 15, 12: 2, 13: 40},
                         {10: 1, 11: 1}, {0: 1, 14: 12, 1: -6}]:
            data = render(rate, source, settings)
            expected, gr = reference(rate, source, settings)
            residual = float(np.max(abs(data[:, 4:6]-expected)))
            assert residual < 2e-10, (rate, settings, residual)
            if settings.get(9) == 0:
                assert np.max(abs(data[:, 6]-gr)) < 2e-9
            cases.append(dict(rate=rate, settings=settings, max_audio_error=residual))
    # Latency-aligned exact unity in both modes, including anti-phase stereo.
    source = rng.uniform(-.5, .5, (16000, 4))
    for mode in [0, 1]:
        result = render(48000, source, {3: 0, 9: mode})[:, 4:6]
        expected = np.pad(source[:, :2], ((720, 0), (0, 0)))[:len(source)]
        assert np.max(abs(result-expected)) < 1e-14
    # Independent steady-state shelf magnitude: low fundamentals survive HF cuts.
    shelf_cases = []
    rate = 48000
    t = np.arange(rate)/rate
    for cutoff in [3000, 6000, 10000]:
        for frequency in [220, 1000, cutoff, 16000, 20000]:
            x = .2*np.sin(2*np.pi*frequency*t)
            sc = .8*np.sin(2*np.pi*min(18000, cutoff*1.5)*t)
            source = np.column_stack([x, x, sc, sc])
            data = render(rate, source, {2: -60, 3: 9, 4: cutoff, 5: 21000, 8: 1, 9: 1, 11: 1})
            segment = data[-24000:, 4]
            measured = np.sqrt(np.mean(segment**2)) / (.2/np.sqrt(2))
            expected = rbj_high_shelf(rate, cutoff, -9, frequency)
            error = abs(20*np.log10(measured/expected))
            assert error < 1e-8, (cutoff, frequency, error)
            shelf_cases.append(dict(cutoff=cutoff, frequency=frequency, attenuation_db=float(20*np.log10(measured)), error_db=float(error)))
    # False-trigger diagnostic: voiced harmonic stack vs same voice plus HF noise.
    t = np.arange(48000)/48000
    voice = sum(.35/k**1.5*np.sin(2*np.pi*180*k*t) for k in range(1, 16))
    noise = sosfilt(butter(2, 7000, btype='highpass', fs=48000, output='sos'), rng.uniform(-1, 1, len(t)))
    metrics = {}
    for name, x in [('voiced', voice), ('sibilant', voice*.2+noise*.6)]:
        data = render(48000, np.column_stack([x, x, x, x]), {})
        metrics[name+'_mean_gr_db'] = float(np.mean(data[-24000:, 6]))
    assert metrics['voiced_mean_gr_db'] < .05
    assert metrics['sibilant_mean_gr_db'] > 3
    result = dict(reference_cases=len(cases), max_audio_error=max(c['max_audio_error'] for c in cases),
                  shelf_cases=shelf_cases, synthetic_detection=metrics, cases=cases,
                  pending='Real vocal listening, lisping/false-positive evaluation, Bitwig recall/export, worst callback timing')
    report = ROOT/'reports/deesser-measurements.json'
    report.parent.mkdir(exist_ok=True)
    report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('cases', 'shelf_cases')}, indent=2))


if __name__ == '__main__':
    main()
