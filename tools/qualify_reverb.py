#!/usr/bin/env python3
"""Independent quality gates and level-matched synthetic listening examples."""
from pathlib import Path
import json
import numpy as np
from scipy.signal import butter, sosfilt
from scipy.io import wavfile
from measure_reverb_quality import render, ROOT

EXE = ROOT/'build/release/reverb_render'
REPORT = ROOT/'reports/reverb-quality'
REPORT.mkdir(exist_ok=True, parents=True)
rate = 48000
results = {}
# A stationary tone should not acquire large distant sidebands when moving predelay.
stop = butter(6, [897, 1097], btype='bandstop', fs=rate, output='sos')
for kind in ('predelay-sweep', 'space-sweep', 'shape-switch'):
    params = {11:100, 4:0, 5:0, 8:100, 2:2.5}
    if kind == 'shape-switch': params.update({50:1, 51:1000, 52:12, 53:4})
    case = {}
    for legacy in (True,False):
        x = render(EXE,3,rate,params,kind,legacy=legacy)
        high = sosfilt(stop,x,axis=0)
        sl = slice(round(.98*rate),round(1.4*rate))
        case['legacy' if legacy else 'refined'] = {
            'distant_sideband_db': float(10*np.log10(np.sum(high[sl]**2)/np.sum(x[sl]**2))),
            'peak': float(np.max(np.abs(x)))}
    results[kind] = case
for kind in ('predelay-sweep','space-sweep','shape-switch'):
    assert results[kind]['refined']['distant_sideband_db'] < results[kind]['legacy']['distant_sideband_db'] - 10
# Band energy retained during a stationary freeze; broadband and upper octave.
for legacy in (True,False):
    x = render(EXE,8,rate,{11:100,4:0,5:25,8:100,2:1.5},'freeze',legacy=legacy)
    band = sosfilt(butter(3,[5600,11000],fs=rate,btype='bandpass',output='sos'),x,axis=0)
    def drift(y): return float(10*np.log10(np.sum(y[6*rate:8*rate]**2)/np.sum(y[rate:3*rate]**2)))
    results['freeze_'+('legacy' if legacy else 'refined')] = {'broadband_drift_db':drift(x),'highband_drift_db':drift(band)}
assert abs(results['freeze_refined']['broadband_drift_db']) < 1
assert abs(results['freeze_refined']['highband_drift_db']) < 1
# These fixed test signals are repeatable listening probes, not recordings of instruments.
t = np.arange(rate*8)/rate
rng = np.random.default_rng(428)
sources = {}
pluck = np.zeros_like(t)
for at,note in [(0,220),(.6,277.18),(1.2,329.63),(2.4,440)]:
    u = np.maximum(0,t-at)
    for h in range(1,17):
        pluck += (t>=at) * .16/h * np.exp(-u*(2.2+.35*h)) * np.sin(2*np.pi*note*h*u)
sources['plucked_phrase'] = pluck
percussion = np.zeros_like(t)
for at in (0,.5,1,1.75,2,2.75):
    u=np.maximum(0,t-at)
    percussion += (t>=at)*(.35*rng.standard_normal(len(t))*np.exp(-u*70)+.22*np.sin(2*np.pi*150*u)*np.exp(-u*25))
sources['percussion'] = percussion
pad = np.zeros_like(t)
for f in (220,277.18,329.63):
    for h in range(1,9): pad += .065/h*np.sin(2*np.pi*f*h*t)
pad *= np.minimum(1,t/.08)*np.minimum(1,np.maximum(0,(3.2-t)/.4))
sources['sustained_chord'] = pad
listening = REPORT/'listening'
listening.mkdir(exist_ok=True)
manifest=[]
for name,signal in sources.items():
    dry=np.column_stack((signal,signal))
    inputpath=listening/(name+'.f64')
    dry.astype('<f8').tofile(inputpath)
    space=.8 if name=='percussion' else 3.2
    style=2 if name=='percussion' else 0
    params={11:100,2:space,13:style,4:20}
    outputs=[render(EXE,8,rate,params,'file:'+str(inputpath),legacy=v) for v in (True,False)]
    # Match wet RMS over the full clip, then apply a common safety gain to all versions.
    rms=[float(np.sqrt(np.mean(x*x))) for x in outputs]
    gains=[1,rms[0]/rms[1]]
    wet=[x*g for x,g in zip(outputs,gains)]
    mixed=[dry*.75+y*.25 for y in wet]
    gain=min(1,.89/max(np.max(np.abs(x)) for x in wet+mixed+[dry]))
    wavfile.write(listening/(name+'_dry.wav'),rate,(dry*gain).astype('float32'))
    for version,y,m in zip(('legacy','refined'),wet,mixed):
        wavfile.write(listening/(name+'_'+version+'_wet.wav'),rate,(y*gain).astype('float32'))
        wavfile.write(listening/(name+'_'+version+'_mix.wav'),rate,(m*gain).astype('float32'))
    manifest.append({'source':name,'space_s':space,'style':style,'wet_rms_gain_refined_db':20*np.log10(gains[1]),'common_gain':gain})
    inputpath.unlink()
results['listening_manifest']=manifest
(REPORT/'qualification.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))

(listening/'README.md').write_text("""# Reverb 0.2 listening probes

These are synthesized test phrases, not recordings of real instruments.
For each phrase compare `legacy_wet.wav` with `refined_wet.wav`, then the two
`mix.wav` files. Wet RMS is matched over the entire eight-second clip; both
versions share a peak-safety gain. Match playback level and listen to the attack,
tail texture, ringing, stereo image and how naturally the tail joins the source.
The mix uses 75% dry and 25% wet. Settings and gain adjustments are recorded
in ../qualification.json. Listen without looking at filenames when possible.
These examples do not establish FabFilter equivalence or production readiness.

| Phrase | Dry | Old wet | Refined wet | Old mix | Refined mix |
| --- | --- | --- | --- | --- | --- |
| Plucked | [Dry](plucked_phrase_dry.wav) | [Old](plucked_phrase_legacy_wet.wav) | [Refined](plucked_phrase_refined_wet.wav) | [Old](plucked_phrase_legacy_mix.wav) | [Refined](plucked_phrase_refined_mix.wav) |
| Percussion | [Dry](percussion_dry.wav) | [Old](percussion_legacy_wet.wav) | [Refined](percussion_refined_wet.wav) | [Old](percussion_legacy_mix.wav) | [Refined](percussion_refined_mix.wav) |
| Chord | [Dry](sustained_chord_dry.wav) | [Old](sustained_chord_legacy_wet.wav) | [Refined](sustained_chord_refined_wet.wav) | [Old](sustained_chord_legacy_mix.wav) | [Refined](sustained_chord_refined_mix.wav) |
""")
