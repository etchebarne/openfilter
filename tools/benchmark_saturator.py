#!/usr/bin/env python3
"""Serial, actual-CLAP callback measurements. Run without other builds/tests.
Optional first argument is the preserved older CLAP for before/after comparison.
Results are an unpaced desktop probe, not a DAW scheduling certification.
"""
import datetime
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT/'reports/saturator-cpu/callbacks.json'
variants = [('optimized', ROOT/'build/release/plugins/OpenFilterSaturator.clap')]
if len(sys.argv)>1:
    variants.insert(0, ('baseline', Path(sys.argv[1]).resolve()))
report = dict(timestamp=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              platform=platform.platform(), cpu=next((line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')), ''),
              binaries={name:dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for name,path in variants}, cases=[])
# rate, block, duration, instances, workload. Three repeated normal cases expose
# run-to-run variance. Higher-rate and multi-instance cases use actual callbacks.
cases = [(48000,64,10,1,'default')]*3
cases += [(48000,64,10,1,m) for m in ('dense','automated','silence','mono')]
cases += [(44100,32,10,1,'default'), (96000,64,10,1,'default'), (192000,128,10,1,'default')]
cases += [(48000,64,15,4,'default'), (48000,64,15,4,'automated')]
for case in cases:
    for name,path in variants:
        row = json.loads(subprocess.check_output([str(ROOT/'build/release/saturator_host_benchmark'),str(path),*map(str,case)],text=True))
        row['variant']=name
        report['cases'].append(row)
        REPORT.write_text(json.dumps(report,indent=2)+'\n')
        print(name,case,'CPU',row['cpu_percent'],'p99 us',row['cpu_p99_us'],flush=True)
# Longer optimized-only stress with every band's style/drive/tone/dynamics and
# crossover events at mid-block offsets; output checked finite, allocation guard on.
row=json.loads(subprocess.check_output([str(ROOT/'build/release/saturator_host_benchmark'),str(variants[-1][1]),'48000','64','120','1','automated'],text=True))
row['variant']='optimized'; row['soak']=True
report['cases'].append(row)
REPORT.write_text(json.dumps(report,indent=2)+'\n')
print('120-second automated soak:',row,flush=True)
