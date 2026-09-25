#!/usr/bin/env python3
"""Offline research only: first-order ADAA versus retained direct 32x shaping.
Uses analytic antiderivatives, the documented FIRs, and no plugin changes.
"""
import json
from pathlib import Path
import numpy as np
from scipy import signal
from saturator_reference import split, up, down, curve
REPORT = Path(__file__).resolve().parents[1]/'reports/saturator-cpu/adaa.json'
def logcosh(x):
    a=np.abs(x)
    return a+np.log1p(np.exp(-2*a))-np.log(2)
def primitive(x,style):
    if style==0: return logcosh(x)
    if style==1: return x*np.arctan(x)-.5*np.log1p(x*x)
    if style==2: return x*x/(np.sqrt(1+x*x)+1)
    t=np.tanh(.35)
    return (logcosh(x+.35)-logcosh(.35)-x*t)/(1-t*t)
def adaa(x,style):
    prev=np.r_[0.,x[:-1]]
    delta=x-prev
    y=curve((x+prev)/2,style)
    np.divide(primitive(x,style)-primitive(prev,style),delta,out=y,
              where=np.abs(delta)>1e-6*(1+np.maximum(np.abs(x),np.abs(prev))))
    return y
size=16384; rate=48000; k=6007
n=np.arange(4*size); x=.5*np.sin(2*np.pi*k*n/size)
cases=[]
for style in range(4):
    for drive in (24,36):
        for factor,antialias in ((32,False),(8,True),(16,True)):
            selector={32:48000,16:96000,8:192000}[factor]
            result=np.zeros_like(x); gain=10**(drive/20)
            for b in split(x,rate):
                u=up(b,selector)
                dry=down(u,selector)
                wet=down((adaa(u*gain,style) if antialias else curve(u*gain,style))/gain,selector)
                result += dry+signal.lfilter([1,-1],[1,-np.exp(-2*np.pi*5/rate)],wet-dry)
            fft=np.fft.rfft(result[-size:]); allowed=np.zeros(len(fft),bool); allowed[:2]=True
            for h in range(k,size//2+1,k): allowed[h-1:h+2]=True
            audible=(np.fft.rfftfreq(size,1/rate)>=20)&(np.fft.rfftfreq(size,1/rate)<=20000)
            ratio=np.linalg.norm(fft[audible & ~allowed])/np.linalg.norm(fft[audible & allowed])
            cases.append(dict(style=style,drive_db=drive,factor=factor,adaa=antialias,residue_dbc=float(20*np.log10(ratio))))
            print(cases[-1],flush=True)
report=dict(cases=cases, linear_adaa_at_20khz=[dict(factor=f,relative_db=float(20*np.log10(np.cos(np.pi*20000/(rate*f)))),extra_delay_host_samples=.5/f) for f in (8,16,32)])
REPORT.write_text(json.dumps(report,indent=2)+'\n')
