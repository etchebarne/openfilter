#!/usr/bin/env python3
"""Additional release gates: actual audio transfer, oversampling convergence,
long reconstruction stress, and an external libebur128 1.2.6 cross-check.

libebur128 is a test-only system dependency; it is not linked into the plugin.
No subjective listening or formal meter conformance claim is made.
"""
import ctypes as C
import ctypes.util
import json
from pathlib import Path
import numpy as np
from scipy.signal import firwin, resample_poly, fftconvolve
from measure_limiter import render, db, latency_for

ROOT = Path(__file__).resolve().parents[1]


class ExternalMeter:
    def __init__(self):
        name = ctypes.util.find_library('ebur128')
        if not name:
            raise RuntimeError('Install libebur128 1.2.6 for the independent meter gate')
        self.lib = lib = C.CDLL(name)
        lib.ebur128_init.argtypes = [C.c_uint, C.c_ulong, C.c_int]
        lib.ebur128_init.restype = C.c_void_p
        lib.ebur128_add_frames_double.argtypes = [C.c_void_p, C.POINTER(C.c_double), C.c_size_t]
        lib.ebur128_true_peak.argtypes = [C.c_void_p, C.c_uint, C.POINTER(C.c_double)]
        lib.ebur128_destroy.argtypes = [C.POINTER(C.c_void_p)]
        version = [C.c_int() for _ in range(3)]
        lib.ebur128_get_version(*[C.byref(v) for v in version])
        self.version = '.'.join(str(v.value) for v in version)
        assert self.version == '1.2.6', self.version

    def peak(self, x, rate):
        x = np.ascontiguousarray(np.pad(x, ((0, 1024), (0, 0))), dtype=np.float64)
        state = C.c_void_p(self.lib.ebur128_init(x.shape[1], rate, 49))
        assert state.value
        try:
            assert self.lib.ebur128_add_frames_double(state, x.ctypes.data_as(C.POINTER(C.c_double)), len(x)) == 0
            peaks = []
            for c in range(x.shape[1]):
                value = C.c_double()
                assert self.lib.ebur128_true_peak(state, c, C.byref(value)) == 0
                peaks.append(value.value)
            return max(peaks)
        finally:
            self.lib.ebur128_destroy(C.byref(state))


def reconstructed_peak(x, factor, h):
    # Independent full linear convolution, including filter tails. FFT evaluation
    # avoids the cost of billions of multiplies in the longest stress kernels.
    up = np.zeros((len(x)*factor, x.shape[1]))
    up[::factor] = x
    return float(abs(fftconvolve(up, h[:,None]*factor, axes=0)).max())


