#!/usr/bin/env python3
"""Independent static DSP/reference and alias qualification. See saturator_reference.py."""
import json
import numpy as np
from scipy import signal
from saturator_reference import REPORT, render, band, reference, split, curve

metrics = {'reference_peak_error': 0., 'dry_response_error_db': 0., 'alias_cases': []}
rng = np.random.default_rng(14517)
for rate in (44100, 48000, 96000):
    x = rng.normal(0, .22, 8192)
    for style in range(4):
        params = {band(b, 1): style for b in range(3)}
        y = render(x, rate, params)[:, 0]
        ref = reference(x, rate, style)
        error = np.max(np.abs(y-ref))
        metrics['reference_peak_error'] = max(metrics['reference_peak_error'], float(error))
        assert error < 2e-10, (rate, style, error)
    # Allpass crossover sum, measured from processed sine amplitude below the FIR transition.
    n = np.arange(32768)
    for f in (50, 250, 997, 4000, 12000, 20000):
        x = .2*np.sin(2*np.pi*f*n/rate)
        y = render(x, rate, {3: 0})[:, 0]
        ref = reference(x, rate, linear=True)
        assert np.max(np.abs(y-ref)) < 2e-10
        # Fit sine/cosine to avoid non-integer-period RMS bias.
        phase = 2*np.pi*f*n[-8192:]/rate
        design = np.column_stack([np.sin(phase), np.cos(phase)])
        coeff = np.linalg.lstsq(design, y[-8192:], rcond=None)[0]
        error = abs(20*np.log10(np.linalg.norm(coeff)/.2))
        metrics['dry_response_error_db'] = max(metrics['dry_response_error_db'], float(error))
        assert error < .05, (rate, f, error)

# Dynamics and all four tone stages checked on actual nonlinear program audio.
metrics['tone_dynamics_peak_error'] = 0.
x = rng.normal(0, .25, 8192)
for dynamics in (-65, 45):
    for style in (0, 3):
        tones = [6, -4, 3, -2]
        params = {band(b, 1): style for b in range(3)} | {band(b, 2): 12 for b in range(3)} | {band(b, 5): dynamics for b in range(3)}
        params |= {band(b, 6+t): gain for b in range(3) for t,gain in enumerate(tones)}
        y = render(x, 48000, params)[:,0]
        ref = reference(x, 48000, style, 12, tones=tones, dynamics=dynamics)
        error = float(np.max(np.abs(y-ref)))
        metrics['tone_dynamics_peak_error'] = max(metrics['tone_dynamics_peak_error'], error)
        assert error < 2e-9, (dynamics, style, error)

# Coherent high-frequency stimuli: measure all non-harmonic bins. The settling
# prefix is a whole number of FFT periods, and allowed harmonic bins are exact.
rate = 48000
size = 16384
n = np.arange(size*4)
for style in range(4):
    for drive in (12, 24, 36):
        for bin_index in (997, 3001, 6007):
            hz = bin_index*rate/size
            x = .5*np.sin(2*np.pi*bin_index*n/size)
            params = {band(b, 1): style for b in range(3)} | {band(b, 2): drive for b in range(3)}
            y = render(x, rate, params)[-size:, 0]
            fft = np.fft.rfft(y)
            allowed = np.zeros(len(fft), bool)
            allowed[:2] = True
            for k in range(bin_index, size//2+1, bin_index):
                allowed[max(0,k-1):k+2] = True
            ratio = np.sqrt(np.sum(np.abs(fft[~allowed])**2)/np.sum(np.abs(fft[allowed])**2))
            alias_db = float(20*np.log10(max(1e-16, ratio)))
            metrics['alias_cases'].append({'style': style, 'drive_db': drive, 'frequency_hz': hz, 'nonharmonic_dbc': alias_db})
            # Explicit product floor at practical drives; extreme drive is reported separately.
            assert alias_db < (-65 if drive <= 24 else -40), (style, drive, hz, alias_db)

# DC from the asymmetric curve must decay on a sustained periodic signal.
n = np.arange(96000)
x = .5*np.sin(2*np.pi*1000*n/rate)
y = render(x, rate, {band(b, 1): 3 for b in range(3)})[:,0]
metrics['asymmetric_dc'] = float(abs(np.mean(y[-24000:])))
assert metrics['asymmetric_dc'] < 1e-6
# Full dry and every-band bypass are phase-identical, including impulse tails.
x = np.zeros(8192); x[0] = 1
mix = render(x, params={3: 0})
off = render(x, params={band(b, 0): 0 for b in range(3)})
metrics['dry_disabled_residual'] = float(np.max(np.abs(mix-off)))
assert metrics['dry_disabled_residual'] < 1e-12
# Independent 128x ideal-resampler convergence, distinct from the halfband
# reference above. Crop settling and align only by the declared integer latency.
high_filter = signal.firwin(16385, 1/128, window=('kaiser', 12))
metrics['high_rate_residual_db'] = []
for style in range(4):
    n = np.arange(24576)
    x = .35*np.sin(2*np.pi*8791*n/rate) + .12*np.sin(2*np.pi*997*n/rate)
    ref = np.zeros_like(x)
    for b in split(x, rate):
        u = signal.resample_poly(b, 128, 1, window=high_filter)
        wet = signal.resample_poly(curve(u*10**(24/20), style)/10**(24/20), 1, 128, window=high_filter)
        dry = signal.resample_poly(u, 1, 128, window=high_filter)
        ref += dry + signal.lfilter([1, -1], [1, -np.exp(-2*np.pi*5/rate)], wet-dry)
    params = {band(b, 1): style for b in range(3)} | {band(b, 2): 24 for b in range(3)}
    y = render(x, rate, params)[:,0]
    target = ref[8192:-4096-76]
    residual = y[8192+76:-4096]-target
    error_db = float(20*np.log10(np.linalg.norm(residual)/np.linalg.norm(target)))
    metrics['high_rate_residual_db'].append(error_db)
    assert error_db < -65, (style, error_db)

metrics['status'] = 'passed'
(REPORT/'measurements.json').write_text(json.dumps(metrics, indent=2)+'\n')
print(json.dumps({k:v for k,v in metrics.items() if k!='alias_cases'}, indent=2))
print('Worst nonharmonic level by drive:', {d:max(c['nonharmonic_dbc'] for c in metrics['alias_cases'] if c['drive_db']==d) for d in (12,24,36)})
