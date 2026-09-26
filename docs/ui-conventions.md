# OpenFilter suite UI conventions

These conventions apply to every OpenFilter effect. The EQ is the first
implementation; reuse `libs/ui/Theme.hpp`, `Interaction.hpp`, drawing helpers and
native raster support when adding another effect.

## Value interactions

**Double primary click resets a control to its declared default.** Use the
parameter descriptor's `initial` value in its stored units. Do not substitute the
range midpoint, zero, the current preset, or the last saved value. This applies
to knobs, sliders, numeric readouts, selectors and parameter toggles. A reset
must send balanced CLAP begin/value/end gestures and participate in local undo.
Do not use double-click for numeric entry anywhere in the suite.

Click a numeric readout, right-click its control, Ctrl-click it, or focus it and
press Enter to type a value. A primary click opens entry on release; moving more
than five logical pixels instead starts a vertical relative drag on the same
readout. Up increases, down decreases. Pointer jitter sends no host gesture,
a drag never opens entry on release, and focus loss cancels a pending click.
This applies to every continuous numeric field in all seven plugins, including
footer trims and contextual inspectors. Enter commits, Escape cancels.
Knobs use vertical relative dragging and support scrolling; Shift makes changes finer without
jumping when the modifier changes during a drag. Units and logarithmic parameter
mappings must match the host parameter contract.

`ClickTracker` detects a pair on the same target within 320 ms and five logical
pixels. Movement beyond that threshold cancels the pair, and a completed pair
is consumed. Unrelated controls and secondary clicks cannot accidentally reset.
Hit tests use logical coordinates so this behavior survives host scaling.

In the EQ, double-clicking a node resets its frequency, gain and Q together
(1 kHz, 0 dB, Q = sqrt(0.5)). Its filter shape, routing and enabled state remain
intact. Click empty graph space once to create a band; drag immediately to place
it. Close/dismiss is separate from remove/delete. View values also reset to their
defaults: ±12 dB range and both pre/post analyzers enabled.

Commands such as Add, Delete, Undo, A/B and Copy are actions rather than value
controls; they do not gain a double-click reset action.

## Visual structure

Use a graph-first workspace with restrained grid lines, clear contrast, colored
bands and a distinct combined response. Keep the header and global footer slim.
Place contextual controls in a floating panel and hide them when nothing
is selected. Give captions, dials and values separate rows; leave clearance for
the outer scale and keyboard focus ring. Center the control group symmetrically,
and align toolbar controls and text to a common vertical center. Use visible
text bounds (`centeredText`) instead of guessed baseline offsets. Use larger gain knobs where useful, modest frequency/Q knobs, and
precise numeric readouts. Keep output controls and meters in consistent positions.

Keep the selected panel stable while dragging its knobs or editing text. Its
horizontal anchor follows selection and node drags; clamp it inside the window
at all supported sizes. Recalculate its upper/lower placement during every node
drag movement, so it moves above deep cuts before release. Keep the full drag
as one undo operation and one balanced host gesture per edited parameter. Floating controls intercept input so clicks cannot create
bands behind them. Keep editing feedback immediate, without decorative motion
on repeated audio-control gestures. Retain readable focus and hover states.

Direct graph editing, contextual controls and clear analysis define the suite
workflow. Shared graphics and implementation are original. Expose only
functionality actually implemented by each effect.

## Material and light

The suite uses a dark graphite neumorphic finish. Reuse `theme::raised`,
`theme::well`, `theme::button` and `theme::knob` from `libs/ui` rather than
inventing separate surface treatments in each plugin. Light comes from the
upper left: raised controls catch a soft upper-left highlight and cast a
lower-right shadow; recessed fields reverse that edge lighting. The graph is
the continuous main surface between header and footer, with no inset frame or
outer gutter or darkened Nyquist strip. Keep frequency labels inside the canvas and view-range controls
in the header. Clip off-screen responses rather than flattening them against
the plot edge, which would create a false response and an apparent border.

Use elevation to express interaction. Selectors, rotary controls and toolbar
commands are raised; active toggles use a recessed surface with an accent mark.
Panel numeric readouts and global output trim are always inset so their
editability is visible before hovering. Floating
panels and menus cast a broader shadow than individual controls. Fine dividers
separate groups without drawing boxes around every section. Knobs have a
recessed scale, a convex cap, a bright pointer, and an explicit focus ring.

Depth is secondary to legibility. Keep labels and values high-contrast and
reserve colored accents for band identity, parameter amount, selection and
focus. Do not use shadow or a subtle hue shift as the only state indicator.
Unavailable fields (Gain for cuts/notches; Q for Brickwall) show a dash, dim
their scale, ignore input and are skipped by keyboard focus. Stored parameter
values remain intact. Keep separate, labeled EQ-gain and meter dBFS scales.
Compact layouts may omit secondary status text to protect control spacing.
Enabled/bypassed nodes differ in fill as well
as color. Filter silhouettes are labels for the shape selector, not previews
of the current gain/Q. Keep explanatory reset text in Help and the empty state.

These are native Cairo vectors. Shadows use four bounded stroke layers, with
no bitmap assets, blur buffers, new dependencies, or decorative animation.
The reverb signal-history layer is a deliberate exception: its diffuse tail
display uses a reusable downsampled alpha mask and a bounded blur on the UI
thread. Controls, text, meters and editable curves remain crisp vectors.
Paint work stays on the main thread. Measure dense-band paint cost when changing
shared materials, and inspect them at normal and 2× scale.

## Wordmarks

Use the approved outlined assets in `assets/branding` for product identity.
The EQ and compressor headers use `ui::drawBrand` at the same visible height,
with ivory lettering and cyan EQ / amber C accents. Preserve aspect ratio,
optical spacing and the centered i dot; do not recreate the logo with system
text or add an icon. Native Cairo paths are embedded from the SVG sources.

## Meter styling

Reuse `meterBar` from `libs/ui/include/openfilter/ui/Meters.hpp` for meter lanes.
The EQ and compressor share narrow recessed wells, six-pixel segmentation and
the green-to-gold level gradient. Keep peak readouts above the lanes and dBFS
labels below. Gain reduction uses the same material with a warm fill growing
downward and a separate positive dB scale; it is not a signal level. Meter
ballistics remain the responsibility of each effect. Match the suite wordmark,
header/footer gradients, toolbar icons and inset bold numeric readouts as well
as the rotary control material.

## Verification

Test parameter resets, undo, exact entry, menu/toggle resets and native host
recording of gestures. Test editor open/close, keyboard interaction and input
through floating controls. Render and inspect empty, selected, disabled, menu,
entry, compact, HiDPI and dense-band states. Record measured and unverified
behavior separately in status.md.
