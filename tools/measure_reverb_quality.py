#!/usr/bin/env python3
"""Rendered-audio diagnostics, not a perceptual quality score or certification."""
import argparse
import json
import math
import subprocess
import tempfile
from pathlib import Path
import numpy as np
from scipy.signal import butter, sosfilt

ROOT = Path(__file__).resolve().parents[1]


def render(exe, seconds, rate, settings, kind='impulse', legacy=False):
    with tempfile.TemporaryDirectory() as td:
        path = Path(td) / 'audio.f64'
        args = [str(exe), str(path), str(rate), str(seconds), kind]
        for key, value in settings.items():
            args += [str(key), str(value)]
        if legacy: args.append('--legacy')
        subprocess.run(args, check=True)
        x = np.fromfile(path, dtype='<f8').reshape(-1, 2)
        assert np.isfinite(x).all()
        return x


def rt60(x, rate, center):
    y = sosfilt(butter(3, [center / np.sqrt(2), center * np.sqrt(2)],
                      fs=rate, btype='bandpass', output='sos'), x, axis=0)
    e = np.cumsum(np.sum(y*y, axis=1)[::-1])[::-1]
    db = 10 * np.log10(np.maximum(e, 1e-100) / max(e[0], 1e-100))
    mask = (db < -5) & (db > -25)
    if np.sum(mask) < 100:
        return None
    t = np.arange(len(x))[mask] / rate
    slope, intercept = np.polyfit(t, db[mask], 1)
    residual = np.std(db[mask] - (slope*t+intercept))
    return {'seconds': float(-60/slope), 'fit_residual_db': float(residual)}


def metrics(x, rate):
    # Abel/Huang normalized echo density, rectangular 20 ms windows, 5 ms hop.
    # Report window centers, and require 50 ms sustained density >= 0.9.
    length, hop = round(.020*rate), round(.005*rate)
    times, density = [], []
    for at in range(0, min(len(x)-length, rate), hop):
        a = x[at:at+length]
        std = np.std(a, axis=0)
        d = np.mean(np.abs(a-np.mean(a, axis=0)) > std, axis=0)
        d[std < 1e-12] = 0
        density.append(float(np.mean(d) / math.erfc(1/math.sqrt(2))))
        times.append((at+length/2)/rate)
    mixing = next((times[n] for n in range(len(density)-10)
                   if min(density[n:n+10]) >= .9), None)
    late = x[round(.15*rate):round(1.5*rate)]
    correlation = float(np.corrcoef(late.T)[0, 1])
    spectrum = np.sum(np.abs(np.fft.rfft(x, axis=0))**2, axis=1)
    frequencies = np.fft.rfftfreq(len(x), 1/rate)
    edges = np.geomspace(200, 12000, 145)
    levels = np.array([10*np.log10(max(1e-100, np.mean(spectrum[(frequencies >= a) & (frequencies < b)])))
                       for a,b in zip(edges[:-1],edges[1:])])
    smooth = np.convolve(np.pad(levels, (12,12), mode='edge'), np.ones(25)/25, mode='valid')
    ripple = levels-smooth
    return {'local_spectral_ripple_db_std': float(np.std(ripple)),
            'local_spectral_peak_db': float(np.max(ripple)),
            'octave_t60': {str(f): rt60(x, rate, f) for f in (250, 500, 1000, 2000, 4000, 8000)},
            'mixing_time_s': mixing,
            'early_density_20_100ms': float(np.mean([d for t,d in zip(times,density) if .02 <= t <= .1])),
            'late_density_200_500ms': float(np.mean([d for t,d in zip(times,density) if .2 <= t <= .5])),
            'late_lr_correlation': correlation,
            'peak': float(np.max(np.abs(x))),
            'energy': float(np.sum(x*x)),
            'early_energy_fraction_50ms': float(np.sum(x[:round(.05*rate)]**2)/np.sum(x*x)),
            'density_times_s': times, 'density': density}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--renderer', type=Path, default=ROOT/'build/release/reverb_render')
    parser.add_argument('--legacy', action='store_true')
    parser.add_argument('--check', action='store_true', help='Apply the refined-engine qualification gates')
    parser.add_argument('--output', type=Path, default=ROOT/'reports/reverb-quality/current.json')
    args = parser.parse_args()
    results = {}
    for rate in (44100, 48000, 96000):
        for space in (.65, 2.5, 8):
            settings = {11:100, 4:0, 5:0, 8:100, 2:space}
            x = render(args.renderer, min(24, max(5, space*2)), rate, settings, legacy=args.legacy)
            results[f'neutral_{rate}_{space}'] = metrics(x, rate)
    for style in range(3):
        settings = {11:100, 4:0, 13:style}
        results[f'default_style_{style}'] = metrics(render(args.renderer, 6, 48000, settings, legacy=args.legacy),48000)
    if args.check:
        for name,m in results.items():
            assert m['mixing_time_s'] is not None and m['mixing_time_s'] < .30, (name,'density')
            assert m['local_spectral_ripple_db_std'] < 1.6, (name,'ripple')
            if name.startswith('neutral_'):
                target = float(name.split('_')[-1])
                for band in ('1000','8000'):
                    assert abs(m['octave_t60'][band]['seconds']/target-1) < .08, (name,band)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, indent=2)+'\n')
    for name,m in results.items():
        print(name, 'mixing',m['mixing_time_s'],'early density',round(m['early_density_20_100ms'],3),
              'ripple',round(m['local_spectral_ripple_db_std'],2),'T60 1k/8k',*[round(m['octave_t60'][str(f)]['seconds'],3) for f in (1000,8000)])

if __name__ == '__main__':
    main()
