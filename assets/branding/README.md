# OpenFilter wordmarks

Standalone, transparent SVGs exported from the approved Figma artwork. Paths
are outlined: no font installation, linked images or presentation backgrounds
are needed. The viewBox tightly encloses the visible artwork; preserve its
aspect ratio and provide surrounding clearance in the layout.

| File | Product | Suffix |
| --- | --- | --- |
| `openfilter.svg` | Suite master | — |
| `openfilter-eq.svg` | Equalizer | EQ |
| `openfilter-compressor.svg` | Compressor | C |
| `openfilter-limiter.svg` | Limiter | L |
| `openfilter-reverb.svg` | Reverb | R |
| `openfilter-multiband-compressor.svg` | Multiband compressor | MB |
| `openfilter-de-esser.svg` | De-esser | DS |
| `openfilter-gate.svg` | Gate | G |
| `openfilter-saturator.svg` | Saturator | S |

The ivory lettering is intended for dark surfaces. Each product's accent is
shared by its suffix and centered i dot. Preserve the optical letter spacing
and custom e outlines. For a monochrome application, replace both fill colors
with the same foreground color.

Source: [OpenFilter — Product logo family in Figma](https://www.figma.com/design/spqnA9phFHLQa8C1GcwpkE/Playground?node-id=762-2186).
Exported 2026-09-23 with layer IDs enabled; only the named `OpenFilter-*-logo`
groups were retained. The suite master uses the same prefix with its green dot.

The wordmarks derive from Unbounded at weight 600 with custom lettering and
spacing. Font copyright and SIL Open Font License are retained in
`Unbounded-OFL.txt`; no font binary is bundled. These logo outlines are artwork,
not an installable modified font.

## Native plugin integration

EQ, compressor and limiter use `libs/ui/include/openfilter/ui/Brand.hpp`. Their Cairo
paths are generated from these SVGs and compiled into the plugins; no runtime
file reads, SVG dependencies or bitmap scaling are used. After changing those
three SVGs, regenerate and format the embedded paths:

```sh
python3 tools/generate_brand_paths.py
.venv/bin/clang-format -i libs/ui/include/openfilter/ui/BrandPaths.hpp
```

The generator intentionally supports only the flattened absolute paths and
solid colors used by these exports. Other SVG features fail explicitly.
