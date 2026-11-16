#!/usr/bin/env python3
"""Generates Resources/icon.svg: FmControl's icon in the DOS-tracker style.

A pixel-art OPL3-style chip with "FM" and "OPL3" in the app's own 5x7 pixel
font, on a dithered VGA-blue tile.
Render with: rsvg-convert -w 1024 -h 1024 Resources/icon.svg > Resources/icon.png
"""
import pathlib

BG, CHIP, EDGE = "#10104a", "#1a1a2e", "#3a3ab8"
CYAN, YELLOW, BLACK, NOTCH = "#55ffff", "#ffff55", "#000008", "#0a0a2a"
GLYPHS = {
    'F': ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    'O': ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    'P': ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    'L': ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    '3': ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
}

out = []
def rect(x, y, w, h, fill, extra=""):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" {extra}/>')

def width(s, px):
    return (len(s) * 6 - 1) * px

def text(s, x, y, px, colour, shadow=None, offset=0):
    for is_shadow in ([True, False] if shadow else [False]):
        cx = x
        for ch in s:
            for r, row in enumerate(GLYPHS[ch]):
                for c, bit in enumerate(row):
                    if bit == "1":
                        d = offset if is_shadow else 0
                        rect(cx + c * px + d, y + r * px + d, px, px, shadow if is_shadow else colour)
            cx += 6 * px

out.append('<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">')
out.append('<defs><clipPath id="body"><rect x="100" y="100" width="824" height="824" rx="185"/></clipPath>'
           '<pattern id="dither" width="16" height="16" patternUnits="userSpaceOnUse">'
           f'<rect x="0" y="0" width="4" height="4" fill="{EDGE}" opacity="0.35"/>'
           f'<rect x="8" y="8" width="4" height="4" fill="{EDGE}" opacity="0.35"/></pattern></defs>')
out.append('<g clip-path="url(#body)">')
rect(100, 100, 824, 824, BG)
rect(100, 100, 824, 824, "url(#dither)")

# The chip: 2 x 8 pins with shadows, body with a hard shadow and pin-1 notch.
cx, cy, cw, ch = 232, 300, 560, 424
for i in range(8):
    x = cx + 44 + i * 64
    for y in (cy - 56, cy + ch):
        rect(x + 12, y + 12, 36, 56, BLACK)
        rect(x, y, 36, 56, CYAN)
rect(cx + 24, cy + 24, cw, ch, BLACK)
rect(cx, cy, cw, ch, CHIP)
rect(cx, cy, cw, ch, "none", f'stroke="{EDGE}" stroke-width="8"')
out.append(f'<circle cx="{cx + 60}" cy="{cy + 60}" r="18" fill="{NOTCH}"/>')

text("FM", cx + (cw - width("FM", 30)) // 2, cy + 60, 30, YELLOW, BLACK, 10)
text("OPL3", cx + (cw - width("OPL3", 14)) // 2, cy + ch - 130, 14, CYAN)

out.append('</g>')
out.append(f'<rect x="100" y="100" width="824" height="824" rx="185" fill="none" stroke="{EDGE}" stroke-width="6"/>')
out.append('</svg>')

path = pathlib.Path(__file__).resolve().parent.parent / "Resources" / "icon.svg"
path.write_text("\n".join(out) + "\n")
print("wrote", path)
