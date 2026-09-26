#!/usr/bin/env python3
"""Render local EBU SQAM excerpts for private R&D listening and regression.

Supply an independently downloaded SQAM FLAC archive. The audio's EBU terms
apply; this script does not redistribute the recordings. Requires system
libsndfile plus the project's development NumPy/SciPy environment.
"""
import argparse
import ctypes as ct
import ctypes.util
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[1]
TRACKS = {11: 'Double bass', 27: 'Castanets', 31: 'Cymbal',
          39: 'Grand piano', 44: 'Soprano', 49: 'Female English speech'}


class SoundInfo(ct.Structure):
    _fields_ = [('frames', ct.c_int64), ('samplerate', ct.c_int),
                ('channels', ct.c_int), ('format', ct.c_int),
                ('sections', ct.c_int), ('seekable', ct.c_int)]


def decode(path):
    library = ctypes.util.find_library('sndfile')
    if not library:
        raise RuntimeError('Install the system libsndfile development/runtime tool')
    lib = ct.CDLL(library)
    lib.sf_open.argtypes = [ct.c_char_p, ct.c_int, ct.POINTER(SoundInfo)]
    lib.sf_open.restype = ct.c_void_p
    lib.sf_readf_double.argtypes = [ct.c_void_p, ct.POINTER(ct.c_double), ct.c_int64]
    lib.sf_readf_double.restype = ct.c_int64
    lib.sf_close.argtypes = [ct.c_void_p]
    info = SoundInfo()
    handle = lib.sf_open(bytes(path), 0x10, ct.byref(info))
    if not handle:
        raise RuntimeError(f'Cannot decode {path}')
    try:
        if info.channels not in (1, 2) or info.frames <= 0:
            raise ValueError('Expected a nonempty mono/stereo recording')
        audio = np.empty((info.frames, info.channels), dtype=np.float64)
        count = lib.sf_readf_double(handle, audio.ctypes.data_as(ct.POINTER(ct.c_double)), info.frames)
        if count != info.frames or not np.isfinite(audio).all():
            raise ValueError('Incomplete or nonfinite recording')
        return info.samplerate, np.repeat(audio, 2, axis=1) if info.channels == 1 else audio
    finally:
        lib.sf_close(handle)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def db(value):
    return float(20 * np.log10(max(float(value), 1e-30)))


