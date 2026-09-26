#!/usr/bin/env python3
"""Drive must produce harmonics without the default band-level collapse.
Tests new Auto level and original recall behavior on actual rendered audio.
"""
import hashlib
import json
from pathlib import Path
import numpy as np
from saturator_reference import render, band, balanced_reference, RENDER

out = Path(__file__).resolve().parents[1]/'reports/saturator-drive'
out.mkdir(parents=True, exist_ok=True)
report = dict(binary_sha256=hashlib.sha256(RENDER.read_bytes()).hexdigest(),
              drive_cases=[], alias_cases=[], references=[])
def save():
    (out/'measurements.json').write_text(json.dumps(report, indent=2)+'\n')

size = 8192
for rate in (44100,48000):
    n = np.arange((int(np.ceil(rate/size))+1)*size)
    for frequency,b in ((55,0),(997,1)):
        k = int(round(frequency*size/rate)) | 1
        for style in range(4):
            for level in (-24,-12,-6):
                x = 10**(level/20)*np.sin(2*np.pi*k*n/size)
                base_rms = base_harmonics = None
                for drive in (0,6,12,24,36):
                    p = {band(b,10):1,band(b,1):style,band(b,2):drive}
                    y = render(x,rate,p)[-size:,0]
                    assert np.isfinite(y).all()
                    rms = np.sqrt(np.mean(y*y))
                    fft = np.fft.rfft(y)
                    harmonics = np.sqrt(sum(abs(fft[h])**2 for h in range(2*k,size//2+1,k))) * np.sqrt(2)/size
                    if drive == 0:
                        base_rms,base_harmonics = rms,harmonics
                    delta = float(20*np.log10(rms/base_rms))
                    row = dict(rate=rate,band=b,style=style,input_peak_dbfs=level,
                               hz=k*rate/size,drive_db=drive,rms_delta_db=delta,
                               thd_db=float(20*np.log10(max(harmonics*size/(abs(fft[k])*np.sqrt(2)),1e-16))),
                               harmonic_growth_db=float(20*np.log10(max(harmonics/base_harmonics,1e-16))))
                    report['drive_cases'].append(row)
                    # Low-band DC rejection causes small frequency-dependent differences.
                    assert abs(delta) < .2, row
                    if drive == 24:
                        # Asymmetric already generates even harmonics at 0 dB drive.
                        assert row['harmonic_growth_db'] > 12 and row['thd_db'] > -30, row
        print(f'Drive/level sweep passed: {rate} Hz, {frequency} Hz tone',flush=True)
        save()

# New auto-level detector must not introduce audible-band modulation/alias spurs.
for rate in (44100,48000,96000,192000):
    n = np.arange((int(np.ceil(rate/size))+1)*size)
    for frequency in (7000,13000,19000):
        k = int(round(frequency*size/rate)) | 1
        for style in range(4):
            for drive in (12,24):
                x = 10**(-6/20)*np.sin(2*np.pi*k*n/size)
                p = {band(b,1):style for b in range(3)} | {band(b,2):drive for b in range(3)}
                y = render(x,rate,p)[-size:,0]
                fft = np.fft.rfft(y)
                allowed = np.zeros(len(fft),bool); allowed[:2] = True
                for h in range(k,size//2+1,k):
                    allowed[h-1:h+2] = True
                hz = np.fft.rfftfreq(size,1/rate)
                audible = (hz>=20)&(hz<=20000)
                ratio = np.linalg.norm(fft[~allowed & audible])/np.linalg.norm(fft[allowed & audible])
                row = dict(rate=rate,hz=k*rate/size,style=style,drive_db=drive,
                           residue_dbc=float(20*np.log10(max(ratio,1e-16))))
                report['alias_cases'].append(row)
                assert row['residue_dbc'] < -90, row
    print(f'Auto-level audible alias sweep passed: {rate} Hz',flush=True)
    save()

rng = np.random.default_rng(2729)
for rate in (44100,48000,96000):
    x = rng.normal(0,.17,8192)
    for style in range(4):
        tones = [3,-2,4,-3]
        dynamics = 35 if style%2 else -25
        p = {band(b,1):style for b in range(3)} | {band(b,2):18 for b in range(3)}
        p |= {band(b,5):dynamics for b in range(3)}
        p |= {band(b,6+t):value for b in range(3) for t,value in enumerate(tones)}
        reference = balanced_reference(x,rate,style,18,tones,dynamics)
        y = render(x,rate,p)[:,0]
        error = float(np.max(np.abs(reference-y)))
        report['references'].append(dict(rate=rate,style=style,peak_error=error))
        assert error < 2e-9, (rate,style,error)
    print(f'Independent auto-level audio reference passed: {rate} Hz',flush=True)
    save()

report['worst_level_drift_db'] = max(abs(r['rms_delta_db']) for r in report['drive_cases'])
report['minimum_harmonic_growth_at_24_db'] = min(r['harmonic_growth_db'] for r in report['drive_cases'] if r['drive_db']==24)
report['worst_alias_dbc'] = max(r['residue_dbc'] for r in report['alias_cases'])
report['reference_peak_error'] = max(r['peak_error'] for r in report['references'])
# Level steps, a real pause and restart. Keep the output's normal adaptation
# visible in the report; this is not a claim of instantaneous/perceptual matching.
rate = 48000
t = np.arange(rate*6)/rate
envelope = np.select([(t>=.5)&(t<2),(t>=2)&(t<3.5),t>=4.5],[.25,.025,.25],0.)
x = envelope*np.sin(2*np.pi*1000*t)
p = {band(b,2):24 for b in range(3)}
y = render(np.column_stack((x,-x)),rate,p)
assert np.isfinite(y).all()
assert np.max(np.abs(y[:,0]+y[:,1])) < 1e-12
step_errors = []
for start,end in ((1.5,2),(3,3.5),(5.5,6)):
    sl = slice(int(start*rate),int(end*rate))
    error = float(20*np.log10(np.linalg.norm(y[sl,0])/np.linalg.norm(x[sl])))
    step_errors.append(error)
    assert abs(error) < .15, (start,error)
tail_peak = float(np.max(np.abs(y[int(4.3*rate):int(4.5*rate)])))
assert tail_peak < 1e-6
unmatched = render(x,rate,{**p,6:0,43:1})
manual = render(x,rate,{**p,6:0,43:0})
assert np.max(np.abs(unmatched-manual)) < 1e-13
report['level_steps'] = dict(settled_rms_errors_db=step_errors,tail_peak=tail_peak,
                             peak=float(np.max(np.abs(y))),anti_phase_error=float(np.max(np.abs(y[:,0]+y[:,1]))))
report['status'] = 'passed'
save()
print(json.dumps({k:v for k,v in report.items() if not isinstance(v,list)},indent=2))
