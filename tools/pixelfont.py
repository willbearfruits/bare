#!/usr/bin/env python3
"""BARE!'s screen font, Terminus, as vector pixels: a word becomes an SVG whose squares are the font's pixels, so it
stays sharp printed at any size (the manual's and the page's headings). Terminus is SIL OFL 1.1 (see NOTICE.md).
Usage: pixelfont.py TEXT [FONT] > out.svg      FONT: a PSF2 console font, default ter-u32b (16x32, bold)"""
import gzip, os, struct, sys

FONT_DIR = '/usr/share/kbd/consolefonts'
_cache = {}

def load(name='ter-u32b'):
    """(width, height, {char: rows}) — each row a list of booleans, left to right"""
    if name in _cache: return _cache[name]
    path = name if os.path.exists(name) else f'{FONT_DIR}/{name}.psf.gz'
    raw = gzip.open(path).read() if path.endswith('.gz') else open(path, 'rb').read()
    magic, _, hsize, flags, count, charsize, height, width = struct.unpack('<8I', raw[:32])
    if magic != 0x864AB572: raise ValueError(f'{path}: not a PSF2 font')
    rowbytes = (width + 7) // 8
    def bitmap(i):
        g = raw[hsize + i * charsize: hsize + (i + 1) * charsize]
        return [[bool(g[r * rowbytes + x // 8] >> (7 - x % 8) & 1) for x in range(width)] for r in range(height)]
    glyphs = {}
    if flags & 1:                                   # the unicode table: each glyph's characters, 0xFE sequences, 0xFF
        p = hsize + count * charsize
        for i in range(count):
            end = raw.index(b'\xff', p)
            chars = raw[p:end].split(b'\xfe')[0].decode('utf-8', 'replace')
            for c in chars: glyphs.setdefault(c, i)
            p = end + 1
    else:
        glyphs = {chr(i): i for i in range(min(count, 256))}
    font = (width, height, {c: bitmap(i) for c, i in glyphs.items()})
    _cache[name] = font
    return font

def path(text, name='ter-u32b', spacing=0):
    """the text's pixels as one SVG path, and its size in font pixels"""
    w, h, g = load(name)
    d = []
    for k, ch in enumerate(text):
        rows = g.get(ch) or g.get('?')
        x0 = k * (w + spacing)
        for y, row in enumerate(rows):
            x = 0
            while x < w:
                if not row[x]: x += 1; continue
                run = x
                while run < w and row[run]: run += 1
                d.append(f'M{x0 + x} {y}h{run - x}v1h-{run - x}z')
                x = run
    return ''.join(d), len(text) * (w + spacing) - spacing, h

def svg(text, name='ter-u32b', cls='px', title=None, trim=True):
    """an inline SVG of the text, filled with currentColor; its height set by CSS (em), its width follows"""
    d, width, height = path(text, name)
    top, bottom = 0, height
    if trim:                                        # the font's empty rows above the capitals and below the descenders
        ys = [int(s.split(' ')[1].split('h')[0]) for s in d.split('M')[1:]]
        if ys: top, bottom = min(ys), max(ys) + 1
    label = title or text
    return (f'<svg class="{cls}" viewBox="0 {top} {width} {bottom - top}" role="img" aria-label="{label}" '
            f'xmlns="http://www.w3.org/2000/svg" shape-rendering="crispEdges"><path fill="currentColor" d="{d}"/></svg>')

if __name__ == '__main__':
    print(svg(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'ter-u32b'))
