#!/usr/bin/env python3
"""Independent SciPy audio references and nonlinear alias diagnostics.
Run after building saturator_render. No plugin modules or C++ equations imported.
"""
import json
from pathlib import Path
import subprocess
import numpy as np
from scipy import signal
ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'build/release/saturator_render'
REPORT = ROOT / 'reports/saturator'
REPORT.mkdir(parents=True, exist_ok=True)

def render(x, rate=48000, params=None):
    x = np.asarray(x, dtype='<f8')
    if x.ndim == 1:
        x = np.column_stack((x, x))
    args = [str(RENDER), str(rate)]
    for k, v in (params or {}).items():
        args += [str(k), str(v)]
    data = subprocess.run(args, input=x.tobytes(), stdout=subprocess.PIPE, check=True).stdout
    return np.frombuffer(data, dtype='<f8').reshape(-1, 2)

def band(b, field):
    return 7 + b * 12 + field

def split(x, rate):
    def lr(x, hz, kind):
        sos = signal.butter(2, hz, btype=kind, fs=rate, output='sos')
        return signal.sosfilt(np.tile(sos, (2, 1)), x)
    lo, hi = lr(x, 250, 'low'), lr(x, 250, 'high')
    return [lr(lo, 4000, 'low') + lr(lo, 4000, 'high'), lr(hi, 4000, 'low'), lr(hi, 4000, 'high')]

# A separately constructed ideal halfband and Kaiser window. Normalize the odd
# phase explicitly to match the documented unit-DC phase specification.
filters = []
for n in (129, 33, 17, 17, 33):
    t = np.arange(n) - (n - 1) / 2
    h = .5 * np.sinc(t / 2) * np.kaiser(n, 10)
    h[::2] = 0
    h[(n - 1) // 2] = .5
    h[1::2] *= .5 / np.sum(h[1::2])
    filters.append(h)

def stages(rate):
    return 5 if rate<=48000 else 4 if rate<=96000 else 3 if rate<=192000 else 2 if rate<=384000 else 1

def up(x, rate):
    for h in filters[:stages(rate)]:
        x = signal.upfirdn(2*h, x, up=2)[:len(x)*2]
    return x

def down(x, rate):
    for h in reversed(filters[:stages(rate)]):
        x = signal.upfirdn(h, x, down=2)[:len(x)//2]
    return x

def curve(x, style):
    if style == 0:
        return np.tanh(x)
    if style == 1:
        return np.arctan(x)
    if style == 2:
        return x / np.sqrt(1 + x*x)
    t = np.tanh(.35)
    return (np.tanh(x+.35)-t)/(1-t*t)

def tone_filter(x, rate, hz, gain, kind):
    # Independent RBJ biquad equations, not the engine's state-variable form.
    a = 10**(gain/40)
    omega = 2*np.pi*min(hz, rate*.4)/rate
    c, alpha = np.cos(omega), np.sin(omega)/(2*np.sqrt(.5))
    beta = 2*np.sqrt(a)*alpha
    if kind == 'bell':
        b = [1+alpha*a, -2*c, 1-alpha*a]
        d = [1+alpha/a, -2*c, 1-alpha/a]
    elif kind == 'low':
        b = [a*((a+1)-(a-1)*c+beta), 2*a*((a-1)-(a+1)*c), a*((a+1)-(a-1)*c-beta)]
        d = [(a+1)+(a-1)*c+beta, -2*((a-1)+(a+1)*c), (a+1)+(a-1)*c-beta]
    else:
        b = [a*((a+1)+(a-1)*c+beta), -2*a*((a-1)+(a+1)*c), a*((a+1)+(a-1)*c-beta)]
        d = [(a+1)-(a-1)*c+beta, 2*((a-1)-(a+1)*c), (a+1)-(a-1)*c-beta]
    return signal.lfilter(b, d, x)

def reference(x, rate, style=0, drive=6, linear=False, tones=None, dynamics=0):
    result = np.zeros_like(x)
    g = 10**(drive/20)
    pole = np.exp(-2*np.pi*5/rate)
    for b in split(x, rate):
        u = up(b, rate)
        dry = down(u, rate)
        detector = 0.
        dynamic_gain = np.ones(len(b))
        if dynamics:
            for i, value in enumerate(np.abs(b)):
                coeff = np.exp(-1/(rate*(.01 if value > detector else .1)))
                detector = coeff*detector+(1-coeff)*value
                level = 20*np.log10(max(1e-12, detector))
                db = -dynamics*.0075*max(0, level+18) if dynamics>0 else -dynamics*.0075*min(0, level+18)
                dynamic_gain[i] = 10**(np.clip(db,-60,0)/20)
        if linear:
            result += dry
        else:
            wet = down(curve(u*np.repeat(dynamic_gain,2**stages(rate))*g, style)/g, rate)
            if tones is not None:
                for hz, gain, kind in zip((160,800,3000,8000), tones, ('low','bell','bell','high')):
                    wet = tone_filter(wet, rate, hz, gain, kind)
            result += dry + signal.lfilter([1, -1], [1, -pole], wet-dry)
    padding = [12, 4, 2, 1, 0][stages(rate)-1]
    return np.r_[np.zeros(padding),result[:-padding]] if padding else result


def response_metrics(rate):
    """Dry and small-signal response over the stated 20 Hz–20 kHz band."""
    impulse = np.zeros(65536)
    impulse[0] = 1e-6
    dry = render(impulse, rate, {3: 0})[:, 0]
    wet = render(impulse, rate)[:, 0]
    bins = np.fft.rfftfreq(len(impulse), 1/rate)
    audible = (bins >= 20) & (bins <= 20000)
    response = 20*np.log10(np.maximum(np.abs(np.fft.rfft(dry))/1e-6, 1e-30))
    return dict(rate=rate, magnitude_error_db=float(np.max(np.abs(response[audible]))),
                small_signal_peak_error=float(np.max(np.abs(wet-dry))/1e-6))
