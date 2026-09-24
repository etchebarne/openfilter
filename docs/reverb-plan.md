# Reverb plan and initial audio contract

Linux / Bitwig, CLAP only. Plugin ID `org.openfilter.reverb`, state magic
`OFRVSTAT`, schema 1. Permanent IDs are declared in Parameters.hpp. Existing
plugins and their states are unchanged. This is a first algorithmic reverb alpha,
not a sonic match to FabFilter Pro-R 2 or a qualified production reverb.

## Implementation milestone

An original eight-delay orthogonal feedback delay network (normalized Hadamard),
stereo injection/output vectors, four input allpass diffusers per channel,
fractional predelay, modulated tank delays, frequency-dependent loop damping,
and six stereo post-EQ bands. Allocate nothing during processing, parameter
flush or reset. Fixed storage supports 1–768 kHz; unsupported rates fail activation.
Dry path has zero latency, including bypass. Input trim affects wet only; output
trim affects the mixture. Mix is linear dry/wet. Mono input excites both channels;
mono output folds the wet result. Sanitize non-finite input.

Space (0.2–10 seconds) sets nominal T60 and delay geometry. Decay Rate scales it
25–400%. Character controls modulation and early reflection contribution.
Thickness sets input diffusion; Distance reduces early reflections and increases
buildup. Brightness applies a high-frequency feedback shelf (neutral at 100%).
Modern / Vintage / Plate are original diffusion/modulation voicings, not models
of commercial hardware. Width is wet mid/side scaling 0–200%. Ducking follows
input with a 5 ms attack / 250 ms release, up to 24 dB attenuation. Auto Gate
opens above -45 dBFS with an adjustable hold and 5/80 ms attack/release.
Freeze fades out injection and loss; fractional-delay interpolation can still
lose high-frequency energy. It is a sustained-tail effect, not guaranteed
lossless infinite storage. The host receives an infinite tail conservatively,
including when freeze can be automated. Processing does not sleep prematurely.

Six decay bands use bell/low-shelf/high-shelf loss filters inside each feedback
line. For line duration d and nominal T, base loss is 60*d/T dB; a ratio r adds
60*d/T*(1-1/r) dB in that band's shape. Summed positive contributions are scaled
to at most 75% of base loss, keeping stationary loop gain below unity even for
overlapping boosts. Shelves use Butterworth Q. This is approximate decay shaping;
overlapping boosts are limited and the graph displays the resulting nominal
loop-loss ratio, not an exact measured room T60. Six post bands provide bell,
shelves, 12 dB/oct cuts and notch, ±24 dB, without automatic gain compensation.

All continuous parameters ramp for 20 ms, with tank geometry slewed over 100 ms.
Coefficients refresh on an absolute 32-sample clock independent of host block
partitioning; gain/mix/bypass update every sample. Filter-type changes remain
an alpha transition limitation. Modulation never overwrites stored values.
Tempo sync supports quarter/eighth/sixteenth/thirty-second predelay, 50–200%
offset, capped at the 500 ms delay capacity; fallback tempo is 120 BPM.
Transport events apply at sample offsets. Gate hold is in milliseconds.

## Editor and validation

Large central Space control and continuous dual decay/post frequency canvas;
original approved R wordmark and shared graphite/raised suite materials.
Editable nodes, band inspector, exact entry, descriptor-default double-click,
undo/redo, A/B, starting presets and mix lock. Lock affects preset loading only.
Live wet/output spectrum uses the existing bounded audio tap and main-thread FFT.
The wet tap follows Post EQ, width, ducking and gating before Mix/output trim.
A bounded 56-slice history retains 2.2 seconds of captured wet spectra, with
visibility fading by sample-clock age. Historical levels are not extrapolated
from parameter values. Rendering selects at most 12 historical contours.
The UI composites these as broad strokes through a reusable alpha mask at 1/2.7
logical resolution, with three separable binomial blur passes. Host scale
does not change the logical blur width; quiet contours fall below the plot
instead of flattening into a glowing floor. The final output/EQ stay sharp.
The UI layout separates controls, captions and exact-value fields; geometry
regressions cover compact/normal/wide layouts and 0.75–3x scaling. Native X11 embedding with queued begin/value/end
and output-event backpressure preserves the established plugin ownership model.

Validate actual impulse responses, decay estimates, finite tails/extremes,
reset/dry/bypass, rates, stereo, freeze and ducking. Independent Python audio
measurements, bit-identical host event partitioning, state validation, allocation
guards, CTest, clap-validator, sanitizers, GUI tests and rendered previews.
Bitwig listening, automation, recall/export and subjective voicing remain manual
gates. Future parity work includes IR import, surround,
automatic post-EQ gain compensation, steeper post cuts, gate tempo sync and
more extensive preset/listening qualification.

## References

Workflow/control reference: https://www.fabfilter.com/help/pro-r/using/maincontrols
and the official Decay Rate EQ / Post EQ help. No FabFilter source or graphics
are included. Feedback delay network background: Julius O. Smith, Physical Audio
Signal Processing, https://ccrma.stanford.edu/~jos/pasp/Feedback_Delay_Networks_FDN.html .
Existing SVF primitive retains its Andrew Simper attribution.
