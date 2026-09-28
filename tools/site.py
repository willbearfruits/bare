#!/usr/bin/env python3
"""BARE!'s web page: one page in the instrument's own NIGHT colours, its headings in BARE!'s screen font
(tools/pixelfont.py). A silent loop, what the release added, the pages behind F-key tabs as on the instrument, how to
put it on a stick (a web page can't write one, so it explains the tools, the visitor's system first), the video, the
manual.
Usage: tools/site.py [OUTDIR]      (default build/site; serve it with python3 -m http.server)
Uses what tools/manual.py and tools/showcase.py made: build/manual (the PDF), build/host/out/showcase (the parts the
loop and a still are cut from), media/RELEASE/bare-showcase-1280.mp4 (the video: without it the page has no video,
the loop gives way to a still, and the splashes stand alone), build/fonts (IBM Plex), and
build/dist (make dist: the sizes of the files to download); makes the NIGHT pictures with build/host/shots."""
import html, os, re, shutil, subprocess, sys

HERE = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/..')
sys.path.insert(0, HERE + '/tools')
import pixelfont

OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else f'{HERE}/build/site'
APP = open(f'{HERE}/core/app.h').read()
RELEASE = re.search(r'#define BARE_RELEASE\s+"([^"]+)"', APP).group(1)
STAGE = re.search(r'#define BARE_STAGE\s+"([^"]*)"', APP).group(1).strip()      # '' once a release is final
VNAME = f'{RELEASE}-{STAGE}' if STAGE else RELEASE                               # the files' names; the tag is v + it
REPO = 'https://github.com/willbearfruits/bare'
SITE = 'https://willbearfruits.github.io/bare'
TAG = f'{REPO}/releases/tag/v{VNAME}'
DL = f'{REPO}/releases/download/v{VNAME}'
MANUAL = f'bare-manual-{VNAME}.pdf'
LABEL = f'{RELEASE} {STAGE}'.strip()
ZIP, XZ, ISO, FLOPPY = f'bare-{VNAME}.img.zip', f'bare-{VNAME}.img.xz', f'bare-{VNAME}.iso', f'bare-{VNAME}-floppy.img'
NIGHT = f'{HERE}/build/manual/shots/night'
PARTS = f'{HERE}/build/host/out/showcase'

def px(text, cls='px', font='ter-u32b'): return pixelfont.svg(text, font, cls)
def e(t): return html.escape(t, quote=True)

# the pages, as the F keys have them: (key, name, one line, [(view, picture, caption)])
PAGES = [
    ('F1', 'PLAY', "An omnichord after Suzuki's OM-108: the three letter rows are its chord buttons, the bottom row and the touchpad its 13-string strumplate, with a rhythm section whose chords and bass follow you. F1 again goes on to the instruments made from text files.", [
        ('OMNICHORD', 'play', 'Cmaj7: C with the 7th button beside it. The strings take the chord.'),
        ('KEYBOARD', 'play-keyboard', 'Keyboard mode, as on the OM-108: two rows of keys, drums on the third and on the strings.'),
        ('SHRUTI', 'inst-shruti', "A shruti box: the keys open reeds tuned in just intonation, the touchpad pumps the bellows."),
        ('STYLO', 'inst-stylo', "The touchpad as a Stylophone's strip."),
        ('GRIDPADS', 'inst-gridpads', 'A grid of pads with an arpeggio.')]),
    ('F2', 'SEQ', 'A tracker: patterns of eight channels, an order list, effect columns, every note on the exact sample.', [
        ('DEMO', 'seq', 'One of the demo songs, playing.'),
        ('E1M1', 'seq-e1m1', "Shift+4: Doom's E1M1 from the WAD on the stick (here Freedoom's), arranged as breakcore.")]),
    ('F3', 'WAVE', 'A Fairlight-style page, green on black: waves drawn with the touchpad, harmonics, a 3D stack of them, and a sampler.', [
        ('3D', 'wave', 'The eight waves, stacked in 3D.'),
        ('HARMONICS', 'wave-harmonics', 'A wave built from 32 harmonics.'),
        ('DRAW', 'wave-draw', 'A wave drawn with a finger.'),
        ('SAMPLER', 'wave-sample', 'A sample: 8-bit or full, trimmed, looped, played on the keys.')]),
    ('F4', 'STRETCH', 'Freezes the phrase you just played and stretches it, up to a thousand times slower, while you play on.', []),
    ('F5', 'OPERATOR', 'Four-operator FM: eight algorithms, envelopes, an LFO, four patches to play anywhere.', []),
    ('F6', 'TAPE', 'An eight-track tape with reels, wow and hiss: record, overdub, loop, bounce, build whole songs.', [
        ('PLAYING', 'tape-playing', 'Eight tracks rolling.')]),
    ('F7', 'FILE', 'Projects on the stick, songs out as WAV, MIDI and Ableton Link, which key opens what, the log, and an installer.', [
        ('PROJECTS', 'file', 'Projects on the stick.'),
        ('KEYS', 'file-keys', 'Which F key opens what: a page, a view, an instrument or nothing.')]),
    ('F8', 'MIX', 'A strip for every source, sends to an echo and a reverb, and a filter, drive and crusher on the master.', []),
    ('F9', 'TOUCH', "A crackle box after Michel Waisvisz's Kraakdoos: the eight bare pads of a small circuit. Fingers close it, pressing harder bends it, and under the board is his story.", []),
    ('F10', 'FX', 'Effects to play live over a song: repeat, reverse, tape stop, gate, a dub throw, a reverb freeze.', []),
    ('F11', 'LINEAGE', 'Homages you can play, each with a short history: who made the music, how, and what to listen to. XENAKIS is the first of them, with five views of its own.', [
        ('XENAKIS 1954', 'xen-meta', 'Iannis Xenakis: METASTASEIS, CLOUDS, SIEVES, UPIC and GENDY on a bar of their own. F12 goes straight there.'),
        ('ANS 1957', 'ans', "Murzin's photoelectronic synthesizer: 360 tones on a plate, read by a moving slit. With a drone sketch after Coil."),
        ('REICH 1965', 'lin-reich', "Steve Reich's phasing: players loop one pattern while a process moves them apart and back."),
        ('CARLOS 1968', 'lin-carlos', "A Moog-style voice in equal temperament or Wendy Carlos's alpha, beta and gamma scales."),
        ('RADIGUE 1969', 'lin-radigue', "Éliane Radigue's drones: eight partials a hair apart, beating, gliding over minutes."),
        ('MERZBOW 1979', 'lin-merzbow', 'Noise from junk: scraping fingers, struck metal, feedback, the program\'s own bytes.')]),
    ('F12', 'XENAKIS', "Inside LINEAGE, on a key of its own: five of Iannis Xenakis's ways of making music, in the order he found them.", [
        ('METASTASEIS 1954', 'xen-meta', 'String glissandi strung between two guide lines: the ruled surfaces of the Philips Pavilion.'),
        ('CLOUDS 1956', 'xen-clouds', 'Clouds of notes whose times, pitches and lengths are drawn from probabilities.'),
        ('SIEVES 1966', 'xen-sieves', 'Scales and rhythms from sieves: m@r, unions, intersections.'),
        ('UPIC 1977', 'xen-upic', 'Draw arcs with a finger, and a cursor plays them.'),
        ('GENDY 1991', 'xen-gendy', 'Dynamic stochastic synthesis: a waveform whose corners random-walk.')]),
]
SHOT_OF = {'STRETCH': 'stretch', 'OPERATOR': 'fm', 'MIX': 'mix', 'TOUCH': 'touch', 'FX': 'fx'}   # the pages without views
SHOTS = sorted({v[1] for p in PAGES for v in p[3]} | {SHOT_OF[p[1]] for p in PAGES if not p[3]})

