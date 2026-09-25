#!/usr/bin/env python3
"""Create deterministic, RMS-matched synthetic material for initial audition.
This is not a substitute for listening to real studio stems.
"""
from pathlib import Path
import json
import subprocess
import numpy as np
from scipy.io import wavfile

root = Path(__file__).resolve().parents[1]
out = root / 'reports/saturator/listening'
out.mkdir(parents=True, exist_ok=True)
rate = 48000
n = np.arange(rate * 6)
t = n / rate
rng = np.random.default_rng(1941)
beat = np.remainder(t, .5)
pitches = np.array([55., 65.4064, 73.4162, 49.])[np.minimum(3, (t/1.5).astype(int))]
phase = np.cumsum(2*np.pi*pitches/rate)
bass = sum(np.sin(k*phase)/k**1.5 for k in range(1, 16)) * .16 * np.exp(-beat*4)
kick = .3*np.sin(2*np.pi*(52*beat+3*(1-np.exp(-beat*24))))*np.exp(-beat*20)
snare = rng.normal(0, .07, len(t))*np.exp(-np.remainder(t+.25,.5)*45)
chord = sum(.026*np.sin(2*np.pi*f*t) for f in (220,261.6256,329.6276,440,523.2511,659.2551))
hat = rng.normal(0,.025,len(t))*np.exp(-np.remainder(t,.25)*100)
left = bass+kick+snare+chord+hat
right = bass+kick-.6*snare+np.roll(chord,43)-hat
x = np.column_stack((left,right))
fade = np.minimum(1, np.minimum(t/.02, (6-t)/.02))
x *= fade[:,None]
metadata = {'sample_rate':rate,'material':'Deterministic synthetic bass, chords and percussion; not real stems', 'target_rms_dbfs':20*np.log10(.1), 'files':[]}
for style,name in enumerate(('dry','soft','rounded','dense','asymmetric')):
    params = {3:0} if style==0 else {7+b*12+1:style-1 for b in range(3)} | {7+b*12+2:12 for b in range(3)}
    args = [str(root/'build/release/saturator_render'),str(rate)]
    for i,v in params.items():args += [str(i),str(v)]
    data = subprocess.run(args,input=x.astype('<f8').tobytes(),stdout=subprocess.PIPE,check=True).stdout
    y = np.frombuffer(data,dtype='<f8').reshape(-1,2).copy()
    gain = .1 / np.sqrt(np.mean(y*y))
    y *= gain
    assert np.max(np.abs(y)) < 1
    filename = f'{style:02d}-{name}.wav'
    wavfile.write(out/filename,rate,y.astype(np.float32))
    metadata['files'].append({'file':filename,'render_params':params,'match_gain_db':20*np.log10(gain),'sample_peak_dbfs':20*np.log10(np.max(np.abs(y)))})
(out/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(out)
