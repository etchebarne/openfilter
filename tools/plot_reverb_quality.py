#!/usr/bin/env python3
"""Plot measured audio diagnostics; run the measurement scripts first."""
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

REPORT = Path(__file__).resolve().parents[1] / 'reports/reverb-quality'
baseline = json.loads((REPORT / 'baseline.json').read_text())
current = json.loads((REPORT / 'current.json').read_text())
colors = ('#777c85', '#197989')

plt.rcParams.update({'font.size': 10, 'axes.spines.top': False,
                     'axes.spines.right': False, 'axes.titleweight': 'bold'})
fig, axes = plt.subplots(2, 2, figsize=(12, 8), layout='constrained')
fig.suptitle('OpenFilter Reverb 0.2 — measured changes', fontsize=19, weight='bold')

ax = axes[0, 0]
bands = ['250', '500', '1000', '2000', '4000', '8000']
for data, label, color in zip((baseline, current), ('0.1.1 baseline', 'Refined'), colors):
    m = data['neutral_48000_8']['octave_t60']
    ax.plot(range(6), [m[b]['seconds'] for b in bands], 'o-', label=label, color=color)
ax.axhline(8, color='#b58223', ls='--', label='8 s target')
ax.set(xticks=range(6), xticklabels=['250', '500', '1k', '2k', '4k', '8k'],
       xlabel='Octave center (Hz)', ylabel='Estimated T60 (seconds)', ylim=(0, 9.5),
       title='Decay accuracy · neutral 8 s / 48 kHz')
ax.legend(frameon=False, fontsize=9)

ax = axes[0, 1]
for data, label, color in zip((baseline, current), ('0.1.1 baseline', 'Refined'), colors):
    m = data['neutral_48000_2.5']
    ax.plot(np.array(m['density_times_s']) * 1000, m['density'], color=color, label=label)
ax.axhline(.9, color='#b58223', ls='--')
ax.set(xlim=(0, 600), ylim=(0, 1.2), xlabel='Time after impulse (ms)',
       ylabel='Normalized echo density', title='Buildup · neutral 2.5 s / 48 kHz')
ax.text(135, .23, 'Mixing: 360 → 110 ms\n50 ms sustained density ≥ 0.9', fontsize=10)

ax = axes[1, 0]
x = np.arange(3)
for i, (data, label, color) in enumerate(zip((baseline, current), ('0.1.1 baseline', 'Refined'), colors)):
    values = [data[f'neutral_48000_{s}']['local_spectral_ripple_db_std'] for s in ('0.65', '2.5', '8')]
    bars = ax.bar(x + (i - .5) * .34, values, width=.34, color=color, label=label)
    ax.bar_label(bars, fmt='%.2f', padding=3, fontsize=9)
ax.set(xticks=x, xticklabels=['0.65 s', '2.5 s', '8 s'], ylim=(0, 1.7),
       ylabel='Residual standard deviation (dB)', xlabel='Nominal Space / 48 kHz',
       title='Spectral ripple · lower is flatter')
ax.text(.5, .95, 'Not every setting improved; this is not an audibility score.',
        transform=ax.transAxes, ha='center', va='top', fontsize=8.5)

ax = axes[1, 1]
q = json.loads((REPORT / 'qualification.json').read_text())
cases = ['predelay-sweep', 'space-sweep', 'shape-switch']
for i, (version, color) in enumerate(zip(('legacy', 'refined'), colors)):
    values = [q[c][version]['distant_sideband_db'] for c in cases]
    bars = ax.bar(x + (i - .5) * .34, values, width=.34, color=color)
    ax.bar_label(bars, fmt='%.1f', padding=3, fontsize=9)
ax.set(xticks=x, xticklabels=['Predelay', 'Space', 'Post-EQ shape'], ylim=(-65, 0),
       ylabel='Outside-band energy / signal (dB)', title='997 Hz transitions · lower is cleaner')

for ax in axes.flat:
    ax.grid(axis='y', alpha=.15)
    ax.set_axisbelow(True)
fig.supxlabel('Controlled synthetic probes · method and limitations in docs/reverb-quality.md · no Pro-R audio comparison',
              fontsize=9)
fig.savefig(REPORT / 'quality-comparison.png', dpi=150)
fig.savefig(REPORT / 'quality-comparison.pdf')
print(REPORT / 'quality-comparison.png')