def render(binary, audio, rate, params):
    args = [str(binary), str(rate)]
    for key, value in params.items():
        args.extend((str(key), str(value)))
    data = subprocess.run(args, input=audio.astype('<f8').tobytes(),
                          stdout=subprocess.PIPE, check=True).stdout
    result = np.frombuffer(data, dtype='<f8').reshape(-1, 2)
    if result.shape != audio.shape or not np.isfinite(result).all():
        raise ValueError('Invalid renderer output')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'reports/saturator-cpu/program-material')
    args = parser.parse_args()
    binary = ROOT / 'build/release/saturator_render'
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sources = out / 'sources'
    sources.mkdir(exist_ok=True)
    manifest = dict(source='EBU SQAM', source_url='https://qc.ebu.io/testmaterials/523/',
                    terms='R&D use only; not for other commercial purposes. Not MIT-licensed audio.',
                    archive_sha256=sha(args.archive), render_sha256=sha(binary),
                    baseline_sha256=sha(args.baseline) if args.baseline else None,
                    input_peak_dbfs=-6, nominal_target_rms_dbfs=-24, excerpts=[])
    with zipfile.ZipFile(args.archive) as archive:
        for track, title in TRACKS.items():
            name = f'{track:02d}.flac'
            source = sources / name
            source.write_bytes(archive.read(name))
            rate, original = decode(source)
            active = np.flatnonzero(np.max(np.abs(original), axis=1) > .01)
            if not active.size:
                raise ValueError(f'No active audio: {title}')
            start = max(0, int(active[0]) - int(.1 * rate))
            audio = original[start:start + 6 * rate].copy()
            # A short edge fade avoids audition-file boundary clicks.
            fade = np.minimum(1, np.minimum(np.arange(len(audio)), np.arange(len(audio))[::-1]) / (.01 * rate))
            audio *= fade[:, None]
            gain = 10**(-6/20) / np.max(np.abs(audio))
            audio *= gain
            padded = np.pad(audio, ((0, 76), (0, 0)))
            row = dict(track=track, title=title, rate=rate, source_sha256=sha(source),
                       source_offset_seconds=start/rate, duration_seconds=len(audio)/rate,
                       source_gain_db=db(gain), edge_fade_seconds=.01, files=[])
            outputs = []
            configurations = [('dry', {3: 0})]
            for style, label in enumerate(('soft', 'rounded', 'dense', 'asymmetric')):
                drive = 24 if style == 3 else 12
                params = {6: 100} | {7+b*12+1: style for b in range(3)} | {7+b*12+2: drive for b in range(3)}
                configurations.append((f'{label}-{drive}dB', params))
            for label, params in configurations:
                # Historical comparisons explicitly retain the pre-0.2 gain
                # law; matched auditions use adaptive matching at 100%.
                # Both override the newer 0% factory compensation default.
                current_params = {43: 0 if args.baseline else 1, **params}
                raw = render(binary, padded, rate, current_params)
                residual = None
                if args.baseline:
                    old = render(args.baseline.resolve(), padded, rate, params)
                    error = raw-old
                    residual = dict(peak=float(np.max(np.abs(error))),
                                    rms_dbfs=db(np.sqrt(np.mean(error**2))))
                    if residual['peak'] > 2e-10:
                        raise AssertionError(f'Audio regression: {title}/{label}: {residual}')
                aligned = raw[76:76+len(audio)]
                rms = float(np.sqrt(np.mean(aligned**2)))
                peak = float(np.max(np.abs(resample_poly(aligned, 4, 1, axis=0))))
                outputs.append((label, current_params, aligned, rms, peak, residual))
            # All five audition files share RMS, with peak safety decided jointly.
            target = min(10**(-24/20), *(rms * 10**(-.5/20) / peak for _, _, _, rms, peak, _ in outputs))
            row['matched_rms_dbfs'] = db(target)
            for label, params, aligned, rms, peak, residual in outputs:
                match = target/rms
                filename = f'{track:02d}-{label}.wav'
                wavfile.write(out/filename, rate, (aligned*match).astype(np.float32))
                row['files'].append(dict(file=filename, params=params, match_gain_db=db(match),
                                         estimated_4x_peak_dbfs=db(peak*match), regression=residual))
            manifest['excerpts'].append(row)
            print(f'{track:02d}: {title}; {len(configurations)} finite, matched renders', flush=True)
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (out/'README.md').write_text('''# Saturator real-recording evaluation pack

Six 6-second excerpts from the EBU SQAM recordings: double bass, castanets,
cymbal, piano, soprano and English speech. Each has five stereo float WAVs:
phase-matched dry; Soft, Rounded and Dense at 12 dB drive; Asymmetric at 24 dB.
Files are latency-aligned and RMS-matched within each recording, normally at
−24 dBFS RMS. A common lower target is used if needed for estimated 4× peak
headroom. This is RMS matching, not a perceptual loudness certification.

Compare attack clarity, low end, image and high-frequency texture at a fixed
monitor level. Dry uses the plugin's phase-matched dry path. Sources are peak
normalized to −6 dBFS before processing, with short fades only at excerpt edges.
The manifest records exact source offsets, hashes, parameters, gains and
baseline-versus-optimized residuals. A numerical pass is not a listening verdict.
These files have been generated and measured; human listening is still pending.

Source and terms: https://qc.ebu.io/testmaterials/523/
EBU permits these recordings for R&D, not other commercial purposes. This local
evaluation pack and extracted sources are not shipping plugin assets, are not
MIT-licensed, and must not be repurposed as commercial demo music. The source
archive remains unchanged. Mono sources are duplicated to stereo for comparison.
''')
    print(out)


if __name__ == '__main__':
    main()
