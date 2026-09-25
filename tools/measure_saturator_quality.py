#!/usr/bin/env python3
"""Wider signal-level/rate coverage. Report alias residue separately from wanted
harmonics, and high-rate error separately from aliasing. No subjective claims.
"""
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy import signal
from saturator_reference import render, band, split, curve, response_metrics, RENDER
REPORT = Path(__file__).resolve().parents[1] / 'reports/saturator-cpu'
REPORT.mkdir(parents=True, exist_ok=True)
metrics = {'binary_sha256': hashlib.sha256(RENDER.read_bytes()).hexdigest(), 'alias_cases': [], 'response': [], 'convergence': []}
def save():
    (REPORT/'quality.json').write_text(json.dumps(metrics, indent=2)+'\n')
size = 8192
for rate in (44100, 48000, 96000, 192000):
    # At least one second of settling, rounded to complete FFT periods.
    n = np.arange((int(np.ceil(rate / size)) + 1) * size)
    for style in range(4):
        for drive in (12, 24, 36):
            for amplitude_db in (-18, -6, 0):
                for frequency in (997, 7000, 13000, 19000):
                    k = int(round(frequency * size / rate)) | 1
                    x = 10**(amplitude_db/20) * np.sin(2*np.pi*k*n/size)
                    params = {band(b, 1): style for b in range(3)} | {band(b, 2): drive for b in range(3)}
                    y = render(x, rate, params)[-size:, 0]
                    fft = np.fft.rfft(y)
                    allowed = np.zeros(len(fft), bool)
                    allowed[:2] = True
                    for h in range(k, size//2+1, k):
                        allowed[h-1:h+2] = True
                    residual = fft.copy(); residual[allowed] = 0
                    noise = np.sqrt(np.mean(np.fft.irfft(residual, n=size)**2))
                    rms = np.sqrt(np.mean(y*y))
                    dbc = float(20*np.log10(max(noise/rms, 1e-16)))
                    dbfs = float(20*np.log10(max(noise, 1e-16)))
                    freqs = np.fft.rfftfreq(size, 1/rate)
                    audible = (freqs >= 20) & (freqs <= 20000)
                    wanted = fft.copy(); wanted[~(allowed & audible)] = 0
                    audible_residual = residual.copy(); audible_residual[~audible] = 0
                    audible_noise = np.sqrt(np.mean(np.fft.irfft(audible_residual, n=size)**2))
                    wanted_rms = np.sqrt(np.mean(np.fft.irfft(wanted, n=size)**2))
                    audible_dbc = float(20*np.log10(max(audible_noise/wanted_rms, 1e-16)))
                    row = dict(rate=rate, style=style, drive_db=drive, input_peak_dbfs=amplitude_db,
                               hz=k*rate/size, residue_dbc=dbc, residue_rms_dbfs=dbfs,
                               largest_residue_hz=float(freqs[np.argmax(np.abs(residual))]),
                               audible_residue_dbc=audible_dbc,
                               audible_residue_rms_dbfs=float(20*np.log10(max(audible_noise,1e-16))))
                    metrics['alias_cases'].append(row)
    # Dry path frequency response and small-signal wet match across audible band.
    row = response_metrics(rate)
    metrics['response'].append(row)
    assert row['magnitude_error_db'] < .01 and row['small_signal_peak_error'] < 1e-8
    save()
    print(f'Completed alias/response sweep at {rate} Hz', flush=True)
# Compare full audio against two independent high-rate references, checking that
# the reference itself has converged. Includes low, mid and bright multitone/noise.
rate = 48000
n = np.arange(24576)
rng = np.random.default_rng(1927)
noise = signal.sosfilt(signal.butter(4, 17000, fs=rate, output='sos'), rng.normal(0, .1, len(n)))
program = .18*np.sin(2*np.pi*73*n/rate) + .12*np.sin(2*np.pi*997*n/rate) + .07*np.sin(2*np.pi*11300*n/rate) + noise
for style in range(4):
    for drive in (12, 24, 36):
        refs = []
        for factor in (64, 128):
            fir = signal.firwin(128*factor+1, 1/factor, window=('kaiser', 12))
            ref = np.zeros_like(program)
            gain = 10**(drive/20)
            for x in split(program, rate):
                u = signal.resample_poly(x, factor, 1, window=fir)
                dry = signal.resample_poly(u, 1, factor, window=fir)
                wet = signal.resample_poly(curve(u*gain, style)/gain, 1, factor, window=fir)
                ref += dry + signal.lfilter([1, -1], [1, -np.exp(-2*np.pi*5/rate)], wet-dry)
            refs.append(ref[8192:-4096-76])
        params = {band(b, 1): style for b in range(3)} | {band(b, 2): drive for b in range(3)}
        out = render(program, rate, params)[8192+76:-4096,0]
        db = lambda a,b: float(20*np.log10(max(np.linalg.norm(a-b)/np.linalg.norm(b),1e-16)))
        # Compare like passbands: the original output anti-alias filter rolls
        # off before Nyquist, while the ideal reference has a different edge.
        # Retain full-band errors as diagnostics; qualify 20 Hz–20 kHz separately.
        window = np.hanning(len(out))
        bins = np.fft.rfftfreq(len(out), 1/rate)
        audible = (bins >= 20) & (bins <= 20000)
        spectrum = lambda y: np.fft.rfft(y*window)[audible]
        metrics['convergence'].append(dict(style=style, drive_db=drive,
            full_band_engine_vs_128_db=db(out,refs[1]),
            audible_engine_vs_128_db=db(spectrum(out),spectrum(refs[1])),
            audible_reference_64_vs_128_db=db(spectrum(refs[0]),spectrum(refs[1]))))
        save()
# A strict, explicit intended-use gate. Hot/extreme settings are reported, not
# hidden by an aggregate average or conflated with ordinary track levels.
nominal = [r for r in metrics['alias_cases'] if r['drive_db']<=24 and r['input_peak_dbfs']<=-6]
metrics['nominal_worst'] = max(nominal, key=lambda r:r['audible_residue_dbc'])
metrics['all_settings_worst'] = max(metrics['alias_cases'], key=lambda r:r['audible_residue_dbc'])
metrics['nominal_full_band_worst'] = max(nominal, key=lambda r:r['residue_dbc'])
metrics['nominal_gate_dbc'] = -90
metrics['qualification_passband_hz'] = [20, 20000]
metrics['nominal_gate_passed'] = metrics['nominal_worst']['audible_residue_dbc'] < -90
metrics['convergence_gate_passed'] = all(r['audible_engine_vs_128_db'] < -65 for r in metrics['convergence'])
(REPORT/'quality.json').write_text(json.dumps(metrics, indent=2)+'\n')
print(json.dumps({k:v for k,v in metrics.items() if k!='alias_cases'},indent=2))
assert metrics['nominal_gate_passed'], metrics['nominal_worst']

assert metrics["convergence_gate_passed"], metrics["convergence"]
