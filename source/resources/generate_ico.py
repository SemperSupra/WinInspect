#!/usr/bin/env python3
"""Generate a placeholder ICO file for WinInspect.

Replace with the proper Strix icon once SVG rendering tools are available:
  inkscape --export-filename=assets/brand/ico/strix.ico assets/brand/svg/strix-icon.svg
"""
import struct, zlib, os

def create_png(width, height, r, g, b):
    raw = b''
    for y in range(height):
        raw += b'\x00'
        for x in range(width):
            raw += struct.pack('BBB', r, g, b)

    def chunk(ctype, data):
        c = ctype + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)

    ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(raw)) +
            chunk(b'IEND', b''))

ico_dir = os.path.join(os.path.dirname(__file__), '..', 'assets', 'brand', 'ico')
os.makedirs(ico_dir, exist_ok=True)

# Create multi-resolution ICO with blue placeholder (will be replaced with Strix)
sizes = [16, 24, 32, 48, 64]
images = [create_png(s, s, 21, 101, 192) for s in sizes]

ico_path = os.path.join(ico_dir, 'strix.ico')
with open(ico_path, 'wb') as f:
    f.write(struct.pack('<HHH', 0, 1, len(sizes)))
    offset = 6 + len(sizes) * 16
    for i, (s, png) in enumerate(zip(sizes, images)):
        f.write(struct.pack('<BBBBHHII', s, s, 0, 0, 1, 32, len(png), offset))
        offset += len(png)
    for png in images:
        f.write(png)

print(f'Created {ico_path} with {len(sizes)} resolutions')
