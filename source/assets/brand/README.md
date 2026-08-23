# WinInspect Brand Assets

## Overview

```
assets/brand/
  README.md          — this file
  svg/
    strix-icon.svg   — Strix the Window Owl app icon (256×256)
    strix-logo.svg   — WinInspect logo with tagline (400×120)
    magnifier-icon.svg — standalone magnifier icon (64×64)
  png/               — PNG renderings (generated from SVGs)
  ico/               — Windows ICO files (generated from SVGs)
  social/            — GitHub social preview image
```

## Generating PNG and ICO from SVG

### Using Inkscape (cross-platform):

```bash
inkscape assets/brand/svg/strix-icon.svg -w 256 -h 256 \
  --export-filename assets/brand/png/strix-icon-256.png
```

### Using ImageMagick (for ICO):

```bash
convert assets/brand/png/strix-icon-256.png \
  \( -clone 0 -resize 16x16 \) \
  \( -clone 0 -resize 32x32 \) \
  \( -clone 0 -resize 48x48 \) \
  \( -clone 0 -resize 64x64 \) \
  \( -clone 0 -resize 128x128 \) \
  \( -clone 0 -resize 256x256 \) \
  -delete 0 assets/brand/ico/strix.ico
```

### Using Python (for ICO):

```python
from PIL import Image
img = Image.open("assets/brand/png/strix-icon-256.png")
img.save("assets/brand/ico/strix.ico", format="ICO", sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])
```

## Validation

Run `scripts/validate-brand-assets.sh` (Linux/WSL) or
`scripts/validate-brand-assets.ps1` (PowerShell) to verify all
required paths exist.