def main():
    external = ExternalMeter()
    transfer = []
    for rate in [44100, 48000, 96000, 192000, 384000, 768000]:
        x = np.zeros((16000, 2)); x[0] = .001
        y = render(x, rate, {2:0})[:,2] / .001
        assert np.argmax(abs(y)) == latency_for(rate)
        assert abs(y.sum()-1) < 1e-12
        response = abs(np.fft.rfft(y, 262144))
        frequency = np.fft.rfftfreq(262144, 1/rate)
        passband = response[(frequency >= 20) & (frequency <= 20000)]
        ripple = float(abs(20*np.log10(passband)).max())
        rejection = db(response[-1])
        assert ripple < .002, (rate, ripple)
        assert rejection < -110, (rate, rejection)
        transfer.append(dict(rate=rate,latency=latency_for(rate),maximum_passband_deviation_db=ripple,nyquist_db=rejection))

    # Same production gain computer at 1x, 4x and an independent 16x-rate
    # reference. A continuously increasing carrier keeps the peak stage working;
    # a steady sine would settle to constant gain and conceal aliasing behavior.
    # Use independent SciPy decimation, compare after scalar level matching.
    # This measures total in-band convergence error (including envelope sampling),
    # not an assertion that every residual component is an alias.
    convergence = []
    rate = 48000
    for frequency in [1000,3000,7000,11000,15000,17000,19000]:
        versions = []
        for factor in [1,4,16]:
            t = np.arange(round(rate*factor*.65))/(rate*factor)
            source = 2*np.exp(8*t)*np.sin(2*np.pi*frequency*t+.4)
            y = render(np.column_stack([source]*2),rate*factor,{7:2,8:0,9:1000,11:0,2:0},'core')[:,2]
            if factor > 1:
                y = resample_poly(y,1,factor,window=firwin(256*factor+1,.94/factor,window=('kaiser',12)))
            versions.append(y[12000:28800])
        errors = []
        for y in versions[:2]:
            scale = np.dot(versions[2],y)/np.dot(y,y)
            errors.append(db(np.linalg.norm(scale*y-versions[2])/np.linalg.norm(versions[2])))
        assert errors[1] < -60, (frequency,errors)
        assert errors[1] < errors[0]-1, (frequency,errors)
        convergence.append(dict(frequency=frequency,native_residual_db=errors[0],four_x_residual_db=errors[1],improvement_db=errors[0]-errors[1]))

    meters = []
    for rate in [44100,48000,96000,192000]:
        for ratio in [.025,.0625,.125,.25,1/3,.4,.45,.49]:
            for phase in [0,np.pi*.37]:
                n = np.arange(12000)
                x = .7*np.sin(2*np.pi*ratio*n+phase)
                taper = np.sin(np.linspace(0,np.pi/2,1000))**2
                x[:1000] *= taper; x[-1000:] *= taper[::-1]
                x = np.pad(x,(0,5000))
                # Bypass isolates the actual output meter from gain/filter stages;
                # tapered edges keep boundary ringing out of sine calibration.
                y = render(np.column_stack([x]*2),rate,{0:1},'meter')
                measured = float(y[:,4].max())
                truth = reconstructed_peak(y[:,2:4],64,firwin(32769,1/64,window=('kaiser',12)))
                other = external.peak(y[:,2:4],rate)
                # libebur128 1.2.6 uses a short Hann FIR and reduces interpolation
                # at high rates. It under-reads some HF sine fixtures (e.g. .4 Fs).
                # Do not tune our meter to that error: analytical amplitude and
                # long 64x reconstruction define accuracy. Keep external deltas,
                # and reject an unexplained external over-read of our estimate.
                assert db(other/measured) < .2, (rate,ratio,phase,db(other/measured))
                assert abs(db(measured/truth)) < .18, (rate,ratio,phase,db(measured/truth))
                analytic_error = db(measured/.7)
                assert -.18 <= analytic_error <= .02, (rate,ratio,phase,analytic_error)
                meters.append(dict(rate=rate,ratio=ratio,phase=phase,our_peak_dbfs=db(measured),analytic_error_db=analytic_error,
                                   reconstructed_dbfs=db(truth),external_dbfs=db(other),external_minus_ours_db=db(other/measured)))

    peaks = []
    rng = np.random.default_rng(530)
    n = np.arange(8000)
    signals = {
        'gated-nyquist': np.cos(np.pi*n),
        'near-nyquist': np.cos(2*np.pi*.499*n),
        'hf-bursts': np.sin(2*np.pi*.413*n)*(n%401<60),
        'quarter-rate': np.sin(2*np.pi*.25*n+np.pi/4),
        'independent-noise': rng.normal(0,3,(len(n),2)),
        'alternating-impulses': np.where(n%113==0,(-1.)**(n//113),0),
    }
    kernels = [firwin(t,1/32,window=('kaiser',12)) for t in [8193,16385,32769,65537]]
    for rate in [44100,48000,96000,192000]:
        for name, source in signals.items():
            x = source if source.ndim==2 else np.column_stack([source]*2)
            x = np.pad(x,((2000,5000),(0,0)))
            for style in range(4):
                y = render(x,rate,{1:18,7:style,8:0,4:37,10:61})[:,2:4]
                reconstructed = [db(reconstructed_peak(y,32,h)) for h in kernels]
                other = db(external.peak(y,rate))
                assert max(reconstructed+[other]) <= -.99, (rate,name,style,reconstructed,other)
                peaks.append(dict(rate=rate,signal=name,style=style,reconstructed_dbfs=reconstructed,external_dbfs=other))
    report = dict(external_library='libebur128 '+external.version,transfer=transfer,
                  convergence=convergence,meter_cases=meters,long_kernel_peak_cases=peaks,
                  qualification='Numerical gates only; Bitwig and human listening reserved for user.')
    directory = ROOT/'reports/limiter-0.3'
    directory.mkdir(parents=True,exist_ok=True)
    (directory/'quality.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    main()
