#!/usr/bin/env python3
"""Independent rendered-audio gates: Schroeder decay, stereo and wet processing."""
from pathlib import Path
import json
import subprocess
import tempfile
import numpy as np
from scipy.signal import butter, sosfilt, sosfreqz

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / 'reports/reverb'
REPORT.mkdir(parents=True, exist_ok=True)
EXE = ROOT / 'build/release/reverb_render'


def render(seconds=5, rate=48000, kind='impulse', **params):
    with tempfile.TemporaryDirectory() as td:
        path = Path(td) / 'audio.f64'
        args = [str(EXE), str(path), str(rate), str(seconds), kind]
        settings = {11: 100, 4: 0, 5: 0, 8: 100, **{int(k): v for k, v in params.items()}}
        for k, v in settings.items():
            args += [str(k), str(v)]
        subprocess.run(args, check=True)
        x = np.fromfile(path, dtype=np.float64).reshape(-1, 2)
        assert np.isfinite(x).all(), 'non-finite rendered output'
        return x


def decay(x, rate, band=None):
    if band:
        x = sosfilt(butter(3, band, btype='bandpass', fs=rate, output='sos'), x, axis=0)
    power = np.sum(x*x, axis=1)
    energy = np.cumsum(power[::-1])[::-1]
    db = 10*np.log10(np.maximum(energy, 1e-100)/max(energy[0], 1e-100))
    selected = (db < -5) & (db > -25)
    assert selected.sum() > 100, 'insufficient decay fit'
    slope, _ = np.polyfit(np.arange(len(x))[selected]/rate, db[selected], 1)
    return float(-60/slope)


results = {}
for rate in (44100, 48000, 96000):
    x = render(rate=rate, **{'2': 1.5})
    t = decay(x, rate, (200, 2000))
    assert 1.4 < t < 1.6, (rate, t)
    stereo = float(np.sum((x[:, 0]-x[:, 1])**2)/np.sum(x*x))
    assert stereo > .01
    results[str(rate)] = {'nominal_t60_s': 1.5, 'measured_midband_t60_s': t, 'stereo_difference_energy_ratio': stereo}
base = render(**{'2': 1.5})
short = render(**{'2': 1.5, '20': 1, '21': 1000, '22': 25, '23': .7})
boost = render(seconds=8, **{'2': 1.5, '20': 1, '21': 1000, '22': 200, '23': .7})
a, b, c = (decay(x, 48000, (800, 1200)) for x in (base, short, boost))
assert b < a*.75 and c > a*1.25, (a, b, c)
results['decay_band_1khz'] = {'neutral_s': a, '25_percent_s': b, '200_percent_s': c}
# Output trim must match an independent scalar applied to actual reverb samples.
trim = render(**{'2': 1.5, '1': -6})
assert np.max(np.abs(trim-base*10**(-6/20))) < 1e-12
mono = render(**{'2': 1.5, '9': 0})
assert np.max(np.abs(mono[:, 0]-mono[:, 1])) < 1e-14
# Post-EQ lowpass modifies the wet spectrum, not tail timing by a hidden dry path.
cut = render(**{'2': 1.5, '50': 1, '51': 1000, '54': 4})
hi = butter(3, 5000, btype='highpass', fs=48000, output='sos')
ratio = float(np.sum(sosfilt(hi, cut, axis=0)**2)/np.sum(sosfilt(hi, base, axis=0)**2))
assert ratio < .05, ratio
results['post_lowpass_highband_energy_ratio'] = ratio
# Independent Butterworth bilinear reference, compared through actual reverb audio.
freq = np.fft.rfftfreq(len(base), 1/48000)
original = np.fft.rfft(base[:, 0])
actual = np.fft.rfft(cut[:, 0])
_, expected = sosfreqz(butter(2, 1000, fs=48000, output='sos'), worN=freq, fs=48000)
mask = (freq > 30) & (freq < 15000) & (np.abs(original) > 1e-4)
error = float(np.max(np.abs(actual[mask] / original[mask] - expected[mask])))
assert error < .002, error
results['post_lowpass_complex_reference_max_error'] = error
# Predelay independently shifts the impulse response with modulation disabled.
delayed = render(**{'2': 1.5, '4': 100})
assert np.max(np.abs(delayed[:4800])) == 0
assert np.max(np.abs(delayed[4800:] - base[:-4800])) < 1e-12
# Input-driven ducking and gate must audibly attenuate rendered material.
burst = render(kind='burst', **{'2': 1.5})
ducked = render(kind='burst', **{'2': 1.5, '10': 100})
gated = render(kind='burst', **{'2': 1.5, '15': 1, '16': 100})
assert np.sum(ducked[:12000]**2) < np.sum(burst[:12000]**2)*.8
assert np.sum(gated[48000:]**2) < np.sum(burst[48000:]**2)*.01
results['passed'] = ['rate/decay matrix', 'spectral decay shortening/extension', 'stereo', 'output trim', 'width', 'post lowpass', 'predelay', 'ducking', 'gate']
(REPORT / 'measurements.json').write_text(json.dumps(results, indent=2)+'\n')
print(json.dumps(results, indent=2))