# the silent loop at the top, cut from tools/showcase.py's parts: (part, start, seconds)
LOOP = [('03', 1.5, 3), ('07', 0.5, 4), ('27', 0.5, 4), ('25', 2, 3.5), ('25', 51, 3.5), ('11', 16, 3.5), ('31', 17, 3.5)]
LOOP_NOTE = 'the GENDY splash, the omnichord, METASTASEIS, REICH, MERZBOW, E1M1 in the tracker, and Doom (with Freedoom)'
SHOWCASE = f'{HERE}/media/{RELEASE}/bare-showcase-1280.mp4'                      # this release's video, when it has one
E1M1_STILL = ('11', 20.5)                                   # the SEQ ⇧4 part, when the song is well under way

def night_shots():
    """every page at 1280x800 in the NIGHT colours (the first scheme), as PNGs"""
    if os.path.exists(f'{NIGHT}/play.png'): return
    os.makedirs(NIGHT, exist_ok=True)
    subprocess.run([f'{HERE}/build/host/shots', '1280', '800', NIGHT, ''], env={**os.environ, 'HOST_THEME': '0'},
                   check=True, stdout=subprocess.DEVNULL)
    for f in os.listdir(NIGHT):
        if f.endswith('.ppm'):
            subprocess.run(['magick', f'{NIGHT}/{f}', f'{NIGHT}/{f[:-4]}.png'], check=True); os.remove(f'{NIGHT}/{f}')

def loop():
    out = f'{HERE}/build/site-loop.mp4'
    if os.path.exists(out) or not os.path.exists(f'{PARTS}/31.mp4'): return out
    tmp = f'{HERE}/build/site-loop'
    os.makedirs(tmp, exist_ok=True)
    with open(f'{tmp}/list.txt', 'w') as lst:
        for k, (part, at, length) in enumerate(LOOP):
            subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-ss', str(at), '-t', str(length), '-i', f'{PARTS}/{part}.mp4', '-an',
                            '-vf', 'scale=1280:800:flags=area,fps=30', '-c:v', 'libx264', '-preset', 'slow', '-crf', '26',
                            '-pix_fmt', 'yuv420p', f'{tmp}/c{k}.mp4'], check=True)
            lst.write(f"file 'c{k}.mp4'\n")
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', f'{tmp}/list.txt', '-c', 'copy',
                    '-movflags', '+faststart', out], check=True)
    shutil.rmtree(tmp)
    return out

def size(name):
    """a release file's size from make dist, as people say it ("6.8 MB"), or '' before make dist"""
    f = f'{HERE}/build/dist/bare-{VNAME}/{name}'
    if not os.path.exists(f): return ''
    n = os.path.getsize(f) / 1e6
    return f'{n:.1f} MB' if n < 10 else f'{n:.0f} MB'

def duration(path):
    return float(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', path],
                                capture_output=True, text=True, check=True).stdout)

def minutes(seconds):
    """513 → "Eight and a half minutes" """
    words = ['', 'One', 'Two', 'Three', 'Four', 'Five', 'Six', 'Seven', 'Eight', 'Nine', 'Ten', 'Eleven', 'Twelve']
    halves = round(seconds / 30)
    whole, half = divmod(halves, 2)
    return f'{words[whole]}{" and a half" if half else ""} minute{"s" if whole > 1 or half else ""}'

def build():
    night_shots()
    video = os.path.exists(SHOWCASE)
    if video: loop()
    if os.path.exists(OUT): shutil.rmtree(OUT)
    for d in ('img', 'media', 'fonts'): os.makedirs(f'{OUT}/{d}')
    for f in SHOTS:
        if f != 'seq-e1m1': shutil.copy(f'{NIGHT}/{f}.png', f'{OUT}/img/{f}.png')
    still = f'{HERE}/build/site-e1m1.png'                     # cut once from the showcase's SEQ ⇧4 part, then kept
    if not os.path.exists(still):
        part, at = E1M1_STILL
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-ss', str(at), '-i', f'{PARTS}/{part}.mp4', '-frames:v', '1',
                        '-vf', 'scale=1280:800:flags=area', still], check=True)
    shutil.copy(still, f'{OUT}/img/seq-e1m1.png')
    for f in ['doom', 'splash-ans', 'splash-gendy', 'splash-cmi', 'splash-meta']:
        shutil.copy(f'{HERE}/build/manual/img/{f}.png', f'{OUT}/img/{f}.png')
    shutil.copy(f'{HERE}/build/manual/{MANUAL}', f'{OUT}/{MANUAL}')
    subprocess.run(['pdftoppm', '-png', '-r', '72', '-f', '1', '-l', '1', '-singlefile', f'{OUT}/{MANUAL}', f'{OUT}/img/manual-cover'], check=True)
    text = subprocess.run(['pdftotext', f'{OUT}/{MANUAL}', '-'], capture_output=True, text=True, check=True).stdout
    keys = next((n + 1 for n, t in enumerate(text.split('\f')) if '§keys§' in t), 4)          # the page with the keyboard map
    subprocess.run(['pdftoppm', '-png', '-r', '72', '-f', str(keys), '-l', str(keys), '-singlefile', f'{OUT}/{MANUAL}', f'{OUT}/img/manual-keys'], check=True)
    pages = int(re.search(r'Pages:\s+(\d+)', subprocess.run(['pdfinfo', f'{OUT}/{MANUAL}'], capture_output=True, text=True, check=True).stdout).group(1))
    clip = f'{HERE}/build/site-loop.mp4'
    if video:
        if os.path.exists(clip): shutil.copy(clip, f'{OUT}/media/loop.mp4')
        shutil.copy(SHOWCASE, f'{OUT}/media/bare-showcase.mp4')
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-ss', '3', '-i', f'{OUT}/media/bare-showcase.mp4', '-frames:v', '1', f'{OUT}/img/showcase-poster.png'], check=True)
    else: os.rmdir(f'{OUT}/media')
    for f in os.listdir(f'{HERE}/build/fonts'):
        if f.endswith('.woff2') or f == 'OFL.txt': shutil.copy(f'{HERE}/build/fonts/{f}', f'{OUT}/fonts/{f}')
    faces = open(f'{HERE}/build/fonts/fonts.css').read().replace("url('", "url('fonts/")
    open(f'{OUT}/favicon.svg', 'w').write(pixelfont.svg('B', 'ter-u32b', 'icon', trim=True)
        .replace('<svg ', '<svg style="background:#0c0f14" ').replace('currentColor', '#ffb454'))
    open(f'{OUT}/index.html', 'w').write(page(faces, minutes(duration(f'{OUT}/media/bare-showcase.mp4')) if video else None, pages))
    open(f'{OUT}/.nojekyll', 'w').write('')

