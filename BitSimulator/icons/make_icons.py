#!/usr/bin/env python3
# Makes every icon from the two logos in source/ (needs rsvg-convert, ImageMagick's magick, fontTools and Noto Sans Bold)
#   logo.svg, logo-square.svg      the logos with their text as outlines, so they look the same without the font
#   mark.svg                       the rising edge alone, for 16 to 48 px, where text can't be read
#   icon-<size>.png                the app icon: the mark up to 48 px, the square logo from 64 px
#   bitsimulator.ico, favicon.ico  Windows' exe icon and the browser tab icon
#   logo-dark.png, logo-light.png  the logo without its background, in each mode's colors (Theme.cpp), for the About dialog
import re, subprocess, sys
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = Path(__file__).resolve().parent
FONT = TTFont(subprocess.run(['fc-match', '-f', '%{file}', 'Noto Sans:style=Bold'], capture_output=True, text=True, check=True).stdout)
GLYPHS, CMAP, UNITS = FONT.getGlyphSet(), FONT.getBestCmap(), FONT['head'].unitsPerEm
SMALL, LARGE = (16, 24, 32, 48), (64, 128, 256, 512)

def outline(match):
    attrs, content = dict(re.findall(r'([\w-]+)="([^"]*)"', match.group(1))), match.group(2)
    scale = float(attrs['font-size']) / UNITS
    names = [CMAP[ord(ch)] for ch in content]
    advance = sum(GLYPHS[n].width for n in names) * scale
    x = float(attrs['x']) - (advance if attrs.get('text-anchor') == 'end' else 0)
    y = float(attrs['y'])
    paths = []
    for name in names:
        pen = SVGPathPen(GLYPHS)
        GLYPHS[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x, y)))
        paths.append(pen.getCommands())
        x += GLYPHS[name].width * scale
    return f'<path d="{" ".join(paths)}" fill="{attrs["fill"]}"/>'

def outlined(svg):
    return re.sub(r'<text ([^>]*)>([^<]*)</text>', outline, svg)

def render_width(svg, png, width):
    subprocess.run(['rsvg-convert', '-w', str(width), '-o', str(png), str(svg)], check=True)

# The mark: the square logo's background and signal colors, with lines thick enough for 16 px
MARK = '''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <rect width="100%" height="100%" fill="#0a0e11"/>
  <path d="M 92 380 H 252" stroke="#3b4a52" stroke-width="60" stroke-linecap="round" fill="none"/>
  <path d="M 252 380 V 132 H 420" stroke="#2dd4bf" stroke-width="60" stroke-linecap="round" stroke-linejoin="round" fill="none"/>
</svg>
'''

(HERE / 'logo.svg').write_text(outlined((HERE / 'source/logo.svg').read_text()))
(HERE / 'logo-square.svg').write_text(outlined((HERE / 'source/logo-square.svg').read_text()))
(HERE / 'mark.svg').write_text(MARK)
render_width(HERE / 'logo.svg', HERE / 'logo.png', 1200)

# Dark mode: the logo's own colors; light mode: the same roles in the light theme (text, wireHigh, a pin's grey)
LIGHT = {'#e4ebee': '#1a2328', '#2dd4bf': '#0d9488', '#3b4a52': '#a3b0b6'}
transparent = re.sub(r'<rect [^>]*/>\s*', '', (HERE / 'logo.svg').read_text())
for mode, svg in (('dark', transparent), ('light', re.sub('|'.join(LIGHT), lambda m: LIGHT[m.group(0)], transparent))):
    (HERE / f'logo-{mode}.svg').write_text(svg)
    render_width(HERE / f'logo-{mode}.svg', HERE / f'logo-{mode}.png', 900)
    (HERE / f'logo-{mode}.svg').unlink()
for size in SMALL:
    render_width(HERE / 'mark.svg', HERE / f'icon-{size}.png', size)
for size in LARGE:
    render_width(HERE / 'logo-square.svg', HERE / f'icon-{size}.png', size)
render_width(HERE / 'logo-square.svg', HERE / 'apple-touch-icon.png', 180)

def ico(name, sizes):
    subprocess.run(['magick', *[str(HERE / f'icon-{s}.png') for s in sizes], str(HERE / name)], check=True)
ico('bitsimulator.ico', (16, 24, 32, 48, 64, 128, 256))
ico('favicon.ico', (16, 32, 48))
print('icons made in', HERE)
