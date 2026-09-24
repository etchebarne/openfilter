#!/usr/bin/env python3
"""Independent audio reference and reconstruction checks for the dual-stage limiter.

Uses SciPy's minimum filter and direct convolution rather than the C++ monotonic
queue/running sum. True peaks below use a separate 32x reconstruction filter,
not a BS.1770 certification. No plugin equations/helpers are imported.
"""
import json
import math
from pathlib import Path
import subprocess
import time
import numpy as np
from scipy.ndimage import minimum_filter1d
from scipy.signal import fftconvolve, resample_poly, firwin, upfirdn

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "build/release/limiter_render"
DEFAULT = [0, 0, -1, 200, 100, 0, 0, 1, 5, 100, 100, 1]


def render(x, rate, settings, mode="stdin"):
    args = [str(RENDER), str(rate), str(len(x)), mode]
    for k, v in settings.items():
        args += [str(k), str(v)]
    raw = subprocess.run(args, input=np.asarray(x, dtype='<f8').tobytes(), stdout=subprocess.PIPE, check=True).stdout
    return np.frombuffer(raw, dtype='<f8').reshape(-1, 5)


def legacy_reference(x, rate, settings):
    p = DEFAULT.copy()
    for k, v in settings.items():
        p[k] = v
    delay = math.ceil(rate * .005)
    length = delay + math.ceil(rate * .01) + 1
    driven = x * 10 ** (p[1] / 20)
    # Causal min-filter, constant unity history before time zero.
    target = minimum_filter1d(1 / np.maximum(1, abs(driven)), length, axis=0,
                              origin=(length - 1) // 2, mode='constant', cval=1)
    envelope = np.empty_like(x)
    slow_decay = math.exp(-1 / (rate * .25))
    for c in range(2):
        g, slow = 1., 0.
        for n in range(len(x)):
            slow = slow_decay * slow + (1 - slow_decay) * (1 - g)
            decay = math.exp(-1000 / (rate * p[3] * (1 + 3 * p[5] * slow)))
            g = min(target[n, c], 1 - decay * (1 - g))
            envelope[n, c] = g
    g = np.column_stack([fftconvolve(np.r_[np.ones(delay), envelope[:, c]], np.ones(delay + 1) / (delay + 1), mode='valid') for c in range(2)])
    g += p[4] / 100 * (g.min(axis=1)[:, None] - g)
    audio = np.pad(driven, ((delay, 0), (0, 0)))[:len(x)]
    result = audio * g * 10 ** (p[2] / 20) / 10 ** (p[6] * p[1] / 20)
    if p[0]:
        result = np.pad(x, ((delay, 0), (0, 0)))[:len(x)]
    return result


def delayed(x, count):
    return np.pad(x, ((count, 0), (0, 0)))[:len(x)]


def causal_min(x, count):
    return minimum_filter1d(x, count, axis=0, origin=(count - 1) // 2, mode='constant', cval=1)


def release_envelope(target, coefficient):
    result = np.empty_like(target)
    current = np.ones(target.shape[1])
    for n in range(len(target)):
        current = np.minimum(target[n], 1 - coefficient * (1 - current))
        result[n] = current
    return result


def average(envelope, length):
    return np.column_stack([fftconvolve(np.r_[np.ones(length - 1), envelope[:, c]],
                                        np.ones(length) / length, mode='valid')
                            for c in range(envelope.shape[1])])


def halfband(taps):
    h = firwin(taps, .5, window=('kaiser', 10))
    middle = (taps-1)//2
    h[::2] = 0
    h[middle] = .5
    h[1::2] *= .5 / h[1::2].sum()
    return h


def fir(x, h):
    return np.column_stack([fftconvolve(x[:, c], h)[:len(x)] for c in range(x.shape[1])])


def interpolate(x, h):
    return upfirdn(h * 2, x, up=2, axis=0)[:len(x)*2]


def factor_for(rate):
    return 4 if rate <= 192000 else 2 if rate <= 384000 else 1


def latency_for(rate):
    factor = factor_for(rate)
    return math.ceil(rate*.005) + (136 if factor==4 else 128 if factor==2 else 0) + 128 + math.ceil(rate*.003) + 266


def modern_reference(driven, rate, delay, p):
    x = driven
    hold = math.ceil(rate*.026)
    punch, dense = p[7] == 2, p[7] == 3
    horizons = [0, .1, .5, 1, 2, 5]
    upper = next(i for i in range(1, 6) if horizons[i] >= p[8])
    w = (p[8] - horizons[upper-1]) / (horizons[upper] - horizons[upper-1])
    transient = np.zeros_like(x)
    for i, weight in [(upper-1, 1-w), (upper, w)]:
        length = math.ceil(rate * horizons[i] / 1000)
        ahead = delayed(driven, delay - length)
        target = causal_min(1 / np.maximum(1, abs(ahead)), length + hold + 1)
        env = release_envelope(target, math.exp(-1000 / (rate * (40 - 32*punch + 40*dense))))
        transient += weight * average(env, length+1)
    audio = delayed(driven, delay)
    held = causal_min(1 / np.maximum(1, abs(audio)), hold + 1)
    demand = -20 * np.log10(held) * (1 - .25*punch + .2*dense)
    body = np.zeros_like(x)
    current, slow = np.zeros(2), np.zeros(2)
    slow_decay = math.exp(-1 / (rate * .25))
    attack = math.exp(-1000 / (rate * p[9]))
    for n in range(len(x)):
        slow = slow_decay * slow + (1 - slow_decay) * current
        release = p[3] * (1 + p[5] * 3 * np.minimum(1, slow/12))
        coef = np.where(demand[n] > current, attack, np.exp(-1000 / (rate * release)))
        current = demand[n] + coef * (current - demand[n])
        body[n] = current
    body = 10 ** (-body / 20)
    transient += p[4]/100 * (transient.min(axis=1)[:, None] - transient)
    body += p[10]/100 * (body.min(axis=1)[:, None] - body)
    wet = audio * np.minimum(transient, body)
    return wet


def reference(x, rate, settings):
    """Batch FIR/minimum filters, independent of C++ symmetry/polyphase/queues."""
    p = DEFAULT.copy()
    for k, v in settings.items():
        p[k] = v
    delay, smooth = math.ceil(rate*.005), math.ceil(rate*.003)
    guard_delay = smooth + 266
    factor = factor_for(rate)
    resampling_delay = 136 if factor==4 else 128 if factor==2 else 0
    if p[7] == 0:
        wet = delayed(legacy_reference(x, rate, {**settings,0:0,2:0,6:0}),resampling_delay)
    else:
        driven = x * 10**(p[1]/20)
        if factor >= 2:
            driven = interpolate(driven,halfband(257))
        if factor == 4:
            driven = interpolate(driven,halfband(33))
        wet = modern_reference(driven,rate*factor,delay*factor,p)
        if factor == 4:
            wet = fir(wet,halfband(33))[::2]
        if factor >= 2:
            wet = fir(wet,halfband(257))[::2]
    wet = fir(wet,firwin(257,.95,window=('kaiser',10))) if p[7] or p[11] else delayed(wet,128)
    sample_peaks = abs(wet).max(axis=1)
    peaks = sample_peaks.copy()
    if p[11]:
        reconstructed = wet
        for taps in [513,33,17]:
            reconstructed = interpolate(reconstructed,halfband(taps))
        peaks = np.maximum(peaks, abs(reconstructed).reshape(len(x),8,2).max(axis=(1,2))) * 10**(.3/20)
    protected = delayed(wet,guard_delay)
    gains = []
    for detector in [sample_peaks,peaks]:
        required = 1/np.maximum(1,detector[:,None])
        target = causal_min(required,guard_delay+math.ceil(rate*.01)+1)
        env = release_envelope(target,math.exp(-1/(rate*.1)))
        gains.append(average(env,smooth+1))
    protected *= np.minimum(*gains)
    protected *= 10**(p[2]/20) / 10**(p[6]*p[1]/20)
    return delayed(x,latency_for(rate)) if p[0] else protected

def db(v):
    return float(20 * np.log10(max(1e-15, v)))


def main():
    start = time.time()
    rng = np.random.default_rng(9213)
    cases = []
    for rate in [44100, 48000, 88200, 96000, 176400, 192000]:
        t = np.arange(round(rate * .75)) / rate
        level = np.where(t < .12, .05, np.where(t < .42, 1.4, .05))
        x = np.column_stack([level * np.cos(2 * np.pi * 997 * t), level * .3 * np.cos(2 * np.pi * 173 * t)])
        settings = {1: 6, 3: 80, 4: 43, 5: 1, 8: .7, 9: 40, 10: 70}
        data = render(x, rate, settings)
        error = float(abs(data[:, 2:4] - reference(x, rate, settings)).max())
        assert error < 2e-10, (rate, error)
        ceiling = float(abs(data[:, 2:4]).max())
        assert ceiling <= 10 ** (-1 / 20) + 1e-12
        cases.append(dict(rate=rate, reference_residual=error, sample_peak_dbfs=db(ceiling)))
    rate = 48000
    stress = rng.uniform(-4, 4, (48000, 2))
    for settings in [{1: 36}, {1: 12, 4: 0, 10: 0, 7: 2, 8: 0}, {1: 6, 6: 1, 7: 3}, {0: 1, 1: 30}, {1: 12, 7: 0, 11: 0}, {1:36,7:2,8:0,11:0}]:
        data = render(stress, rate, settings)
        error = float(abs(data[:, 2:4] - reference(stress, rate, settings)).max())
        assert error < 2e-10, (settings, error)
        cases.append(dict(settings=settings, reference_residual=error))
    t = np.arange(rate * 2) / rate
    distortion = []
    for frequency in [20, 30, 55, 997, 10000]:
        x = np.column_stack([.8 * np.sin(2 * np.pi * frequency * t)] * 2)
        data = render(x, rate, {1: 6})
        y = data[rate:, 2]
        tt = np.arange(len(y)) / rate
        basis = np.column_stack([np.sin(2 * np.pi * frequency * tt), np.cos(2 * np.pi * frequency * tt)])
        fundamental = basis @ np.linalg.lstsq(basis, y, rcond=None)[0]
        residual = np.sqrt(np.mean((y - fundamental) ** 2) / np.mean(fundamental ** 2))
        distortion.append(dict(frequency=frequency, residual_db=db(residual)))
        assert db(residual) < -60, (frequency, db(residual))
    # Independent longer, denser reconstruction; include start/end transients by
    # feeding silence through the engine, rather than truncating active output.
    kernel = firwin(8193, 1/32, window=('kaiser', 12))
    true_peaks = []
    n = np.arange(24000)
    signals = {
        'quarter-rate': np.sin(2*np.pi*.25*n + np.pi/4),
        'random': rng.uniform(-3, 3, len(n)),
        'high-frequency-bursts': np.sin(2*np.pi*.413*n) * ((n % 401) < 60),
        'near-nyquist': np.sin(2*np.pi*.49*n),
        'nyquist-burst': np.cos(np.pi*n),
        'alternating-impulses': np.where(n % 113 == 0, (-1.)**(n//113), 0),
    }
    for name, signal in signals.items():
        x = np.column_stack([np.pad(signal, (2000,4000))]*2)
        for style in [1,2,3]:
            for lookahead in [0,.3,5]:
                out = render(x, rate, {1:12,7:style,8:lookahead})[:,2]
                peak = db(abs(resample_poly(out,32,1,window=kernel)).max())
                assert peak <= -.99, (name,style,lookahead,peak)
                true_peaks.append(dict(signal=name,style=style,lookahead=lookahead,peak_dbfs=peak))
    # Multi-rate, independently seeded stereo overloads with partial linking.
    for seed in range(60):
        source = np.random.default_rng(seed)
        fs = [44100, 48000, 96000, 192000][seed % 4]
        noise = source.normal(0, 2, (12000, 2))
        noise = np.pad(noise, ((2000, 5000), (0, 0)))
        out = render(noise, fs, {1:18, 7:1+seed%3, 8:(seed%6)/2, 4:seed, 10:100-seed})[:,2:4]
        peak = db(abs(resample_poly(out,32,1,axis=0,window=kernel)).max())
        assert peak <= -.99, (seed, fs, peak)
        true_peaks.append(dict(signal='seeded-stereo-noise',seed=seed,rate=fs,peak_dbfs=peak))
    x = np.column_stack([np.pad(signals['quarter-rate'], (2000,4000))]*2)
    without = render(x,rate,{1:12,11:0})[:,2]
    without_peak = db(abs(resample_poly(without,32,1,window=kernel)).max())
    summary = dict(cases=cases, distortion=distortion, true_peak_cases=true_peaks,
                   protection_off_intersample_dbfs=without_peak,
                   note='8x reconstructed-peak protection tested with independent 32x reconstruction; no certification or universal DAC/codec guarantee.',
                   seconds=time.time()-start)
    directory = ROOT / 'reports/limiter'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'measurements.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
