#!/usr/bin/env python3
"""Measure calibrated voices on real engine output, including quiet input and maximum Drive."""
import hashlib
import json
from pathlib import Path
import numpy as np
from saturator_reference import render, band, balanced_reference, RENDER

out = Path(__file__).resolve().parents[1]/'reports/saturator-research'
out.mkdir(parents=True, exist_ok=True)
report = dict(binary_sha256=hashlib.sha256(RENDER.read_bytes()).hexdigest(), levels=[], aliases=[], references=[])

def save():
    (out/'character.json').write_text(json.dumps(report,indent=2)+'\n')

def params(style,drive):
    return {band(b,1):style for b in range(3)} | {band(b,2):drive for b in range(3)} | {43:1}

N=8192
for rate in (44100,48000):
    n=np.arange((int(np.ceil(rate/N))+1)*N)
    for frequency,b in ((55,0),(997,1)):
        k=round(frequency*N/rate)|1
        for style in (0,3,4,5):
            for level in (-24,-18,-12):
                x=10**(level/20)*np.sin(2*np.pi*k*n/N)
                base=None
                for drive in (0,6,12,24,36):
                    y=render(x,rate,params(style,drive)|{band(b,10):1})[-N:,0]
                    f=np.fft.rfft(y); rms=np.sqrt(np.mean(y*y))
                    if base is None: base=rms
                    harmonics=np.linalg.norm(f[2*k::k]); fundamental=abs(f[k])
                    row=dict(rate=rate,hz=k*rate/N,band=b,style=style,level=level,drive=drive,
                             rms_delta_db=float(20*np.log10(rms/base)),
                             thd_db=float(20*np.log10(max(harmonics/fundamental,1e-16))),
                             h2_dbc=float(20*np.log10(max(abs(f[2*k])/fundamental,1e-16))),
                             h3_dbc=float(20*np.log10(max(abs(f[3*k])/fundamental,1e-16))))
                    report['levels'].append(row)
                    assert abs(row['rms_delta_db']) < .2, row
                    if style>=4 and drive==12: assert row['thd_db'] > -38, row
        save()
        print('Level/character passed',rate,frequency,flush=True)

for rate in (44100,48000,96000,192000):
    n=np.arange((int(np.ceil(rate/N))+1)*N)
    for frequency in (7000,13000,19000):
        k=round(frequency*N/rate)|1
        for style in (4,5):
            for drive in (6,12,24,36):
                x=10**(-6/20)*np.sin(2*np.pi*k*n/N)
                y=render(x,rate,params(style,drive))[-N:,0]
                f=np.fft.rfft(y);allowed=np.zeros(len(f),bool);allowed[:2]=1
                for h in range(k,N//2+1,k):allowed[h-1:h+2]=1
                hz=np.fft.rfftfreq(N,1/rate);aud=(hz>=20)&(hz<=20000)
                db=float(20*np.log10(max(np.linalg.norm(f[~allowed&aud])/np.linalg.norm(f[allowed&aud]),1e-16)))
                row=dict(rate=rate,hz=k*rate/N,style=style,drive=drive,residue_dbc=db)
                report['aliases'].append(row)
                assert db < -90,row
    print('Full Drive alias sweep passed',rate,flush=True);save()

rng=np.random.default_rng(9228)
for rate in (44100,48000,96000):
    x=rng.normal(0,.13,8192)
    for style in (4,5):
        for drive in (.1,12,36):
            tones=[3,-2,4,-3];dynamics=25 if style==4 else -35
            p=params(style,drive)|{band(b,5):dynamics for b in range(3)}
            p|={band(b,6+t):v for b in range(3) for t,v in enumerate(tones)}
            y=render(x,rate,p)[:,0]
            ref=balanced_reference(x,rate,style,drive,tones,dynamics)
            error=float(np.max(np.abs(y-ref)))
            report['references'].append(dict(rate=rate,style=style,drive=drive,error=error))
            assert error<2e-9,(rate,style,drive,error)
    print('Independent reference passed',rate,flush=True);save()
report['worst_alias_dbc']=max(r['residue_dbc'] for r in report['aliases'])
report['worst_level_drift_db']=max(abs(r['rms_delta_db']) for r in report['levels'] if r['style']>=4)
report['reference_peak_error']=max(r['error'] for r in report['references'])
report['status']='passed';save()
print(json.dumps({k:v for k,v in report.items() if not isinstance(v,list)},indent=2))
