#!/usr/bin/env python3
"""Reproduce source comparisons without installing third-party plugins.

Downloads pinned MIT Airwindows sources to ignored reports, compiles the exact
Drive/Density double-precision processing functions in an offline harness, and
renders level-matched private SQAM auditions. These are different designs and
settings, not emulation targets or a blind sound-quality ranking.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly
from saturator_program_material import decode, render, db, sha

ROOT=Path(__file__).resolve().parents[1]
REV='d22a25b7f7c0c8c05e9f3ae480e7f1da9769f49d'
BASE=f'https://raw.githubusercontent.com/airwindows/airwindows/{REV}/'

def build_reference(out):
    source=out/'sources'/'airwindows'/REV;source.mkdir(parents=True,exist_ok=True)
    records=[]
    for path in ['LICENSE']+[f'plugins/LinuxVST/src/{name}/{name}{suffix}'
                             for name in ('Drive','Density') for suffix in ('.cpp','Proc.cpp','.h')]:
        file=source/('airwindows-'+Path(path).name)
        if not file.exists():file.write_bytes(urllib.request.urlopen(BASE+path,timeout=30).read())
        records.append(dict(url=BASE+path,sha256=sha(file)))
    code='#include <cmath>\n#include <cstdint>\n#include <cstdio>\n#include <cstdlib>\n#include <string>\nusing VstInt32=int;\n'
    for name in ('Drive','Density'):
        code+='''class NAME { public:
        double sampleRate=48000; float A=0,B=0,C=1,D=1;
        double iirSampleAL=0,iirSampleBL=0,iirSampleAR=0,iirSampleBR=0;
        bool fpFlip=true; uint32_t fpdL=32771,fpdR=65537;
        double getSampleRate() const {return sampleRate;}
        void processDoubleReplacing(double**,double**,VstInt32);
        };\n'''.replace('NAME',name)
        original=(source/f'airwindows-{name}Proc.cpp').read_text()
        # Preserve the upstream function verbatim, including attribution in
        # the downloaded source. Only the VST shell is replaced by a CLI.
        code+=original[original.index(f'void {name}::processDoubleReplacing'):]
    code+='''
    int main(int argc,char**argv) {
        if(argc!=4) return 1;
        Drive drive; Density density;
        drive.sampleRate=density.sampleRate=std::atof(argv[2]);
        drive.A=density.A=std::atof(argv[3]);
        const bool useDrive=std::string(argv[1])=="Drive";
        double row[2];
        while(std::fread(row,sizeof(double),2,stdin)==2) {
            double* channels[]{row,row+1};
            if(useDrive)drive.processDoubleReplacing(channels,channels,1);
            else density.processDoubleReplacing(channels,channels,1);
            if(std::fwrite(row,sizeof(double),2,stdout)!=2)return 2;
        }
    }
    '''
    wrapper=source/'airwindows-offline.cpp';wrapper.write_text(code)
    binary=out/'airwindows-render'
    subprocess.run(['c++','-O3','-std=c++20',str(wrapper),'-o',str(binary)],check=True)
    return binary,records

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sources',type=Path,default=ROOT/'reports/saturator-drive/listening/sources')
    ap.add_argument('--output',type=Path,default=ROOT/'reports/saturator-research')
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    reference,records=build_reference(out)
    binary=ROOT/'build/release/saturator_render'
    report=dict(airwindows_revision=REV,sources=records,render_sha256=sha(binary),
                reference_sha256=sha(reference),reference_settings={'Drive':.85,'Density':.6},
                method='Native-rate upstream double processing, HP off, Output/Wet=1, fixed PRNG seeds. '
                       'OpenFilter uses 3 bands with Auto level On and 100% compensation. Output RMS matched; these settings '
                       'are illustrative and are not calibrated as equivalent amounts of distortion.',
                material='EBU SQAM: private R&D only. Audio not redistributed with the source.',clips=[])
    listening=out/'listening';listening.mkdir(exist_ok=True)
    for track in (11,27,31,39,44,49):
        source=args.sources/f'{track:02d}.flac';rate,original=decode(source)
        active=np.flatnonzero(np.max(np.abs(original),axis=1)>.01)
        start=max(0,int(active[0]) - int(.02*rate))
        x=original[start:start+6*rate].copy()
        # Use a stem-level input, not a near-full-scale oscillator. Shared input
        # gain and short edge fades, with every operation recorded for recall.
        input_gain=10**(-12/20)/np.max(np.abs(x));x*=input_gain
        fade=np.linspace(0,1,int(.01*rate));x[:len(fade)]*=fade[:,None];x[-len(fade):]*=fade[::-1,None]
        padded=np.pad(x,((0,76),(0,0)))
        outputs={'dry':x}
        for name,style in (('soft',0),('punch',4),('color',5)):
            p={6:100,43:1}|{7+b*12+1:style for b in range(3)}|{7+b*12+2:12 for b in range(3)}
            outputs[name]=render(binary,padded,rate,p)[76:]
        for name,setting in report['reference_settings'].items():
            raw=subprocess.run([str(reference),name,str(rate),str(setting)],input=x.astype('<f8').tobytes(),stdout=subprocess.PIPE,check=True).stdout
            outputs['airwindows-'+name.lower()]=np.frombuffer(raw,dtype='<f8').reshape(-1,2)
        # Common target prevents a high-crest-factor clip being peak clipped.
        target=min(10**(-24/20),*(.8*np.sqrt(np.mean(y*y))/np.max(np.abs(resample_poly(y,4,1,axis=0))) for y in outputs.values()))
        row=dict(track=track,source_sha256=sha(source),rate=rate,offset_seconds=start/rate,
                 input_gain_db=db(input_gain),duration=len(x)/rate,edge_fade_seconds=.01,files=[])
        for name,y in outputs.items():
            assert np.isfinite(y).all()
            rms=np.sqrt(np.mean(y*y));gain=target/rms;file=listening/f'{track:02d}-{name}.wav'
            wavfile.write(file,rate,(y*gain).astype(np.float32))
            row['files'].append(dict(file=file.name,sha256=sha(file),rms_before_dbfs=db(rms),
                                     matching_gain_db=db(gain),crest_db=db(np.max(np.abs(y))/rms)))
        report['clips'].append(row)
        (listening/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
        print('Source audition rendered',track,flush=True)
    # A short three-way audition with identical excerpt and measured level.
    parts=[]
    for name in ('dry','punch','color'):
        rate,y=wavfile.read(listening/f'11-{name}.wav');parts.extend([y[:rate*4],np.zeros((rate//3,2),dtype=np.float32)])
    wavfile.write(listening/'bass-dry-punch-color.wav',rate,np.concatenate(parts))
    (listening/'README.md').write_text('Private EBU SQAM R&D auditions. Each track has dry, Soft/Punch/Color at 12 dB, '
       'and actual Airwindows Drive (A=.85) / Density (A=.6) source renders. All share the same input; '
       'outputs are RMS matched. These are different multiband/full-band processors, not equivalent settings. '
       'The combined bass file plays dry, Punch, Color, four seconds each separated by silence. '
       'No human listening verdict is implied. Source/settings/gains/hashes are in manifest.json.\n')

if __name__=='__main__':main()
