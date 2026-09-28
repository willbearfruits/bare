#!/usr/bin/env python3
"""The manual, to read on screen or print: MANUAL.md, INSTRUMENTS.md and NOTICE.md laid out as chapters, one for each
page of the instrument with its picture (in the CREAM colours, which are kind to paper and ink), headings in BARE!'s
own screen font drawn as vector pixels (tools/pixelfont.py), and a map of the keyboard. Chromium turns the HTML into
an A4 PDF; a second pass puts the page numbers into the contents.
Usage: tools/manual.py [OUT.pdf]      (default build/manual/bare-manual-RELEASE.pdf, the release as the files name it: 1.0-beta)
Needs build/host/shots (make build/host/shots), Chromium and ImageMagick; fetches IBM Plex into build/fonts once."""
import html, os, re, subprocess, sys, urllib.request

HERE = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/..')
sys.path.insert(0, HERE + '/tools')
import pixelfont

BUILD = f'{HERE}/build/manual'
APP = open(f'{HERE}/core/app.h').read()
RELEASE = re.search(r'#define BARE_RELEASE\s+"([^"]+)"', APP).group(1)
STAGE = re.search(r'#define BARE_STAGE\s+"([^"]*)"', APP).group(1).strip()      # '' once a release is final
LABEL = f'{RELEASE} {STAGE}'.strip()
VNAME = f'{RELEASE}-{STAGE}' if STAGE else RELEASE
OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else f'{BUILD}/bare-manual-{VNAME}.pdf'
URL = 'github.com/willbearfruits/bare'

# ---------------------------------------------------------------- pictures
def shots():
    """every page at 1280x800 in the CREAM colours (HOST_THEME 7), as PNGs"""
    d = f'{BUILD}/shots/cream'
    if not os.path.exists(f'{d}/play.png'):
        os.makedirs(d, exist_ok=True)
        subprocess.run([f'{HERE}/build/host/shots', '1280', '800', d, ''], env={**os.environ, 'HOST_THEME': '7'},
                       check=True, stdout=subprocess.DEVNULL)
        for f in os.listdir(d):
            if f.endswith('.ppm'):
                subprocess.run(['magick', f'{d}/{f}', f'{d}/{f[:-4]}.png'], check=True); os.remove(f'{d}/{f}')
    return d

def frames():
    """the four splash pieces and Doom, one frame each (build/host/video, as tools/showcase.py records them)"""
    d = f'{BUILD}/img'
    os.makedirs(d, exist_ok=True)
    for n, (name, ms) in enumerate([('splash-ans', 5), ('splash-gendy', 5), ('splash-cmi', 6), ('splash-meta', 6)]):
        if os.path.exists(f'{d}/{name}.png'): continue
        tmp = f'{d}/{name}.mp4'
        subprocess.run([f'{HERE}/build/host/video', '--splash', str(n), tmp], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-ss', str(ms), '-i', tmp, '-frames:v', '1', '-vf',
                        'scale=1280:800:flags=neighbor', f'{d}/{name}.png'], check=True)
        os.remove(tmp)
    return d