# ---------------------------------------------------------------- the page
def tabs_and_panels():
    """the pages behind F-key tabs, the way the instrument's title bar shows them; a page's views under its picture"""
    tabs, panels = '', ''
    for n, (fk, name, line, views) in enumerate(PAGES):
        pid = name.lower()
        first = views[0] if views else (name, SHOT_OF.get(name, pid), '')
        tabs += (f'<button role="tab" id="t-{pid}" aria-controls="p-{pid}" aria-selected="{"true" if n == 0 else "false"}"'
                 f'{"" if n == 0 else " tabindex=\"-1\""}><span>{fk}</span>{name}</button>')
        chips = ''
        if len(views) > 1:
            chips = '<div class="views">' + ''.join(
                f'<button type="button" aria-pressed="{"true" if k == 0 else "false"}" data-img="img/{img}.png" data-cap="{e(cap)}"'
                f' data-alt="{e(name)} · {e(v)}">{e(v)}</button>' for k, (v, img, cap) in enumerate(views)) + '</div>'
        panels += (f'<div class="panel{" on" if n == 0 else ""}" role="tabpanel" id="p-{pid}" aria-labelledby="t-{pid}">'
                   f'<div class="screen"><img src="img/{first[1]}.png" alt="{e(name)}{" · " + e(first[0]) if views else ""}" width="1280" height="800"'
                   f'{"" if n == 0 else " loading=\"lazy\""}></div>'
                   f'<div class="cap"><div class="ct"><kbd>{fk}</kbd><h3>{name}</h3></div><p>{e(line)}</p>{chips}'
                   f'<p class="vcap">{e(first[2]) if len(views) > 1 else ""}</p></div></div>')
    return tabs, panels

def page(faces, video_len, manual_pages):
    """video_len: the showcase's length in words, or None when this release has no video"""
    places = [('#new', 'HIGHLIGHTS'), ('#pages', 'PAGES'), ('#get', 'GET IT')] + ([('#video', 'VIDEO')] if video_len else []) \
        + [('#manual', 'MANUAL'), (REPO, 'SOURCE')]
    nav = ''.join(f'<a class="tab" href="{h}"><span>F{k + 1}</span>{n}</a>' for k, (h, n) in enumerate(places))
    hero = (f'<div class="screen"><video id="loop" src="media/loop.mp4" poster="img/splash-gendy.png" autoplay muted loop playsinline aria-label="BARE! being played"></video></div>\n'
            f'  <p class="note">Recorded from the instrument itself: {LOOP_NOTE}.</p>' if video_len else
            '<div class="screen"><img src="img/splash-gendy.png" alt="the GENDY splash: BARE! drawn by random walks" width="1280" height="800"></div>\n'
            '  <p class="note">One of the four splashes it plays as it starts: GENDY, after Xenakis\'s dynamic stochastic synthesis.</p>')
    tabs, panels = tabs_and_panels()
    curl = f'curl -L {DL}/{XZ} | xz -dc | sudo dd of=/dev/sdX bs=4M conv=fsync status=progress'
    zs, xs, fs = size(ZIP), size(XZ), size(FLOPPY)
    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BARE! · a synthesizer with no operating system</title>
