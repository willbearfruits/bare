#!/usr/bin/env python3
"""Contact sheets for `make theme-shots`: one per colour scheme (its pages side by side), one of every scheme's PLAY
page, and index.html to look through the full-size pictures (serve the directory: python3 -m http.server).
Usage: theme_sheets.py DIR   (DIR holds N_page.png from test/shots with HOST_THEME=N)"""
import os, re, sys
from PIL import Image, ImageDraw, ImageFont

d = sys.argv[1]
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'core', 'gfx.c')).read()
names = re.findall(r'^    \{ "([A-Z ]+)", /\* (.*?) \*/', src, re.M)
PAGES = ['play', 'seq', 'wave', 'wave-sample', 'stretch', 'fm', 'tape-playing', 'mix', 'touch', 'fx', 'file']
TW, TH = 640, 400
font = None
for f in ('/usr/share/fonts/noto/NotoSans-Regular.ttf', '/usr/share/fonts/TTF/DejaVuSans.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
    try: font = ImageFont.truetype(f, 22); break
    except OSError: pass
font = font or ImageFont.load_default()

def thumb(path):
    return Image.open(path).convert('RGB').resize((TW, TH), Image.LANCZOS)

def sheet(items, cols, title, out):
    rows = (len(items) + cols - 1) // cols
    img = Image.new('RGB', (cols * TW + (cols + 1) * 8, rows * (TH + 34) + 56 + 8), (24, 24, 24))
    g = ImageDraw.Draw(img)
    g.text((12, 14), title, fill=(235, 235, 235), font=font)
    for i, (label, path) in enumerate(items):
        x, y = 8 + (i % cols) * (TW + 8), 56 + (i // cols) * (TH + 34)
        img.paste(thumb(path), (x, y))
        g.text((x + 2, y + TH + 4), label, fill=(200, 200, 200), font=font)
    img.save(out)

for t, (name, what) in enumerate(names):
    items = [(p, os.path.join(d, f'{t}_{p}.png')) for p in PAGES if os.path.exists(os.path.join(d, f'{t}_{p}.png'))]
    sheet(items, 5, f'{t + 1}. {name} — {what}', os.path.join(d, f'sheet-{t:02d}-{name.lower()}.png'))
sheet([(f'{t + 1}. {n}', os.path.join(d, f'{t}_play.png')) for t, (n, _) in enumerate(names)], 5,
      'PLAY in every colour scheme (Shift+H switches)', os.path.join(d, 'sheet-all-play.png'))
sheet([(f'{t + 1}. {n}', os.path.join(d, f'{t}_touch.png')) for t, (n, _) in enumerate(names)], 5,
      'TOUCH in every colour scheme', os.path.join(d, 'sheet-all-touch.png'))

html = ['<!doctype html><meta charset="utf-8"><title>BARE! colour schemes</title>',
        '<style>body{background:#181818;color:#ddd;font:15px sans-serif;margin:16px}img{max-width:100%;display:block;margin:6px 0 18px}'
        'h2{margin-top:36px}a{color:#9cf}</style><h1>BARE! colour schemes</h1>',
        '<p>' + ' · '.join(f'<a href="#t{t}">{n}</a>' for t, (n, _) in enumerate(names)) + '</p>',
        '<img src="sheet-all-play.png"><img src="sheet-all-touch.png">']
for t, (name, what) in enumerate(names):
    html.append(f'<h2 id="t{t}">{t + 1}. {name}</h2><p>{what}</p><img src="sheet-{t:02d}-{name.lower()}.png">')
    html += [f'<p>{p}</p><img src="{t}_{p}.png">' for p in PAGES if os.path.exists(os.path.join(d, f'{t}_{p}.png'))]
open(os.path.join(d, 'index.html'), 'w').write('\n'.join(html))
print(f'{len(names)} schemes: {d}/index.html')
