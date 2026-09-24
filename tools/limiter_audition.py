#!/usr/bin/env python3
"""Make an original synthetic listening fixture, aligned and RMS matched.

This is an audition aid, not a claim of a listening test or real-mix validation.
No downloaded material or user DAW projects are read or changed.
"""
import json
from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter
from measure_limiter import render, latency_for

ROOT = Path(__file__).resolve().parents[1]
RATE = 48000


def main():
    duration = 12
    count = RATE * duration
    x = np.zeros((count, 2))
    rng = np.random.default_rng(824)

    def add(at, wave, pan=0):
        first = round(at * RATE)
        length = min(len(wave), count - first)
        if length > 0:
            x[first:first+length, 0] += wave[:length] * np.sqrt((1-pan)/2)
            x[first:first+length, 1] += wave[:length] * np.sqrt((1+pan)/2)

    for beat in range(20):
        at = beat * .6
        t = np.arange(round(RATE * .5)) / RATE
        # A decaying pitch sweep, snare/noise and alternating hats.
        phase = 2*np.pi*(48*t + 75*.022*(1-np.exp(-t/.022)))
        add(at, .7*np.sin(phase)*np.exp(-t*13))
        if beat % 2:
            noise = lfilter([1, -.7], [1], rng.normal(0, 1, len(t)))
            add(at, .12*noise*np.exp(-t*30) + .18*np.sin(2*np.pi*180*t)*np.exp(-t*25), -.15)
        for off in [0, .3]:
            noise = lfilter([1, -1], [1], rng.normal(0, 1, len(t)))
            add(at+off, .025*noise*np.exp(-t*65), .4 if beat % 2 else -.4)
        root = [55, 65.406, 73.416, 49][(beat//4)%4]
        bass = np.sin(2*np.pi*root*t) + .15*np.sin(2*np.pi*2*root*t)
        add(at+.02, .28*bass*(1-np.exp(-t*300))*np.exp(-t*5))
        if beat % 2 == 0:
            t = np.arange(round(RATE * 1.1)) / RATE
            for ratio, pan in [(4,-.6),(5,.15),(6,.6)]:
                note = np.sin(2*np.pi*root*ratio*t) + .12*np.sin(2*np.pi*root*ratio*3*t)
                add(at+.06, .075*note*(1-np.exp(-t*180))*np.exp(-t*3.5), pan)
    x *= .9 / abs(x).max()
    x[-960:] *= np.linspace(1, 0, 960)[:, None]
    padded = np.pad(x, ((0, 2*RATE), (0, 0)))
    versions = {'Dry': x}
    raw_stats = {}
    delay = latency_for(RATE)
    for name, style in [('Clean',1),('Punch',2),('Dense',3)]:
        y = render(padded, RATE, {1:12, 7:style, 8:2, 9:50, 3:200, 11:1})
        versions[name] = y[delay:delay+count, 2:4]
        raw_stats[name] = {'peak_dbfs': float(20*np.log10(abs(versions[name]).max())),
                           'maximum_reduction_db': float(y[:,4].max())}
    directory = ROOT / 'reports/limiter-0.3/audition'
    directory.mkdir(parents=True, exist_ok=True)
    comparisons = []
    timings = []
    for name, y in versions.items():
        rms = np.sqrt(np.mean(y*y))
        matched = y * 10**(-24/20) / rms
        assert abs(matched).max() < 1
        wavfile.write(directory / f'{name.lower()}.wav', RATE, matched.astype(np.float32))
        timings.append({'name': name, 'starts_seconds': len(comparisons)*(duration+1)})
        comparisons.append(np.vstack([matched, np.zeros((RATE,2))]))
    wavfile.write(directory / 'comparison.wav', RATE, np.vstack(comparisons).astype(np.float32))
    metadata = {'source': 'Original deterministic synthetic bass/drums/chords',
                'sample_rate': RATE, 'matching': 'Stereo RMS -24 dBFS; not LUFS or perceptual loudness matching',
                'settings': '12 dB drive, -1 dBFS ceiling, 2 ms lookahead, 50 ms attack, 200 ms release, full links, True Peak on',
                'sequence': timings, 'unmatched_output_statistics': raw_stats,
                'listening_status': 'Rendered for user audition; no subjective quality pass claimed'}
    (directory/'notes.json').write_text(json.dumps(metadata, indent=2)+'\n')
    print(directory)


if __name__ == '__main__':
    main()