<meta name="description" content="A synthesizer that is the whole computer. It starts from a USB stick straight into the instrument, with no operating system underneath, on PCs from the late 1990s to now.">
<meta property="og:title" content="BARE! · a synthesizer with no operating system">
<meta property="og:description" content="A synthesizer that is the whole computer: an omnichord, a tracker, FM, tape, Xenakis, homages to Reich, Carlos, Radigue and Merzbow — and Doom. From a USB stick, with no operating system.">
<meta property="og:type" content="website">
<meta property="og:url" content="{SITE}/">
<meta property="og:image" content="{SITE}/img/play.png">
<meta name="twitter:card" content="summary_large_image">
<meta name="color-scheme" content="dark">
<link rel="icon" href="favicon.svg">
<script>document.documentElement.classList.add('js')</script>
<style>
{faces}
:root {{ --bg: #0c0f14; --panel: #141a22; --deep: #0f141b; --border: #263140; --line: #2c3746; --dim: #7a8899; --text: #b8c4cf;
  --bright: #f0f4f7; --amber: #ffb454; --amber-d: #6e5025; --green: #7ee787; --cyan: #58c7f3; --pink: #f286c4; --red: #ff7b72; }}
* {{ box-sizing: border-box; }}
html {{ background: var(--bg); color: var(--text); font: 17px/1.6 'IBM Plex Sans', system-ui, sans-serif; scroll-behavior: smooth; scroll-padding-top: 56px; -webkit-text-size-adjust: 100%; }}
body {{ margin: 0; overflow-x: hidden; }}
a {{ color: var(--cyan); text-decoration: none; }} a:hover {{ text-decoration: underline; }}
h3 {{ color: var(--bright); font-weight: 600; margin: 0; }}
code {{ font: .84em 'IBM Plex Mono', monospace; color: var(--bright); background: var(--panel); border: 1px solid var(--line); border-radius: 4px; padding: .05em .35em; word-break: break-word; }}
kbd {{ font: 500 .78em 'IBM Plex Mono', monospace; color: var(--bright); background: var(--bg); border: 1px solid var(--dim); border-bottom-width: 2.5px; border-radius: 5px; padding: .08em .45em; white-space: nowrap; }}
.wrap {{ max-width: 1120px; margin: 0 auto; padding: 0 24px; }}
svg.px {{ display: block; height: clamp(26px, 6vw, 40px); width: auto; max-width: 100%; color: var(--amber); }}
svg.px.big {{ height: clamp(84px, 16vw, 180px); color: var(--bright); }}
svg.px.small {{ height: 22px; }}
.small {{ font-size: 14.5px; color: var(--dim); }}
.small a {{ color: var(--text); text-decoration: underline; text-decoration-color: var(--line); text-underline-offset: 3px; }}

/* the title bar, as the instrument draws it: the lit tab follows the section in view */
.bar {{ position: sticky; top: 0; z-index: 10; background: rgba(20, 26, 34, .94); backdrop-filter: blur(6px); border-bottom: 1px solid var(--line); }}
.bar .wrap {{ display: flex; align-items: center; gap: 2px; height: 44px; font: 500 13px 'IBM Plex Mono', monospace; overflow-x: auto; scrollbar-width: none; }}
.bar .wrap::-webkit-scrollbar {{ display: none; }}
.bar .home {{ color: var(--bright); margin-right: 14px; white-space: nowrap; flex-shrink: 0; }} .bar .home b {{ color: var(--amber); font-weight: 500; }}
.bar a.tab {{ color: var(--text); padding: 3px 9px; border-radius: 3px; white-space: nowrap; flex-shrink: 0; }}
.bar a.tab span {{ color: var(--dim); margin-right: 5px; }}
.bar a.tab:hover, .bar a.tab.lit {{ background: var(--amber); color: var(--bg); text-decoration: none; }}
.bar a.tab:hover span, .bar a.tab.lit span {{ color: var(--amber-d); }}
.bar .ver {{ margin-left: auto; padding-left: 14px; color: var(--dim); white-space: nowrap; flex-shrink: 0; }}

.hero {{ padding: 64px 0 24px; }}
.hero .tag {{ font-size: clamp(24px, 3.4vw, 36px); line-height: 1.25; color: var(--bright); margin: 30px 0 12px; max-width: 30ch; letter-spacing: -.01em; }}
.hero .sub {{ font-size: 19px; max-width: 56ch; margin: 0; }}
.cta {{ display: flex; flex-wrap: wrap; gap: 12px; margin: 30px 0 44px; }}
.btn {{ display: inline-flex; align-items: baseline; gap: 10px; font: 500 15px 'IBM Plex Mono', monospace; padding: 11px 18px; border: 1px solid var(--line); border-radius: 6px; color: var(--bright); background: var(--panel); cursor: pointer; }}
.btn:hover {{ border-color: var(--amber); text-decoration: none; }}
.btn small {{ font-size: 12.5px; color: var(--dim); }}
.btn.primary {{ background: var(--amber); border-color: var(--amber); color: var(--bg); }} .btn.primary:hover {{ background: #ffc677; }}
.btn.primary small {{ color: var(--amber-d); }}
.screen {{ border: 1px solid var(--line); border-radius: 12px; overflow: hidden; background: #000; box-shadow: 0 0 0 6px var(--panel), 0 36px 80px -24px rgba(0, 0, 0, .85); }}
.screen video, .screen img {{ display: block; width: 100%; height: auto; aspect-ratio: 16 / 10; }}
.hero .note {{ font: 13px 'IBM Plex Mono', monospace; color: var(--dim); margin-top: 18px; }}

section {{ padding: 88px 0 8px; }}
p.lede {{ font-size: 19px; max-width: 60ch; margin: 18px 0 34px; }}

/* what it is: three facts */
.three {{ display: grid; grid-template-columns: repeat(3, 1fr); gap: 34px; }}
.three div {{ border-top: 2px solid var(--amber); padding-top: 14px; }}
.three b {{ display: block; font: 600 13px 'IBM Plex Mono', monospace; color: var(--bright); letter-spacing: .07em; margin-bottom: 8px; }}
.three p {{ margin: 0; font-size: 16px; }}

/* what the release added */
.feature {{ display: grid; grid-template-columns: 1.25fr 1fr; gap: 48px; align-items: center; margin: 0 0 72px; }}
.feature.flip {{ grid-template-columns: 1fr 1.25fr; }}
.feature.flip > .screen, .feature.flip > .quad {{ order: 2; }}
.feature > * {{ min-width: 0; }}
.kicker {{ display: flex; align-items: center; gap: 10px; font: 600 13px 'IBM Plex Mono', monospace; color: var(--amber); letter-spacing: .08em; margin: 0 0 12px; }}
.kicker kbd {{ color: var(--amber); border-color: var(--amber-d); }}
.feature h3 {{ font-size: clamp(22px, 2.6vw, 28px); line-height: 1.2; letter-spacing: -.01em; margin-bottom: 14px; }}
.feature p {{ margin: 0 0 12px; }}
.feature .try {{ font-size: 15px; color: var(--dim); }}
.quad {{ display: grid; grid-template-columns: 1fr 1fr; gap: 8px; padding: 8px; background: var(--panel); border: 1px solid var(--line); border-radius: 12px; }}
.quad img {{ width: 100%; height: auto; aspect-ratio: 16 / 10; display: block; border-radius: 6px; }}
.chips {{ display: flex; flex-wrap: wrap; gap: 6px; margin: 4px 0 14px; }}
.chips span {{ font: 500 12px 'IBM Plex Mono', monospace; color: var(--text); border: 1px solid var(--line); border-radius: 3px; padding: 2px 7px; }}
.chips span b {{ color: var(--dim); font-weight: 500; margin-left: 5px; }}
ul.also {{ list-style: none; padding: 0; margin: 0; display: grid; grid-template-columns: repeat(4, 1fr); gap: 16px; }}
ul.also li {{ background: var(--panel); border: 1px solid var(--border); border-radius: 10px; padding: 16px 18px; font-size: 15.5px; }}
ul.also b {{ display: block; color: var(--bright); font-weight: 600; margin-bottom: 4px; }}

/* the pages, behind F-key tabs */
.tabs {{ display: flex; gap: 2px; overflow-x: auto; scrollbar-width: none; background: var(--panel); border: 1px solid var(--line); border-radius: 10px 10px 0 0; border-bottom: 0; padding: 6px; }}
.tabs::-webkit-scrollbar {{ display: none; }}
.tabs button {{ font: 500 13px 'IBM Plex Mono', monospace; color: var(--text); background: none; border: 0; border-radius: 3px; padding: 5px 10px; white-space: nowrap; cursor: pointer; flex-shrink: 0; }}
.tabs button span {{ color: var(--dim); margin-right: 5px; }}
.tabs button:hover {{ color: var(--bright); background: var(--deep); }}
.tabs button[aria-selected=true] {{ background: var(--amber); color: var(--bg); }}
.tabs button[aria-selected=true] span {{ color: var(--amber-d); }}
.tabs button:focus-visible, .views button:focus-visible, .os button:focus-visible, .btn:focus-visible {{ outline: 2px solid var(--cyan); outline-offset: 2px; }}
.deck {{ border: 1px solid var(--line); border-radius: 0 0 12px 12px; background: var(--deep); padding: 22px; }}
.panel {{ display: grid; grid-template-columns: 1.6fr 1fr; gap: 28px; align-items: start; }}
.panel + .panel {{ margin-top: 40px; }}
.js .panel {{ display: none; margin-top: 0; }} .js .panel.on {{ display: grid; }}
.panel > * {{ min-width: 0; }}
.panel .screen {{ box-shadow: none; border-radius: 8px; }}
.panel .ct {{ display: flex; align-items: center; gap: 12px; margin-bottom: 10px; }}
.panel .ct kbd {{ color: var(--amber); border-color: var(--amber-d); font-size: 14px; }}
.panel .ct h3 {{ font: 600 22px 'IBM Plex Mono', monospace; letter-spacing: .05em; }}
.panel p {{ margin: 0 0 14px; }}
.views {{ display: flex; flex-wrap: wrap; gap: 6px; margin: 4px 0 12px; }}
.views button {{ font: 500 12px 'IBM Plex Mono', monospace; color: var(--text); background: var(--panel); border: 1px solid var(--line); border-radius: 3px; padding: 4px 8px; cursor: pointer; }}
.views button:hover {{ border-color: var(--cyan); }}
.views button[aria-pressed=true] {{ background: var(--cyan); border-color: var(--cyan); color: var(--bg); }}
.vcap {{ font-size: 15px; color: var(--dim); min-height: 3em; }}
.nojs-note {{ display: none; }}

/* getting it onto a stick */
ol.steps {{ counter-reset: s; list-style: none; padding: 0; margin: 0; display: grid; gap: 16px; }}
ol.steps > li {{ counter-increment: s; display: grid; grid-template-columns: 46px 1fr; gap: 12px; background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 22px 24px 20px 20px; }}
ol.steps > li::before {{ content: counter(s); font: 600 24px/1 'IBM Plex Mono', monospace; color: var(--amber); padding-top: 2px; }}
ol.steps > li > div {{ min-width: 0; }}
ol.steps h3 {{ font-size: 19px; margin-bottom: 8px; }}
ol.steps p {{ margin: 0 0 10px; }}
.dl {{ display: flex; flex-wrap: wrap; gap: 10px; margin: 6px 0 12px; }}
.os {{ display: flex; gap: 4px; margin: 4px 0 14px; }}
.os button {{ font: 500 13.5px 'IBM Plex Mono', monospace; color: var(--text); background: var(--deep); border: 1px solid var(--line); border-radius: 5px; padding: 6px 14px; cursor: pointer; }}
.os button[aria-selected=true] {{ background: var(--bright); color: var(--bg); border-color: var(--bright); }}
.ospanel ol {{ margin: 0 0 10px; padding-left: 22px; }} .ospanel li {{ margin-bottom: 6px; }}
.js .ospanel {{ display: none; }} .js .ospanel.on {{ display: block; }}
.ospanel h4 {{ margin: 0 0 6px; font: 600 12px 'IBM Plex Mono', monospace; letter-spacing: .08em; color: var(--dim); }}
.js .ospanel h4 {{ display: none; }}
.cmd {{ display: flex; align-items: stretch; gap: 0; margin: 8px 0 12px; border: 1px solid var(--line); border-radius: 8px; overflow: hidden; background: var(--bg); }}
.cmd code {{ flex: 1; display: block; border: 0; border-radius: 0; background: none; padding: 12px 14px; font-size: 13.5px; line-height: 1.5; white-space: pre-wrap; word-break: normal; overflow-wrap: anywhere; }}
.cmd button {{ font: 500 12.5px 'IBM Plex Mono', monospace; color: var(--bright); background: var(--panel); border: 0; border-left: 1px solid var(--line); padding: 0 16px; cursor: pointer; }}
.cmd button:hover {{ color: var(--amber); }}
.warn {{ border-left: 3px solid var(--red); padding-left: 12px; }}
.update {{ margin: 22px 0 0; padding: 16px 20px; border: 1px dashed var(--line); border-radius: 10px; }}
.floppy {{ display: grid; grid-template-columns: 1fr 1.3fr; gap: 30px; margin-top: 22px; background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 24px; }}
.floppy > * {{ min-width: 0; }}
.floppy h3 {{ font-size: 19px; margin-bottom: 8px; }} .floppy p {{ margin: 0 0 14px; }}
.floppy ol {{ margin: 0; padding-left: 20px; display: grid; gap: 8px; font-size: 15.5px; }}
.floppy ol b {{ color: var(--bright); font-weight: 600; }}
.update b {{ color: var(--bright); }}
.facts {{ display: grid; grid-template-columns: repeat(3, 1fr); gap: 24px; margin-top: 30px; }}
.facts div {{ border-top: 2px solid var(--line); padding-top: 12px; font-size: 15px; }}
.facts b {{ display: block; font: 600 12.5px 'IBM Plex Mono', monospace; color: var(--bright); letter-spacing: .07em; margin-bottom: 6px; }}

.splash {{ display: grid; grid-template-columns: repeat(4, 1fr); gap: 14px; margin-top: 22px; }}
.splash figure {{ margin: 0; }} .splash img {{ width: 100%; height: auto; display: block; border-radius: 8px; border: 1px solid var(--line); }}
.splash figcaption {{ font: 12px 'IBM Plex Mono', monospace; color: var(--dim); margin-top: 7px; letter-spacing: .06em; }}

.two {{ display: grid; grid-template-columns: 1fr 1fr; gap: 24px; }}
.card {{ background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 26px; min-width: 0; }}
.card p {{ margin: 16px 0 16px; }}
.card img {{ width: 100%; height: auto; display: block; border-radius: 8px; border: 1px solid var(--line); }}
pre.file {{ font: 13.5px/1.55 'IBM Plex Mono', monospace; color: var(--text); background: var(--bg); border: 1px solid var(--line); border-radius: 8px; padding: 16px 18px; margin: 0; overflow-x: auto; }}
pre.file .c {{ color: var(--dim); }} pre.file .s {{ color: var(--amber); }} pre.file .k {{ color: var(--cyan); }}

.manual {{ display: grid; grid-template-columns: 250px 250px 1fr; gap: 26px; align-items: center; }}
.manual img {{ width: 100%; height: auto; display: block; border-radius: 4px; box-shadow: 0 24px 60px -18px rgba(0, 0, 0, .9); background: #fff; }}
.manual .btns {{ display: flex; flex-wrap: wrap; gap: 10px; margin-top: 18px; }}
ul.next {{ list-style: none; padding: 0; margin: 26px 0 0; display: grid; gap: 12px; }}
ul.next li {{ background: var(--panel); border: 1px solid var(--border); border-left: 3px solid var(--amber); border-radius: 8px; padding: 14px 18px; max-width: 82ch; }}
ul.next b {{ color: var(--bright); font-weight: 600; }}

footer {{ margin-top: 96px; border-top: 1px solid var(--line); padding: 36px 0 60px; font-size: 14px; color: var(--dim); }}
footer .wrap {{ display: grid; grid-template-columns: auto 1fr; gap: 36px; align-items: start; }}
footer p {{ margin: 0 0 8px; max-width: 80ch; }}
footer a {{ color: var(--text); }}
footer svg.px {{ height: 30px; color: var(--dim); }}

@media (max-width: 900px) {{
  html {{ font-size: 16px; }} .wrap {{ padding: 0 16px; }}
  .feature, .feature.flip, .panel, .two, .floppy, footer .wrap {{ grid-template-columns: 1fr; gap: 22px; }}
  .feature.flip > .screen, .feature.flip > .quad {{ order: 0; }}
  .three, .facts, ul.also {{ grid-template-columns: 1fr; gap: 18px; }}
  .splash {{ grid-template-columns: 1fr 1fr; }}
  .hero {{ padding-top: 40px; }} section {{ padding-top: 64px; }} .feature {{ margin-bottom: 52px; }}
  .deck {{ padding: 14px; }} ol.steps > li {{ grid-template-columns: 30px 1fr; padding: 18px 16px; }}
  .manual {{ grid-template-columns: 1fr 1fr; gap: 16px; }} .manual > div {{ grid-column: 1 / -1; }} .bar .ver {{ display: none; }}
}}
@media (prefers-reduced-motion: reduce) {{ html {{ scroll-behavior: auto; }} }}
</style>
</head>
<body>
<nav class="bar" aria-label="sections"><div class="wrap">
  <a class="home" href="#top"><b>◆</b> BARE!</a>
  {nav}
  <span class="ver">{LABEL}</span>
</div></nav>

<header class="hero" id="top"><div class="wrap">
  {px('BARE!', 'px big')}
  <p class="tag">A synthesizer that is the whole computer.</p>
  <p class="sub">It starts from a USB stick straight into the instrument, with no operating system underneath, on PCs from the late 1990s to now.</p>
  <div class="cta">
    <a class="btn primary" href="#get">Put it on a stick</a>
    {'<a class="btn" href="#video">Watch it played</a>' if video_len else ''}
    <a class="btn" href="{MANUAL}">The manual <small>PDF</small></a>
  </div>
  {hero}
</div></header>

<section id="what" style="padding-top:56px"><div class="wrap">
  <div class="three">
    <div><b>NO OPERATING SYSTEM</b><p>BARE! is the program the PC starts, and nothing else runs: the keys, the screen and the sound chip belong to the instrument.</p></div>
    <div><b>OLD PCS AND NEW</b><p>Any PC since the Pentium Pro, from 32 MB of memory up, BIOS or UEFI. It sizes itself to what it finds.</p></div>
    <div><b>YOUR WORK ON THE STICK</b><p>It runs from memory, so the stick can come out while you play. Projects, songs and your own instruments are saved on it.</p></div>
  </div>
</div></section>

<section id="new"><div class="wrap">
  <h2>{px('HIGHLIGHTS')}</h2>
  <p class="lede">An omnichord you play like the real one, a lineage of homages with their histories, and Doom's first level as breakcore. This is the first release, a beta: it works, and it is still being tried on more machines.</p>

  <div class="feature">
    <div class="screen"><img src="img/play.png" alt="the PLAY page: the OM-108's chord buttons and strumplate" width="1280" height="800" loading="lazy"></div>
    <div>
      <p class="kicker"><kbd>F1</kbd> PLAY</p>
      <h3>An OM-108 on the keyboard</h3>
      <p>PLAY is laid out after Suzuki's OM-108 and its owner's manual. The three letter rows are its MAJOR, MINOR and 7th chord buttons, pressed together for maj7, m7, dim, aug, sus4 and add9; the bottom row and the touchpad are its 13-string strumplate.</p>
      <p>Ten voices, ten rhythms that change at the next bar, AUTO chords and bass, HOLD, SYNC START, a keyboard mode with drums, and MIDI out on its channels.</p>
      <p class="try">Try it: hold <kbd>6</kbd> and run a finger along the keys from <kbd>Z</kbd> to <kbd>/</kbd>.</p>
    </div>
  </div>

  <div class="feature flip">
    <div class="quad">
      <img src="img/lin-reich.png" alt="REICH" width="1280" height="800" loading="lazy"><img src="img/lin-carlos.png" alt="CARLOS" width="1280" height="800" loading="lazy">
      <img src="img/lin-radigue.png" alt="RADIGUE" width="1280" height="800" loading="lazy"><img src="img/lin-merzbow.png" alt="MERZBOW" width="1280" height="800" loading="lazy">
    </div>
    <div>
      <p class="kicker"><kbd>F11</kbd> LINEAGE</p>
      <h3>Six homages, each with its history</h3>
      <p>Instruments after the people who made the music: Iannis Xenakis, with five views of his own; Murzin's ANS synthesizer, with a drone sketch after Coil; Steve Reich's phasing; Wendy Carlos's alpha, beta and gamma scales; Éliane Radigue's slow drones; Merzbow's noise from junk.</p>
      <p>Each one plays, and each has a short history beside it: who, how, and what to listen to. TOUCH has one too: Michel Waisvisz and his Crackle Box.</p>
      <div class="chips"><span>XENAKIS<b>1954</b></span><span>ANS<b>1957</b></span><span>REICH<b>1965</b></span><span>CARLOS<b>1968</b></span><span>RADIGUE<b>1969</b></span><span>MERZBOW<b>1979</b></span></div>
    </div>
  </div>

  <div class="feature">
    <div class="screen"><img src="img/xen-meta.png" alt="METASTASEIS: glissandi between two guide lines, and the surface they make" width="1280" height="800" loading="lazy"></div>
    <div>
      <p class="kicker"><kbd>F12</kbd> LINEAGE › XENAKIS</p>
      <h3>Metastaseis</h3>
      <p>In 1954 Iannis Xenakis drew 46 string glissandi as straight lines on graph paper, and together they made curved surfaces — the same surfaces became the Philips Pavilion.</p>
      <p>Here you set two guide lines, the strings are strung between them, and the surface turns in 3D while it sounds. <kbd>Enter</kbd> writes it onto UPIC's page.</p>
    </div>
  </div>

  <div class="feature flip">
    <div class="screen"><img src="img/seq-e1m1.png" alt="the tracker playing Freedoom's E1M1, arranged as breakcore" width="1280" height="800" loading="lazy"></div>
    <div>
      <p class="kicker"><kbd>F2</kbd> SEQ · <kbd>Shift</kbd>+<kbd>4</kbd></p>
      <h3>E1M1, as breakcore</h3>
      <p>The tracker reads Doom's first-level music from the WAD on your stick — yours, or Freedoom's — and lays it out on its grid: guitars and bass under chopped breakbeats at 172 BPM, snare rolls, stutters, a breakdown and a tape stop.</p>
      <p class="try">The notes come from your WAD; the arrangement is BARE!'s. The picture is Freedoom's E1M1.</p>
    </div>
  </div>

  <ul class="also">
    <li><b>Keys follow the chord</b><kbd>Shift</kbd>+<kbd>K</kbd>: hold a chord on PLAY, and what you play on the other pages lands on it.</li>
    <li><b>Your F keys</b>Each opens a page, a view, an instrument or nothing: on FILE's KEYS view, or drag the tabs.</li>
    <li><b>In time with others</b>MIDI in and out over serial ports and USB, its clock both ways, and Ableton Link's tempo and beat.</li>
    <li><b>On a floppy</b>All of it fits on one 1.44 MB diskette, <a href="#floppy">for a PC with a floppy drive</a>.</li>
  </ul>
  <p class="small" style="margin-top:18px">How it got here: <a href="{REPO}/blob/main/CHANGES.md">CHANGES.md</a>.</p>
</div></section>

<section id="pages"><div class="wrap">
  <h2>{px('THE PAGES')}</h2>
  <p class="lede">A page on each F key, and they all play at once: a song on the tracker, a chord held on PLAY, the tape rolling.</p>
  <div class="tabs" role="tablist" aria-label="the pages">{tabs}</div>
  <div class="deck">{panels}</div>
</div></section>

<section id="get"><div class="wrap">
  <h2>{px('GET IT')}</h2>
  <p class="lede">You need a USB stick of 256 MB or more — everything on it is replaced — and a PC that can start from USB.</p>
  <ol class="steps">
    <li><div>
      <h3>Download the image</h3>
      <div class="dl">
        <a class="btn primary" href="{DL}/{ZIP}">{ZIP}{f" <small>{zs}</small>" if zs else ""}</a>
        <a class="btn" href="{DL}/{XZ}">{XZ}{f" <small>{xs}</small>" if xs else ""}</a>
      </div>
      <p class="small">The same image twice: the <code>.zip</code> for Windows and macOS, the <code>.xz</code> for Linux. There is also <a href="{DL}/{ISO}">an ISO</a> for a CD or a virtual machine and <a href="#floppy">a floppy</a> (neither keeps projects), and <a href="{DL}/SHA256SUMS">SHA256SUMS</a>.</p>
    </div></li>
    <li><div>
      <h3>Write it to the stick</h3>
      <div class="os" role="tablist" aria-label="your computer">
        <button role="tab" id="o-win" aria-controls="os-win" aria-selected="true">Windows</button>
        <button role="tab" id="o-mac" aria-controls="os-mac" aria-selected="false" tabindex="-1">macOS</button>
        <button role="tab" id="o-linux" aria-controls="os-linux" aria-selected="false" tabindex="-1">Linux</button>
      </div>
      <div class="ospanel on" role="tabpanel" id="os-win" aria-labelledby="o-win"><h4>WINDOWS</h4><ol>
        <li>Get <a href="https://etcher.balena.io/">balenaEtcher</a>, <a href="https://rufus.ie/">Rufus</a> or <a href="https://www.raspberrypi.com/software/">Raspberry Pi Imager</a>.</li>
        <li>Choose the <code>.zip</code> you downloaded (in Raspberry Pi Imager: <i>Choose OS</i>, then <i>Use custom</i>), then the stick, and write.</li>
        <li>Windows may then offer to format the stick: say no. It can't read the part where BARE! keeps projects, and doesn't need to.</li>
      </ol></div>
      <div class="ospanel" role="tabpanel" id="os-mac" aria-labelledby="o-mac"><h4>MACOS</h4><ol>
        <li>Get <a href="https://etcher.balena.io/">balenaEtcher</a> or <a href="https://www.raspberrypi.com/software/">Raspberry Pi Imager</a>.</li>
        <li>In Etcher: <i>Flash from file</i> and the <code>.zip</code> you downloaded, <i>Select target</i> and the stick, <i>Flash</i>. (In Raspberry Pi Imager: <i>Choose OS</i>, then <i>Use custom</i>.)</li>
        <li>If macOS then says a disk isn't readable, choose <i>Ignore</i>: that is where BARE! keeps projects.</li>
      </ol></div>
      <div class="ospanel" role="tabpanel" id="os-linux" aria-labelledby="o-linux"><h4>LINUX</h4>
        <p>One line downloads the image and writes it. First find the stick: <code>lsblk</code> lists the disks, and the one with your stick's size is it, <code>sdb</code> for example.</p>
        <div class="cmd"><code id="curl">{e(curl)}</code><button type="button" class="copy" data-for="curl">copy</button></div>
        <p class="warn">Put the stick's name where <code>sdX</code> is. Whatever disk you name is overwritten.</p>
        <p class="small">Or balenaEtcher with the <code>.zip</code>, as on the other systems.</p>
      </div>
      <p class="small" style="margin-top:14px">Why not straight from this page? A web page can't write to a USB stick: browsers keep disks out of its reach, on purpose, and their USB access leaves storage devices out. So it takes one of these tools, once.</p>
    </div></li>
    <li><div>
      <h3>Start the computer from it</h3>
      <p>Turn Secure Boot off in the firmware settings. Plug the stick in and pick it from the boot menu: usually <kbd>F12</kbd>, <kbd>F9</kbd>, <kbd>F8</kbd> or <kbd>Esc</kbd> at power-on.</p>
    </div></li>
    <li><div>
      <h3>Play</h3>
      <p>When the splash plays, press any key. Hold <kbd>6</kbd> and run a finger along the keys from <kbd>Z</kbd> to <kbd>/</kbd>: a C major chord, strummed. The F keys open the pages; <kbd>Shift</kbd>+<kbd>?</kbd> shows every key.</p>
    </div></li>
  </ol>
  <p class="update"><b>Already have a BARE! stick?</b> Put <a href="{DL}/BARE.UPD">BARE.UPD</a> in its root and start from it: it updates itself and keeps your projects.</p>
  <div class="floppy" id="floppy">
    <div>
      <h3>Got a floppy drive?</h3>
      <p>BARE! fits on one 3.5" 1.44 MB diskette. It starts a PC that has a BIOS and a floppy drive, an internal one or a USB drive the BIOS boots from, and plays like the stick, but can't save.</p>
      <a class="btn" href="{DL}/{FLOPPY}">{FLOPPY}{f" <small>{fs}</small>" if fs else ""}</a>
    </div>
    <div>
      <ol>
        <li><b>Linux:</b> <code>sudo dd if={FLOPPY} of=/dev/fd0 bs=18k</code>. A USB floppy drive is a <code>/dev/sdX</code>: <code>lsblk</code> shows which.</li>
        <li><b>Windows:</b> <a href="http://www.chrysocome.net/rawwrite">RawWrite for Windows</a>: choose the image and drive A:, then Write.</li>
        <li><b>macOS</b>, with a USB floppy drive: <code>diskutil list</code> finds it, <code>diskutil unmountDisk /dev/diskN</code>, then <code>sudo dd if={FLOPPY} of=/dev/rdiskN bs=18k</code>.</li>
        <li>Put the floppy first in the BIOS boot order and start the PC. The drive reads for about a minute, then the splash plays.</li>
      </ol>
      <p class="small" style="margin:14px 0 0">Booted in emulators so far. GRUB starts it: Limine, the stick's boot loader, can't read floppy drives.</p>
    </div>
  </div>
  <div class="facts">
    <div><b>PLAYED ON</b>A ThinkPad X250 and an ASUS VivoBook (Intel 12th gen), BIOS and UEFI. It boots on 90 emulated PCs, from a Pentium II with 32 MB to 4K screens.</div>
    <div><b>NOT TRIED YET</b>On real hardware: AC'97, the Sound Blaster 16, SATA and NVMe disks, the installer and Ethernet. Tried it? <a href="{REPO}/issues">Say how it went.</a></div>
    <div><b>CAREFUL</b>The installer on the FILE page erases the disk you choose. Everything else leaves the computer as it was.</div>
  </div>
</div></section>

<section id="{'video' if video_len else 'splash'}"><div class="wrap">
  <h2>{px('THE VIDEO' if video_len else 'THE SPLASH')}</h2>
  {f'<p class="lede">{video_len} of all of it, recorded from the instrument itself, picture and sound together.</p>' if video_len else ''}
  {'<div class="screen"><video src="media/bare-showcase.mp4" poster="img/showcase-poster.png" controls preload="none"></video></div>' if video_len else ''}
  <div class="splash">
    <figure><img src="img/splash-ans.png" alt="" width="1280" height="800" loading="lazy"><figcaption>SPLASH · ANS</figcaption></figure>
    <figure><img src="img/splash-gendy.png" alt="" width="1280" height="800" loading="lazy"><figcaption>SPLASH · GENDY</figcaption></figure>
    <figure><img src="img/splash-cmi.png" alt="" width="1280" height="800" loading="lazy"><figcaption>SPLASH · CMI</figcaption></figure>
    <figure><img src="img/splash-meta.png" alt="" width="1280" height="800" loading="lazy"><figcaption>SPLASH · METASTASEIS</figcaption></figure>
  </div>
  <p class="small">A different splash each time it starts: a short piece, picture and sound.</p>
</div></section>

<section id="more"><div class="wrap">
  <div class="two">
    <div class="card">
      {px('INSTRUMENTS', 'px small')}
      <p>An instrument is a text file on the stick: what it sounds like, and how it is played — the letter rows, the touchpad as a strip or a grid of pads, an arpeggio, a drone. Five come with it. SHRUTI is a shruti box: its keys open reeds tuned in just intonation, and a finger moving on the touchpad pumps the bellows.</p>
<pre class="file"><span class="c"># SHRUTI.TXT, less its comments</span>
<span class="k">name:</span> SHRUTI
<span class="s">[sound]</span>
<span class="k">wave:</span> pulse   <span class="k">width:</span> 22   <span class="k">detune:</span> 8
<span class="k">attack:</span> 160   <span class="k">release:</span> 500
<span class="s">[play]</span>
<span class="k">keys:</span> chromatic C3
<span class="k">tuning:</span> just
<span class="k">hold:</span> toggle
<span class="k">bellows:</span> yes</pre>
      <p class="small" style="margin-bottom:0">The format: <a href="{REPO}/blob/main/INSTRUMENTS.md">INSTRUMENTS.md</a>.</p>
    </div>
    <div class="card">
      {px('IDDQD', 'px small')}
      <p>Type <kbd>i</kbd><kbd>d</kbd><kbd>d</kbd><kbd>q</kbd><kbd>d</kbd> on any page and it runs Doom, with the WAD on your stick: your own, or Freedoom's. The F keys go back to the synthesizer, and Doom waits. Its music is played by BARE!'s own voices.</p>
      <img src="img/doom.png" alt="Doom (Freedoom) running in BARE!" loading="lazy">
    </div>
  </div>
</div></section>

<section id="manual"><div class="wrap">
  <h2>{px('THE MANUAL')}</h2>
  <div class="manual" style="margin-top:30px">
    <a href="{MANUAL}"><img src="img/manual-cover.png" alt="the manual's cover" loading="lazy"></a>
    <a href="{MANUAL}"><img src="img/manual-keys.png" alt="the manual's page about the keys" loading="lazy"></a>
    <div>
      <p>{manual_pages} A4 pages: every page of the instrument with its keys, the instrument files, which machines, and the stick. Made to be printed.</p>
      <div class="btns"><a class="btn" href="{MANUAL}">{MANUAL}</a><a class="btn" href="{REPO}/blob/main/MANUAL.md">read it on GitHub</a></div>
    </div>
  </div>
</div></section>

<section id="next"><div class="wrap">
  <h2>{px('NEXT')}</h2>
  <ul class="next">
    <li><b>Hardware controllers made for BARE!</b> Boxes of keys, pads, knobs and encoders laid out for its pages — an omnichord's chord buttons and strum plate, a tracker's step keys — built on RP2040 or ESP32-S3 boards, talking USB MIDI.</li>
    <li><b>A musical Etch A Sketch.</b> Two knobs draw one unbroken line, and then the drawing plays.</li>
    <li><b>More machines.</b> An EHCI driver, Realtek Ethernet, and ports: Raspberry Pi, PowerPC Mac, coreboot.</li>
  </ul>
</div></section>

<footer><div class="wrap">
  {px('BARE!')}
  <div>
    <p>BARE! {LABEL} is free software, GPL-3.0-or-later, by willbearfruits. <a href="{REPO}">Source</a> · <a href="{TAG}">release</a> · <a href="{MANUAL}">manual</a> · <a href="{REPO}/issues">issues</a></p>
    <p>It starts with the Limine bootloader, draws with the Terminus font, and runs Doom with Chocolate Doom's engine (GPL-2.0-or-later); this page is set in IBM Plex. Doom is a trademark of id Software; Freedoom is its own project. None of them is involved.</p>
  </div>
</div></footer>

<script>
(() => {{
  // tabs: the pages, the computers; arrow keys move along, as the ARIA pattern has it
  for (const list of document.querySelectorAll('[role=tablist]')) {{
    const tabs = [...list.querySelectorAll('[role=tab]')];
    const show = (t, focus) => {{
      for (const u of tabs) {{
        const on = u === t;
        u.setAttribute('aria-selected', on); u.tabIndex = on ? 0 : -1;
        document.getElementById(u.getAttribute('aria-controls')).classList.toggle('on', on);
      }}
      if (focus) {{ t.focus(); t.scrollIntoView({{block: 'nearest', inline: 'nearest'}}); }}
    }};
    list.show = show;
    list.addEventListener('click', ev => {{ const t = ev.target.closest('[role=tab]'); if (t) show(t); }});
    list.addEventListener('keydown', ev => {{
      const i = tabs.indexOf(document.activeElement); if (i < 0) return;
      const j = {{ArrowRight: i + 1, ArrowDown: i + 1, ArrowLeft: i - 1, ArrowUp: i - 1, Home: 0, End: tabs.length - 1}}[ev.key];
      if (j === undefined) return;
      ev.preventDefault(); show(tabs[(j + tabs.length) % tabs.length], true);
    }});
  }}
  // a page's views: the picture and its caption
  for (const box of document.querySelectorAll('.views')) {{
    const panel = box.closest('.panel'), img = panel.querySelector('.screen img'), cap = panel.querySelector('.vcap');
    box.addEventListener('click', ev => {{
      const b = ev.target.closest('button'); if (!b) return;
      for (const u of box.querySelectorAll('button')) u.setAttribute('aria-pressed', u === b);
      img.src = b.dataset.img; img.alt = b.dataset.alt; cap.textContent = b.dataset.cap;
    }});
  }}
  // the visitor's computer first
  const ua = navigator.userAgent, os = /Windows/.test(ua) ? 'win' : /Macintosh|Mac OS X/.test(ua) && !/iPhone|iPad/.test(ua) ? 'mac' : /Linux|X11|CrOS/.test(ua) && !/Android/.test(ua) ? 'linux' : 'win';
  const osTab = document.getElementById('o-' + os); osTab.closest('[role=tablist]').show(osTab);
  // copy buttons
  for (const b of document.querySelectorAll('button.copy')) b.addEventListener('click', async () => {{
    const code = document.getElementById(b.dataset.for);
    try {{ await navigator.clipboard.writeText(code.textContent); b.textContent = 'copied'; }}
    catch {{ getSelection().selectAllChildren(code); b.textContent = 'select'; }}
    setTimeout(() => b.textContent = 'copy', 1600);
  }});
  // the lit tab in the bar follows the section in view
  const lit = new Map([...document.querySelectorAll('.bar a.tab[href^="#"]')].map(a => [a.getAttribute('href').slice(1), a]));
  const io = new IntersectionObserver(es => {{ for (const x of es) if (x.isIntersecting)
    for (const [id, a] of lit) a.classList.toggle('lit', id === x.target.id); }}, {{rootMargin: '-40% 0px -55% 0px'}});
  for (const id of lit.keys()) {{ const s = document.getElementById(id); if (s) io.observe(s); }}
  // no moving pictures for those who asked for none
  const v = document.getElementById('loop');
  if (v && matchMedia('(prefers-reduced-motion: reduce)').matches) {{ v.removeAttribute('autoplay'); v.pause(); v.controls = true; }}
}})();
</script>
</body>
</html>
'''

if __name__ == '__main__':
    build()
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(OUT) for f in fs)
    print(f'{OUT}: {total >> 20} MiB')