def fonts():
    """IBM Plex Sans and Mono (SIL OFL), fetched once into build/fonts"""
    d = f'{HERE}/build/fonts'
    if os.path.exists(f'{d}/fonts.css'): return d
    os.makedirs(d, exist_ok=True)
    ua = {'User-Agent': 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36'}
    css = urllib.request.urlopen(urllib.request.Request(
        'https://fonts.googleapis.com/css2?family=IBM+Plex+Sans:ital,wght@0,400;0,500;0,600;0,700;1,400'
        '&family=IBM+Plex+Mono:wght@400;500;600&display=swap', headers=ua)).read().decode()
    faces = []
    for full, subset in re.findall(r'(/\* ([^*]+) \*/\s*@font-face \{[^}]+\})', css):
        if subset.strip() != 'latin': continue
        fam = re.search(r"font-family: '([^']+)'", full).group(1)
        style = re.search(r'font-style: (\w+)', full).group(1)
        weight = re.search(r'font-weight: (\d+)', full).group(1)
        name = f"{fam.replace(' ', '')}-{weight}{'i' if style == 'italic' else ''}.woff2"
        open(f'{d}/{name}', 'wb').write(urllib.request.urlopen(re.search(r'url\((https://[^)]+)\)', full).group(1)).read())
        faces.append(f"@font-face {{ font-family: '{fam}'; font-style: {style}; font-weight: {weight}; src: url('{name}'); }}")
    open(f'{d}/fonts.css', 'w').write('\n'.join(faces) + '\n')
    return d

# ---------------------------------------------------------------- the text: a little markdown
KEYNAMES = {'Shift', 'Ctrl', 'Alt', 'Fn', 'Space', 'Enter', 'Tab', 'Esc', 'Home', 'End', 'PgUp', 'PgDn', 'Del', 'Ins',
            'Backspace', 'BKSP', 'Caps'}
def keylike(t):
    if t.startswith('⇧') and len(t) > 1: return keylike(t[1:])
    if t in KEYNAMES or re.fullmatch(r'F\d{1,2}', t) or re.fullmatch(r'[↑↓←→]+', t) or len(t) == 1: return True
    if '+' in t and len(t) > 1:
        a, _, b = t.partition('+')
        return keylike(a) and (b == '' or keylike(b) or b == '+')
    return False
def keycaps(t):
    """`Shift+B` → keycaps; `make img`, `3@1`, `INSTR` stay code"""
    parts = t.split(' ')
    if not all(keylike(p) for p in parts if p): return f'<code>{html.escape(t)}</code>'
    out = []
    for p in parts:
        if not p: continue
        if p.startswith('⇧') and len(p) > 1: out.append('<kbd>⇧</kbd>' + keycaps(p[1:])); continue
        if '+' in p and len(p) > 1:
            a, _, b = p.partition('+')
            out.append(f'<kbd>{html.escape(a)}</kbd>+<kbd>{html.escape(b or "+")}</kbd>')
        else: out.append(f'<kbd>{html.escape(p)}</kbd>')
    return ' '.join(out)

LINKS = {'INSTRUMENTS.md': '#instruments', 'LICENSE': '#notices', 'NOTICE.md': '#notices'}
def inline(s):
    spans = []
    def keep(m):
        spans.append(keycaps(m.group(1).strip() if m.group(0).startswith('``') else m.group(1)))
        return f'\x00{len(spans) - 1}\x00'
    s = re.sub(r'``\s?(.+?)\s?``', keep, s)
    s = re.sub(r'`([^`]+)`', keep, s)
    s = html.escape(s, quote=False)
    s = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', s)
    s = re.sub(r'(?<![\w*])\*([^*\s][^*]*?)\*(?![\w*])', r'<em>\1</em>', s)
    s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', lambda m: f'<a href="{LINKS.get(m.group(2), m.group(2))}">{m.group(1)}</a>', s)
    s = s.replace(' — ', '&#8202;—&#8202;').replace('...', '…')
    return re.sub(r'\x00(\d+)\x00', lambda m: spans[int(m.group(1))], s)

def md(text, lead=False):
    """paragraphs, lists (- and 1.), tables, fenced code; the first paragraph a lead if asked"""
    out, lines, i = [], text.strip('\n').split('\n'), 0
    first = lead
    while i < len(lines):
        line = lines[i]
        if not line.strip(): i += 1; continue
        if line.startswith('```'):
            j = i + 1
            while j < len(lines) and not lines[j].startswith('```'): j += 1
            out.append('<pre>' + html.escape('\n'.join(lines[i + 1:j])) + '</pre>'); i = j + 1; continue
        if line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                cells = [c.strip() for c in lines[i].strip().strip('|').split('|')]
                if not all(re.fullmatch(r':?-+:?', c) for c in cells): rows.append(cells)
                i += 1
            head, body = rows[0], rows[1:]
            cls = ' class="keys"' if head[0].lower() in ('keys', 'page', 'setting') else ''
            t = f'<table{cls}><thead><tr>' + ''.join(f'<th>{inline(c)}</th>' for c in head) + '</tr></thead><tbody>'
            t += ''.join('<tr>' + ''.join(f'<td>{inline(c)}</td>' for c in r) + '</tr>' for r in body) + '</tbody></table>'
            out.append(t); continue
        m = re.match(r'(\s*)(- |\d+\. )', line)
        if m and not m.group(1):
            tag = 'ol' if m.group(2)[0].isdigit() else 'ul'
            items = []
            while i < len(lines) and lines[i].strip():
                l = lines[i]
                if re.match(r'(- |\d+\. )', l): items.append(re.sub(r'^(- |\d+\. )', '', l))
                elif items: items[-1] += ' ' + l.strip()
                i += 1
            out.append(f'<{tag}>' + ''.join(f'<li>{inline(x)}</li>' for x in items) + f'</{tag}>'); continue
        para = []
        while i < len(lines) and lines[i].strip() and not lines[i].startswith(('|', '```')) and not re.match(r'(- |\d+\. )', lines[i]):
            para.append(lines[i].strip()); i += 1
        out.append(f'<p{" class=\"lead\"" if first else ""}>{inline(" ".join(para))}</p>'); first = False
    return '\n'.join(out)

# ---------------------------------------------------------------- MANUAL.md, cut into its parts
def manual_parts():
    src = open(f'{HERE}/MANUAL.md').read()
    secs = {}
    for m in re.finditer(r'^## ([^\n]+)\n(.*?)(?=^## |\Z)', src, re.M | re.S): secs[m.group(1).strip()] = m.group(2)
    intro = src[src.index('\n', src.index('# BARE!')):src.index('## ')]
    blocks, keys_general = {}, secs['Keys']
    starts = [m for m in re.finditer(r'^\*\*([^*]+)\*\*', secs['Keys'], re.M)]
    if starts:
        keys_general = secs['Keys'][:starts[0].start()]
        for k, m in enumerate(starts):
            end = starts[k + 1].start() if k + 1 < len(starts) else len(secs['Keys'])
            blocks[m.group(1).strip().rstrip('.')] = secs['Keys'][m.start():end]
    return intro, secs, keys_general, blocks

def lead_of(block):
    """a page's paragraph without its name and keys: "**FX page** (`F10`, or …) — effects to play" → "Effects to play" """
    t = re.sub(r'^\*\*[^*]+\*\*\s*(\([^)]*\))?\s*(—\s*)?', '', block.strip(), count=1)
    t = re.sub(r'^\(([^)]*)\)\s*', '', t)
    return t[:1].upper() + t[1:]

# ---------------------------------------------------------------- drawing
def px(text, cls='px', font='ter-u32b'): return pixelfont.svg(text, font, cls)

def keyboard():
    """the laptop keyboard as PLAY and the F keys have it"""
    U, G = 11.4, 0.6                                         # a key unit and the gap between keys, in mm
    pages = ['PLAY', 'SEQ', 'WAVE', 'STRETCH', 'OPERATOR', 'TAPE', 'FILE', 'MIX', 'TOUCH', 'FX', 'LINEAGE', 'XENAKIS']
    roots = ['D♭', 'A♭', 'E♭', 'B♭', 'F', 'C', 'G', 'D', 'A', 'E', 'B', 'F♯']       # the OM-108's buttons, on three rows
    rows = [
        [('Esc', 1, 'fn', 'stop all'), (None, .5)] + [(f'F{k + 1}', 1, 'page', pages[k]) for k in range(4)] + [(None, .5)]
        + [(f'F{k + 1}', 1, 'page', pages[k]) for k in range(4, 8)] + [(None, .5)] + [(f'F{k + 1}', 1, 'page', pages[k]) for k in range(8, 12)],
        [('`', 1, 'fn', 'hold')] + [(c, 1, 'major', roots[k]) for k, c in enumerate('1234567890-=')] + [('⌫', 2, 'fn', 'off')],
        [('Tab', 1.5, 'fn', 'auto')] + [(c, 1, 'minor', roots[k]) for k, c in enumerate('QWERTYUIOP[]')] + [('\\', 1.5, 'seventh', roots[11])],
        [('Caps', 1.75, 'fn', 'keyboard')] + [(c, 1, 'seventh', roots[k]) for k, c in enumerate("ASDFGHJKL;'")] + [('Enter', 2.25, 'seventh', roots[11])],
        [('Shift', 2.25, 'mod', 'functions')] + [(c, 1, 'string', str(1 + k)) for k, c in enumerate('ZXCVBNM,./')] + [('Shift', 2.75, 'mod', 'functions')],
        [('Ctrl', 1.5, 'mod', 'Ctrl+1…= pages'), ('Alt', 1.25, '', ''), ('Space', 6.25, 'fn', 'rhythm'), ('Alt', 1.25, '', ''),
         ('←', 1, 'fn', 'pattern'), ('↑', 1, 'fn', 'octave'), ('↓', 1, 'fn', 'octave'), ('→', 1, 'fn', 'pattern')],
    ]
    fills = {'page': '#f4d6c3', 'major': '#f0e3c7', 'minor': '#e6cf9f', 'seventh': '#dcc08a', 'string': '#dde6cf', 'fn': '#ebe6dd', 'mod': '#ffffff', '': '#ffffff'}
    inks = {'page': '#b3471f', 'major': '#7a5a1e', 'minor': '#7a5a1e', 'seventh': '#6a4b12', 'string': '#4d6b35', 'fn': '#5d544a', 'mod': '#5d544a', '': '#5d544a'}
    out, y = [], 0.0
    for r, row in enumerate(rows):
        h = U * (0.8 if r == 0 else 1)
        x = 0.0
        for k in row:
            if k[0] is None: x += U * k[1]; continue
            legend, w, kind, label = k
            wmm = U * w - G
            out.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{wmm:.2f}" height="{h - G:.2f}" rx="1.1" fill="{fills[kind]}" stroke="#3b332b" stroke-width=".22"/>')
            out.append(f'<text x="{x + 1.1:.2f}" y="{y + 3.5:.2f}" class="kl">{html.escape(legend)}</text>')
            if label:
                size = 2.45 if len(label) <= 5 else 2.1 if len(label) <= 7 else 1.75 if len(label) <= 9 else 1.6
                out.append(f'<text x="{x + wmm / 2:.2f}" y="{y + h - G - 1.3:.2f}" class="kf" style="font-size:{size}px;fill:{inks[kind]}" text-anchor="middle">{html.escape(label)}</text>')
            x += U * w
        y += h + (1.6 if r == 0 else 0)
    width = U * 15 - G
    legend = [('page', 'the pages'), ('major', 'MAJOR'), ('minor', 'MINOR'), ('seventh', '7th'), ('string', 'strings (13 on the pad)'), ('fn', 'on PLAY')]
    lx, ly = 0.0, y + 3.5
    for kind, name in legend:
        out.append(f'<rect x="{lx:.2f}" y="{ly - 2.4:.2f}" width="3" height="3" rx=".5" fill="{fills[kind]}" stroke="#3b332b" stroke-width=".2"/>')
        out.append(f'<text x="{lx + 4:.2f}" y="{ly:.2f}" class="kg">{name}</text>')
        lx += 4 + len(name) * 1.42 + 6
    return (f'<svg class="keyboard" viewBox="-1 -1 {width + 2:.1f} {ly + 2:.1f}" xmlns="http://www.w3.org/2000/svg">'
            + ''.join(out) + '</svg>')

def figure(src, caption, cls='screen'):
    return f'<figure class="{cls}"><img src="{src}" alt=""><figcaption>{caption}</figcaption></figure>'

# ---------------------------------------------------------------- the chapters
def chapters():
    intro, secs, keys_general, b = manual_parts()
    S = shots(); I = frames()
    ch = []
    def page(fkey, name, kicker, lead_block, rest='', pic=None, alt=None, subs=()):
        body = f'<p class="lead">{inline(lead_of(lead_block).split(chr(10) + chr(10))[0].replace(chr(10), " "))}</p>'
        remainder = lead_of(lead_block).split('\n\n', 1)
        if pic: body += figure(f'{S}/{pic}.png', f'{fkey} · {name}' + (f' — {alt}' if alt else ''))
        if len(remainder) > 1: body += md(remainder[1])
        body += md(rest) if rest else ''
        for title, text, spic in subs:
            body += f'<section class="sub"><h3>{px(title.upper(), "px sub", "ter-u24b")}</h3>'
            if spic: body += figure(f'{S}/{spic}.png', title, 'screen small')
            body += md(lead_of(text) if text.startswith('**') else text) + '</section>'
        ch.append((name.lower().replace(' ', '-'), name, kicker, fkey, body))

    # 1: what it is, and trying it
    intro_text = '\n'.join(l for l in intro.split('\n') if not l.startswith(('![', '- The manual', '- All of it', '- Its page')))
    ch.append(('bare', 'BARE!', 'what it is', None, md(intro_text, lead=True) + md(secs['Trying it'])))
    splash = ''.join(f'<figure><img src="{I}/{n}.png" alt=""><figcaption>{c}</figcaption></figure>' for n, c in
                     [('splash-ans', 'ANS'), ('splash-gendy', 'GENDY'), ('splash-cmi', 'CMI'), ('splash-meta', 'METASTASEIS')])
    ch.append(('starting', 'STARTING UP', 'the boot and its splash', None, md(secs['Starting up']) + f'<div class="row4">{splash}</div>'))
    # 2: the keys
    keys_body = md(keys_general) + f'<figure class="board">{keyboard()}<figcaption>The keyboard on PLAY, the first page: the F keys go to the pages everywhere.</figcaption></figure>'
    keys_body += '<div class="pair">' + ''.join(f'<section class="sub"><h3>{px(t.upper(), "px sub", "ter-u24b")}</h3>{md(b[t])}</section>' for t in ('Echo', 'Thermal mode')) + '</div>' 
    ch.append(('keys', 'KEYS', 'the F keys, Shift and the rest', None, keys_body))
    # 3: the pages
    page('F1', 'PLAY', 'the omnichord', b['PLAY page'], pic='play', subs=[('Your own instruments', b['Your own instruments'], 'inst-shruti')])
    page('F2', 'SEQ', 'the tracker', b['SEQ page'], pic='seq')
    page('F3', 'WAVE', 'waves and the sampler', b['WAVE page'], pic='wave', alt='the eight waves in 3D',
         subs=[('Sampler', b['Sampler'], 'wave-sample')])
    page('F4', 'STRETCH', 'freeze what you just played', b['STRETCH page'], pic='stretch')
    page('F5', 'OPERATOR', 'four-operator FM', b['OPERATOR page'], pic='fm')
    page('F6', 'TAPE', 'eight tracks', b['TAPE page'], pic='tape-playing')
    page('F7', 'FILE', 'projects, songs, MIDI, the log', b['FILE page'], pic='file',
         subs=[('Songs', b['Songs'], None), ('MIDI and sync', b['MIDI and sync'], None), ('Network', b['Network'], None), ('Install', b['Install'], None)])
    page('F8', 'MIX', 'the mixer and its effects', b['MIX page'], pic='mix', subs=[('Input', b['Input'], None)])
    page('F9', 'TOUCH', 'a crackle box', b['TOUCH page'], pic='touch')
    page('F10', 'FX', 'effects to play live', b['FX page'], pic='fx')
    def with_views(block):                                   # a page of views: its lead, then a view each
        lead, lst = block.split('\n\n', 1) if '\n\n' in block else (block, '')
        return lead, re.findall(r'^- \*\*(\w+)\*\* — (.*?)(?=^- \*\*|\Z)', lst, re.M | re.S)
    para = lambda t: (lambda s: s[:1].upper() + s[1:])(' '.join(l.strip() for l in t.split('\n')))
    lin_lead, views = with_views(b['LINEAGE page'])
    pics = {'ANS': 'ans', 'REICH': 'lin-reich', 'CARLOS': 'lin-carlos', 'RADIGUE': 'lin-radigue', 'MERZBOW': 'lin-merzbow'}
    page('F11', 'LINEAGE', 'Xenakis, ANS, Reich, Carlos, Radigue, Merzbow', lin_lead, pic=None, subs=[(v, para(t), pics.get(v)) for v, t in views])
    xen_lead, views = with_views(b['XENAKIS'])
    pics = {'METASTASEIS': 'xen-meta', 'GENDY': 'xen-gendy', 'CLOUDS': 'xen-clouds', 'SIEVES': 'xen-sieves', 'UPIC': 'xen-upic'}
    page('F12', 'XENAKIS', 'in LINEAGE: Metastaseis, clouds, sieves, UPIC, GENDY', xen_lead, pic=None, subs=[(v, para(t), pics.get(v)) for v, t in views])
    doom = f'<p class="lead">{inline(lead_of(b["Doom"]).replace(chr(10), " "))}</p>'
    if os.path.exists(f'{I}/doom.png'): doom += f'<div class="grid2">{figure(f"{I}/doom-title.png", "Freedoom, from the stick")}{figure(f"{I}/doom.png", "E1M1, in BARE!")}</div>'
    ch.append(('doom', 'DOOM', 'iddqd', None, doom))
    # 4: instruments, machines, what next, notices
    inst = open(f'{HERE}/INSTRUMENTS.md').read()
    inst = inst[inst.index('\n'):]
    inst = re.sub(r'^## (.+)$', lambda m: f'\n@@H3@@{m.group(1)}\n', inst, flags=re.M)
    body = ''
    for part in re.split(r'\n@@H3@@', inst):
        if not part.strip(): continue
        title, _, text = part.partition('\n') if not part.startswith('An instrument') else ('', '', part)
        body += (f'<section class="sub"><h3>{px(title.upper(), "px sub", "ter-u24b")}</h3>' if title else '') + md(text, lead=not title) + ('</section>' if title else '')
    ch.append(('instruments', 'INSTRUMENTS', 'write your own', None, body))
    ch.append(('machines', 'MACHINES', 'which computers, and the stick', None, md(secs['Which machines'])
               + f'<section class="sub"><h3>{px("THE STICK", "px sub", "ter-u24b")}</h3>{md(secs["Stick layout and updates"])}</section>'))
    ch.append(('next', 'NEXT', 'the roadmap', None, md(secs['Roadmap'])))
    notice = open(f'{HERE}/NOTICE.md').read()
    notice = re.sub(r'^## (.+)$', r'**\1**', notice[notice.index('\n'):], flags=re.M)
    ch.append(('notices', 'NOTICES', 'licences', None, md(notice)))
    return ch

# ---------------------------------------------------------------- the book
FOLLOW = {'starting'}                                   # chapters that go on from the page before instead of a new one
LOGO = ["    ____    ___      ____    ______  __", "   / __ )  /   |    / __ \\  / ____/ / /", "  / __  | / /| |   / /_/ / / __/   / / ",
        " / /_/ / / ___ |  / _, _/ / /___  /_/  ", "/_____/ /_/  |_| /_/ |_| /_____/ (_)   "]

def book(pages=None):
    ch = chapters()
    toc = ''.join(f'<li><a href="#{cid}"><span class="tk">{f"<kbd>{fk}</kbd>" if fk else ""}</span><span class="tn">{html.escape(name)}</span>'
                  f'<span class="tt">{html.escape(kick)}</span><span class="dots"></span><span class="tp">{(pages or {}).get(cid, "")}</span></a></li>'
                  for cid, name, kick, fk, _ in ch)
    body = ''.join(f'<section class="chapter{" follows" if cid in FOLLOW else ""}" id="{cid}"><span class="marker">§{cid}§</span><header class="head">'
                   + (f'<kbd class="fkey">{fk}</kbd>' if fk else '') + f'{px(name)}<span class="kicker">{html.escape(kick)}</span></header>{text}</section>'
                   for cid, name, kick, fk, text in ch)
    S = f'{BUILD}/shots/cream'
    cover = (f'<section class="cover"><div class="ctop"><span>manual</span><span>version {LABEL} · 2026</span></div>'
             f'<div class="clogo">{px("BARE!", "px big")}</div><p class="ctag">a synthesizer with no operating system</p>'
             + '<div class="cpanel">' + ''.join(f'<span><kbd>F{k + 1}</kbd>{n}</span>' for k, n in enumerate(
                 ['PLAY', 'SEQ', 'WAVE', 'STRETCH', 'OPERATOR', 'TAPE', 'FILE', 'MIX', 'TOUCH', 'FX', 'LINEAGE', 'XENAKIS'])) + '</div>'
             f'<figure class="cshot"><img src="{S}/play.png" alt=""></figure>'
             f'<div class="cfoot"><span>willbearfruits</span><span>free software · GPL-3.0</span><span>{URL}</span></div></section>')
    back = (f'<section class="back"><pre class="ascii">{html.escape(chr(10).join(LOGO))}</pre>'
            f'<p>BARE! {LABEL} · the manual, made from MANUAL.md at {URL}.<br>Printed or on screen, it is free like the '
            'instrument: GPL-3.0-or-later.</p></section>')
    contents = f'<section class="contents"><header class="head">{px("CONTENTS")}</header><ol class="toc">{toc}</ol></section>'
    css = open(f'{fonts()}/fonts.css').read().replace("url('", f"url('file://{HERE}/build/fonts/")
    return f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>BARE! manual {LABEL}</title><style>{css}{STYLE.replace("@RELEASE@", LABEL)}</style></head><body>{cover}{contents}{body}{back}</body></html>'

STYLE = '''
@page { size: A4; margin: 16mm 17mm 19mm 17mm;
  @bottom-left { content: "BARE! @RELEASE@ · manual"; font: 500 6.8pt 'IBM Plex Mono'; color: #a3927c; letter-spacing: .06em; }
  @bottom-right { content: counter(page); font: 600 7.5pt 'IBM Plex Mono'; color: #b3471f; } }
@page :first { margin: 0; @bottom-left { content: none; } @bottom-right { content: none; } }
@page back { @bottom-left { content: none; } @bottom-right { content: none; } }
:root { --ink: #1f1a15; --soft: #6f6355; --faint: #a3927c; --rule: #e4dac9; --accent: #b3471f; --cap: #f5f0e6; }
html { font-family: 'IBM Plex Sans', sans-serif; font-size: 9.1pt; line-height: 1.5; color: var(--ink); -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { margin: 0; }
p { margin: 0 0 2.3mm; hyphens: auto; text-wrap: pretty; }
a { color: inherit; text-decoration: none; border-bottom: .4pt solid var(--faint); }
strong { font-weight: 600; }
.marker { font-size: .5pt; color: #fff; position: absolute; }
svg.px { display: block; height: 11mm; width: auto; }
svg.px.sub { height: 5.2mm; }
svg.px.big { height: 46mm; }
kbd { font: 500 7.4pt/1 'IBM Plex Mono', monospace; display: inline-block; padding: .7pt 2.3pt .5pt; border: .55pt solid #4a4037;
  border-bottom-width: 1.25pt; border-radius: 1.7pt; background: #fcfaf6; white-space: nowrap; vertical-align: .6pt; }
code, pre { font: 7.8pt 'IBM Plex Mono', monospace; }
code { background: var(--cap); padding: .3pt 2pt; border-radius: 1.2pt; }
pre { background: var(--cap); padding: 3mm 4mm; border-radius: 1.5mm; white-space: pre-wrap; line-height: 1.45; }
ul, ol { margin: 0 0 3mm; padding-left: 4.5mm; } li { margin-bottom: 1.3mm; } li::marker { color: var(--accent); }
table { border-collapse: collapse; width: 100%; margin: 3mm 0 4.5mm; font-size: 8.4pt; line-height: 1.42; }
th, td { text-align: left; vertical-align: top; padding: 1.35mm 2.5mm 1.35mm 0; border-bottom: .45pt solid var(--rule); }
th { font: 600 6.8pt 'IBM Plex Mono', monospace; text-transform: uppercase; letter-spacing: .08em; color: var(--soft); border-bottom: .9pt solid var(--ink); }
table.keys td:first-child { width: 29%; }
tr { break-inside: avoid; }

.chapter { break-before: page; position: relative; }
.chapter.follows { break-before: auto; margin-top: 12mm; }
.head { display: flex; align-items: flex-end; gap: 4mm; padding-bottom: 3mm; margin-bottom: 5mm; border-bottom: 1.3pt solid var(--ink); break-after: avoid; }
.head .fkey { font: 600 12pt/1 'IBM Plex Mono'; padding: 1.6mm 2.4mm 1.2mm; border: 1pt solid var(--ink); border-bottom-width: 2.6pt; border-radius: 2pt; background: #fff; vertical-align: 0; }
.head .kicker { margin-left: auto; font: 500 7.6pt 'IBM Plex Mono'; color: var(--accent); text-transform: uppercase; letter-spacing: .12em; padding-bottom: 1mm; }
.lead { font-size: 11.2pt; line-height: 1.46; margin-bottom: 4.5mm; font-weight: 400; }
figure { margin: 0; break-inside: avoid; }
figure.screen { margin: 3mm 0 5mm; }
figure.screen img { width: 100%; display: block; border: 1.5mm solid #221c17; border-radius: 1.8mm; box-sizing: border-box; }
figure.screen.small { width: 72%; }
figcaption { font: 500 6.8pt 'IBM Plex Mono'; color: var(--soft); margin-top: 1.6mm; letter-spacing: .03em; }
.sub { margin-top: 5.5mm; }
.sub h3 { margin: 0 0 3mm; padding-bottom: 1.6mm; border-bottom: .5pt solid var(--rule); break-after: avoid; color: var(--ink); }
.grid2, .grid4 { display: grid; gap: 3.5mm; margin: 3mm 0 4mm; }
.pair { display: grid; grid-template-columns: 1fr 1fr; gap: 7mm; } .pair .sub { margin-top: 2mm; }
.grid2 { grid-template-columns: 1fr 1fr; } .grid4 { grid-template-columns: 1fr 1fr; }
.row4 { display: grid; grid-template-columns: repeat(4, 1fr); gap: 2.6mm; margin: 3mm 0 4mm; break-inside: avoid; }
.row4 img { width: 100%; display: block; border: .9mm solid #221c17; border-radius: 1mm; box-sizing: border-box; }
.grid2 img, .grid4 img { width: 100%; display: block; border: 1.1mm solid #221c17; border-radius: 1.3mm; box-sizing: border-box; }
figure.board { margin: 5mm 0 6mm; }
svg.keyboard { width: 100%; display: block; }
svg.keyboard .kl { font: 500 3.3px 'IBM Plex Mono'; fill: #2b241e; }
svg.keyboard .kf { font-family: 'IBM Plex Sans'; font-weight: 600; }
svg.keyboard .kg { font: 500 2.9px 'IBM Plex Sans'; fill: #4b4238; }

.cover { width: 210mm; height: 297mm; position: relative; box-sizing: border-box; padding: 17mm 17mm; break-after: page; }
.ctop { display: flex; justify-content: space-between; font: 500 8pt 'IBM Plex Mono'; text-transform: uppercase; letter-spacing: .16em; color: var(--soft); border-bottom: 1.3pt solid var(--ink); padding-bottom: 3mm; }
.clogo { margin-top: 20mm; color: var(--ink); }
.ctag { font-size: 17pt; line-height: 1.25; margin-top: 7mm; color: var(--ink); letter-spacing: -.005em; }
.cpanel { display: grid; grid-template-columns: repeat(6, 1fr); gap: 2.6mm 3mm; margin-top: 11mm; font: 600 7.6pt 'IBM Plex Mono'; letter-spacing: .05em; color: var(--ink); }
.cpanel span { display: flex; align-items: center; gap: 1.8mm; padding: 1.6mm 0; border-top: .6pt solid var(--rule); }
.cpanel kbd { font-size: 7pt; color: var(--accent); border-color: var(--accent); }
.cshot { position: absolute; left: 17mm; right: 17mm; bottom: 34mm; }
.cshot img { width: 100%; display: block; border: 2.2mm solid #221c17; border-radius: 2.4mm; box-sizing: border-box; }
.cfoot { position: absolute; left: 17mm; right: 17mm; bottom: 15mm; display: flex; justify-content: space-between; font: 500 7.4pt 'IBM Plex Mono'; color: var(--soft); border-top: .6pt solid var(--ink); padding-top: 2.5mm; letter-spacing: .04em; }

.contents { break-after: page; }
.toc { list-style: none; padding: 0; margin: 2mm 0 0; }
.toc li { margin: 0; border-bottom: .45pt solid var(--rule); }
.toc a { display: flex; align-items: baseline; gap: 3mm; padding: 2.1mm 0; border: 0; }
.toc .tk { width: 12mm; } .toc .tn { font: 600 9.6pt 'IBM Plex Mono'; letter-spacing: .04em; width: 34mm; }
.toc .tt { color: var(--soft); } .toc .dots { flex: 1; } .toc .tp { font: 600 9pt 'IBM Plex Mono'; color: var(--accent); }

.back { page: back; break-before: page; height: 250mm; display: flex; flex-direction: column; justify-content: center; align-items: center; text-align: center; color: var(--soft); }
.back .ascii { background: none; font: 600 9.5pt/1.18 'IBM Plex Mono'; color: var(--accent); white-space: pre; margin-bottom: 8mm; }
.back p { font-size: 8.4pt; line-height: 1.6; }
'''

def render(page_html, pdf):
    src = f'{BUILD}/manual.html'
    open(src, 'w').write(page_html)
    subprocess.run(['chromium', '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files',
                    f'--print-to-pdf={pdf}', f'file://{src}'], check=True, stderr=subprocess.DEVNULL)

def where(pdf):
    """the page each chapter begins on, from the invisible markers"""
    text = subprocess.run(['pdftotext', '-layout', pdf, '-'], capture_output=True, text=True, check=True).stdout
    return {m: n + 1 for n, t in enumerate(text.split('\f')) for m in re.findall(r'§([\w-]+)§', t)}

if __name__ == '__main__':
    os.makedirs(BUILD, exist_ok=True)
    tmp = f'{BUILD}/pass1.pdf'
    render(book(), tmp)
    pages = where(tmp)
    render(book(pages), OUT)
    n = len(subprocess.run(['pdftotext', OUT, '-'], capture_output=True, text=True).stdout.split('\f')) - 1
    print(f'{OUT}: {n} pages, {os.path.getsize(OUT) >> 10} KiB; chapters from {min(pages.values(), default=0)} to {max(pages.values(), default=0)}')
