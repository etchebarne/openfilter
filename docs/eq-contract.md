# EQ audio contract (0.3.0; legacy modes preserved)

Plugin ID: `org.openfilter.eq`. CLAP descriptor name: `OpenFilter EQ`.
The first artifact is Linux x86-64. Cross-platform source organization is present;
macOS bundles, Windows exports/builds, and their host testing remain future work.

## Parameter and routing semantics

There are 170 parameters: global Bypass (ID 0), Output dB (ID 1), and 24 bands.
Band n (zero-based) uses ID `0x100 + 16*n + field`. Never reuse reserved gaps.

| Field | Offset | Stored/CLAP unit | Range or enumeration |
| --- | --- | --- | --- |
| Enabled | 0 | integer | 0 off, 1 on |
| Type | 1 | integer | 0 bell, 1 low shelf, 2 high shelf, 3 low cut, 4 high cut, 5 notch |
| Frequency | 2 | log2(Hz) | log2(10) to log2(30000) |
| Gain | 3 | dB | −24 to +24 |
| Q | 4 | log2(Q) | log2(0.1) to log2(40) |
| Slope | 5 | integer | 0 = 12, 1 = 24, 2 = 48 dB/oct, 3 = Brickwall |
| Routing | 6 | integer | 0 stereo, 1 left, 2 right, 3 mid, 4 side |

Frequency and Q use logarithmic host values to make host knobs/modulation useful;
their display/text entry uses Hz and physical Q. +1 frequency modulation means
one octave, +1 Q modulation doubles Q. Gain/Output modulation is in dB. Only
these continuous controls advertise global modulation; there is no per-note
modulation. Stored base values and offsets remain separate.

Bands process in fixed slot order. Within each band, mid is (L+R)/2 and side is
(L−R)/2; the filtered component delta is added/subtracted back into L/R. This
defines mixed L/R and M/S ordering. In mono, stereo/left/mid process the mono
channel, while right/side have no effect.

The frequency target is preserved across rate changes, with actual processing
clamped to 0.475*sample-rate. Slope affects cuts only. For slope values 0–2, at Q=sqrt(1/2), cuts use
Butterworth section alignment; Q scales the sections, each bounded to 0.1–40.
Gain is meaningful only for bells/shelves. Shelf Q is the SVF damping control,
not a shelf-slope control. All defaults are identity: bands disabled, trim 0 dB.

## Brickwall cuts (added in 0.3.0)

Slope value 3 selects an independent 20th-order elliptic low/high cut, built from
ten double-precision SVF sections. The fixed analog prototype has 0.01 dB
passband ripple and a 100 dB stopband. Frequency denotes the passband edge
(−0.01 dB), rather than a −3 dB point. The cutoff is prewarped and then mapped
with the bilinear transform; the existing 0.475*rate clamp still applies.

For a high cut the stopband starts by 1.017 times the prewarped cutoff; for a
low cut it starts by its reciprocal. The transition in Hz becomes narrower
near Nyquist. This is a finite causal filter, not an infinite-slope mathematical
ideal. It has nonlinear phase and ringing near the edge, particularly at low
frequencies; it adds no buffered latency. Moving the cutoff also excites filter
history, so listening validation remains necessary.

Q and Gain are inactive in Brickwall cuts. Their stored values survive switching
back to other slopes/shapes. Existing routing, frequency smoothing, bypass,
modulation and slope-change fades apply. Slope 3 on a non-cut shape is inert.
The displayed curve uses the actual section transfer functions; no artificial
vertical line is substituted for the response.

Reproduce the prototype with `tools/design_brickwall.py`. Independent SciPy SOS
impulse comparisons are part of `tools/measure_eq.py`. Regression tests cover
response-versus-audio, extreme rate/cutoff automation, inactive Q, routing,
reset, CLAP text/state and sample-offset/block-partition agreement.

## Smoothing and precision

Continuous parameters ramp over 10 ms, in their stored units. Band/global wet
bypass ramps over 5 ms. Shape, slope and routing changes fade a band to dry,
clear its filter states, then fade to the new filter. This avoids abrupt state
reuse but includes a brief dry transition; it is not a morph between shapes.
Events start ramps at the supplied sample offset, independently of block size.

The DSP uses double coefficients/state and accepts both float32/float64 buffers.
It does not intentionally saturate or clip output. Exact identity applies to
normal finite samples on the fully inactive path; subnormal input/output samples
are flushed to zero. Internal states smaller than 1e-30 are cleared. No host
floating-point environment flags are changed.

Processing adds zero samples of latency. The alpha conservatively reports an
infinite recursive tail and returns CONTINUE: adaptive silence/tail estimation
is pending. It accepts finite rates from 1 kHz through 768 kHz, including
fractional rates; detailed EQ measurements currently focus on 44.1–192 kHz.

## State schema 1

Version 0.3.0 appends slope value 3 without changing any existing ID, enumeration
value, default or legacy filter equation. All previous states remain loadable
with their original sound. A state containing Brickwall requires 0.3.0 or newer;
older builds reject the out-of-range slope, rather than silently changing it.
The serialization layout is unchanged.

All integers and IEEE754 doubles are little endian. The fixed size is 2060 bytes:
8-byte ASCII magic `OFEQSTAT`, uint32 schema version 1, uint32 parameter count
170, then 170 records in parameter-index order, then uint32 FNV-1a checksum of
all preceding bytes. Each record contains uint32 parameter ID + float64 value.

Partial stream reads/writes are supported. Wrong magic/version/count/order,
non-finite or out-of-range values, non-integral enums, truncation, and checksum
mismatch fail without applying a partial state. The checksum detects accidental
corruption, not malicious modification. Channel configuration belongs to the
host and is not serialized in the parameter state. Filter memories and transient
modulation are not saved.

The alpha baseline uses bilinear-transform SVF responses. Stable IDs and schema
do not yet promise that every future alpha sounds identical. Before changing
filter design in released sessions, add a versioned algorithm mode/migration
that preserves old sound; do not silently replace baseline coefficients.
