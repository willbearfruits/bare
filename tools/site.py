#!/usr/bin/env python3
"""BARE!'s web page: one page in the instrument's own NIGHT colours, its headings in BARE!'s screen font
(tools/pixelfont.py), a loop and the showcase video, every page's picture, how to try it, the manual.
Usage: tools/site.py [OUTDIR]      (default build/site; serve it with python3 -m http.server)
Uses what tools/manual.py and tools/showcase.py made: build/manual (the PDF, the NIGHT pictures), build/host/out/showcase
(the parts the loop is cut from), media/2.5/bare-showcase-1280.mp4 (the video), build/fonts (IBM Plex)."""
import html, os, re, shutil, subprocess, sys

HERE = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/..')
sys.path.insert(0, HERE + '/tools')
import pixelfont

OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else f'{HERE}/build/site'
RELEASE = re.search(r'#define BARE_RELEASE "([^"]+)"', open(f'{HERE}/core/app.h').read()).group(1)
REPO = 'https://github.com/willbearfruits/bare'
MANUAL = f'bare-manual-{RELEASE}.pdf'
STAGE = 'beta'                                              # '' once a release is final
LABEL = f'{RELEASE} {STAGE}'.strip()

def px(text, cls='px', font='ter-u32b'): return pixelfont.svg(text, font, cls)
def e(t): return html.escape(t, quote=True)

PAGES = [
    ('F1', 'PLAY', 'play', 'An omnichord: chord buttons on the number row, a strum plate on the letters and the touchpad, a rhythm section with a bass that follows the chord.'),
    ('F2', 'SEQ', 'seq', 'A tracker: patterns of eight channels, an order list, effect columns, every note on the exact sample.'),
    ('F3', 'WAVE', 'wave', 'A Fairlight-style page, green on black: waves drawn with the touchpad, harmonics, a 3D stack of them, and a sampler.'),
    ('F4', 'STRETCH', 'stretch', 'Freezes the phrase you just played and stretches it, up to a thousand times slower, while you play on.'),
    ('F5', 'OPERATOR', 'fm', 'Four-operator FM: eight algorithms, envelopes, an LFO, four patches to play anywhere.'),
    ('F6', 'TAPE', 'tape-playing', 'An eight-track tape with reels, wow and hiss: record, overdub, loop, bounce, build whole songs.'),
    ('F7', 'FILE', 'file', 'Projects on the stick, songs out as WAV, MIDI and Ableton Link, the log, and an installer.'),
    ('F8', 'MIX', 'mix', 'A strip for every source, sends to an echo and a reverb, and a filter, drive and crusher on the master.'),
    ('F9', 'TOUCH', 'touch', 'A crackle box: eight bare pads of a small circuit. Two fingers close it, and pressing harder bends it.'),
    ('F10', 'FX', 'fx', 'Effects to play live over a song: repeat, reverse, tape stop, gate, a dub throw, a reverb freeze.'),
    ('F11', 'ANS', 'ans', "Murzin's ANS: a plate of 360 tones read by a moving slit. Draw on it, or let the camera be the score."),
    ('F12', 'XENAKIS', 'xen-upic', "Four of Xenakis's ways of making music: UPIC's drawn arcs, GENDY, stochastic clouds and sieves."),
]

LOOP = [('03', 1.5, 3.5), ('23', 44, 4), ('21', 3, 3.5), ('15', 4, 3.5), ('17', 13, 3), ('19', 5, 3), ('27', 17, 4)]
def loop():
    """a silent loop for the top of the page, cut from tools/showcase.py's parts: the GENDY splash, UPIC, ANS, SHRUTI,
    TOUCH, FX and Doom"""
    out, parts = f'{HERE}/build/site-loop.mp4', f'{HERE}/build/host/out/showcase'
    if os.path.exists(out) or not os.path.exists(f'{parts}/27.mp4'): return out
    tmp = f'{HERE}/build/site-loop'
    os.makedirs(tmp, exist_ok=True)
    with open(f'{tmp}/list.txt', 'w') as lst:
        for k, (part, at, length) in enumerate(LOOP):
            subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-ss', str(at), '-t', str(length), '-i', f'{parts}/{part}.mp4', '-an',
                            '-vf', 'scale=1280:800:flags=neighbor,fps=30', '-c:v', 'libx264', '-preset', 'slow', '-crf', '26',
                            '-pix_fmt', 'yuv420p', f'{tmp}/c{k}.mp4'], check=True)
            lst.write(f"file 'c{k}.mp4'\n")
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', f'{tmp}/list.txt', '-c', 'copy', out], check=True)
    shutil.rmtree(tmp)
    return out

def build():
    loop()
    if os.path.exists(OUT): shutil.rmtree(OUT)
    for d in ('img', 'media', 'fonts'): os.makedirs(f'{OUT}/{d}')
    night = f'{HERE}/build/manual/shots/night'
    for f in ['play', 'seq', 'wave', 'stretch', 'fm', 'tape-playing', 'file', 'mix', 'touch', 'fx', 'ans', 'xen-upic',
              'xen-gendy', 'xen-clouds', 'xen-sieves', 'inst-shruti', 'inst-stylo', 'inst-gridpads']:
        shutil.copy(f'{night}/{f}.png', f'{OUT}/img/{f}.png')
    for f in ['doom', 'splash-ans', 'splash-gendy', 'splash-cmi', 'splash-meta']:
        shutil.copy(f'{HERE}/build/manual/img/{f}.png', f'{OUT}/img/{f}.png')
    shutil.copy(f'{HERE}/build/manual/{MANUAL}', f'{OUT}/{MANUAL}')
    subprocess.run(['pdftoppm', '-png', '-r', '72', '-f', '1', '-l', '1', '-singlefile', f'{OUT}/{MANUAL}', f'{OUT}/img/manual-cover'], check=True)
    text = subprocess.run(['pdftotext', f'{OUT}/{MANUAL}', '-'], capture_output=True, text=True, check=True).stdout
    keys = next((n + 1 for n, t in enumerate(text.split('\f')) if '§keys§' in t), 4)          # the page with the keyboard map
    subprocess.run(['pdftoppm', '-png', '-r', '72', '-f', str(keys), '-l', str(keys), '-singlefile', f'{OUT}/{MANUAL}', f'{OUT}/img/manual-keys'], check=True)
    clip = f'{HERE}/build/site-loop.mp4'
    if os.path.exists(clip): shutil.copy(clip, f'{OUT}/media/loop.mp4')
    shutil.copy(f'{HERE}/media/{RELEASE}/bare-showcase-1280.mp4', f'{OUT}/media/bare-showcase.mp4')
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-ss', '3', '-i', f'{OUT}/media/bare-showcase.mp4', '-frames:v', '1', f'{OUT}/img/showcase-poster.png'], check=True)
    for f in os.listdir(f'{HERE}/build/fonts'):
        if f.endswith('.woff2') or f == 'OFL.txt': shutil.copy(f'{HERE}/build/fonts/{f}', f'{OUT}/fonts/{f}')
    faces = open(f'{HERE}/build/fonts/fonts.css').read().replace("url('", "url('fonts/")
    open(f'{OUT}/favicon.svg', 'w').write(pixelfont.svg('B', 'ter-u32b', 'icon', trim=True)
        .replace('<svg ', '<svg style="background:#0c0f14" ').replace('currentColor', '#ffb454'))
    open(f'{OUT}/index.html', 'w').write(page(faces))
    open(f'{OUT}/.nojekyll', 'w').write('')

def page(faces):
    cards = ''.join(f'''
      <a class="card" href="img/{img}.png"><div class="shot"><img src="img/{img}.png" alt="the {name} page" loading="lazy"></div>
        <div class="cap"><div class="ct"><kbd>{fk}</kbd><span>{name}</span></div><p>{e(text)}</p></div></a>''' for fk, name, img, text in PAGES)
    steps = [
        f'Download <code>bare-{RELEASE}.img.xz</code> from the <a href="{REPO}/releases/tag/v{RELEASE}">{RELEASE} release</a>.',
        f'Write it to a USB stick of 256 MB or more with balenaEtcher or Raspberry Pi Imager, or on Linux with <code>xz -dc bare-{RELEASE}.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync</code>. Everything on the stick is replaced.',
        'Turn Secure Boot off, plug the stick in and start the computer from it: the boot menu is usually <kbd>F12</kbd>, <kbd>F9</kbd>, <kbd>F8</kbd> or <kbd>Esc</kbd> at power-on.',
        "When the splash plays, press any key. Hold <kbd>4</kbd> and run a finger along the keys from <kbd>A</kbd> to <kbd>'</kbd>: a C major chord, strummed.",
    ]
    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BARE! · a synthesizer with no operating system</title>
<meta name="description" content="A synthesizer that is the whole computer. It starts from a USB stick straight into the instrument, with no operating system underneath.">
<meta property="og:title" content="BARE!">
<meta property="og:description" content="A synthesizer that is the whole computer: no operating system, from a USB stick, on PCs from the late 1990s to now.">
<meta property="og:image" content="img/play.png">
<link rel="icon" href="favicon.svg">
<style>
{faces}
:root {{ --bg: #0c0f14; --panel: #141a22; --border: #2c3746; --dim: #5a6879; --text: #b6c2ce; --bright: #f0f4f7;
  --amber: #ffb454; --amber-d: #6e5025; --green: #7ee787; --cyan: #58c7f3; --pink: #f286c4; --scope: #5ef0a8; }}
* {{ box-sizing: border-box; }}
html {{ background: var(--bg); color: var(--text); font: 17px/1.6 'IBM Plex Sans', system-ui, sans-serif; scroll-behavior: smooth; }}
body {{ margin: 0; }}
a {{ color: var(--cyan); text-decoration: none; }} a:hover {{ text-decoration: underline; }}
code {{ font: .82em 'IBM Plex Mono', monospace; color: var(--bright); background: var(--panel); border: 1px solid var(--border); border-radius: 4px; padding: .05em .35em; word-break: break-word; }}
kbd {{ font: 500 .78em 'IBM Plex Mono', monospace; color: var(--bright); background: var(--bg); border: 1px solid var(--dim); border-bottom-width: 2.5px; border-radius: 5px; padding: .08em .45em; white-space: nowrap; }}
.wrap {{ max-width: 1120px; margin: 0 auto; padding: 0 24px; }}
svg.px {{ display: block; height: clamp(26px, 7vw, 44px); width: auto; max-width: 100%; color: var(--amber); }}
svg.px.big {{ height: clamp(90px, 17vw, 190px); color: var(--bright); }}
svg.px.small {{ height: 22px; }}

/* the title bar, as the instrument draws it */
.bar {{ position: sticky; top: 0; z-index: 10; background: rgba(20, 26, 34, .94); backdrop-filter: blur(6px); border-bottom: 1px solid var(--border); }}
.bar .wrap {{ display: flex; align-items: center; gap: 4px; height: 44px; font: 500 13px 'IBM Plex Mono', monospace; overflow-x: auto; scrollbar-width: none; }}
.bar .home {{ color: var(--bright); margin-right: 14px; white-space: nowrap; }} .bar .home b {{ color: var(--amber); font-weight: 500; }}
.bar a.tab {{ color: var(--text); padding: 3px 9px; border-radius: 3px; white-space: nowrap; flex-shrink: 0; }}
.bar .home {{ flex-shrink: 0; }}
.bar a.tab span {{ color: var(--dim); margin-right: 5px; }}
.bar a.tab:hover {{ background: var(--amber); color: var(--bg); text-decoration: none; }} .bar a.tab:hover span {{ color: var(--amber-d); }}

.hero {{ padding: 72px 0 40px; }}
.hero .tag {{ font-size: clamp(24px, 3.4vw, 36px); line-height: 1.25; color: var(--bright); margin: 34px 0 12px; max-width: 30ch; letter-spacing: -.01em; }}
.hero .sub {{ font-size: 19px; max-width: 58ch; margin: 0; }}
.cta {{ display: flex; flex-wrap: wrap; gap: 12px; margin: 30px 0 48px; }}
.btn {{ font: 500 15px 'IBM Plex Mono', monospace; padding: 11px 18px; border: 1px solid var(--border); border-radius: 6px; color: var(--bright); background: var(--panel); }}
.btn:hover {{ border-color: var(--amber); text-decoration: none; }}
.btn.primary {{ background: var(--amber); border-color: var(--amber); color: var(--bg); }} .btn.primary:hover {{ background: #ffc677; }}
.screen {{ border: 1px solid var(--border); border-radius: 12px; overflow: hidden; background: #000; box-shadow: 0 0 0 7px var(--panel), 0 40px 90px -20px rgba(0, 0, 0, .8); }}
.screen video, .screen img {{ display: block; width: 100%; height: auto; aspect-ratio: 16 / 10; }}
.hero .note {{ font: 13px 'IBM Plex Mono', monospace; color: var(--dim); margin-top: 18px; }}

section {{ padding: 72px 0 24px; }}
section > .wrap > p.lede {{ font-size: 19px; max-width: 62ch; margin: 18px 0 30px; }}
h2 {{ margin: 0; }}
.split {{ display: grid; grid-template-columns: 1fr 1fr; gap: 44px; align-items: center; }}
.split > *, .manual > *, section > .wrap > *, .hero > .wrap > * {{ min-width: 0; }}
.split p {{ max-width: 52ch; }}

.cards {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(320px, 1fr)); gap: 22px; margin-top: 34px; }}
.card {{ display: block; background: var(--panel); border: 1px solid var(--border); border-radius: 10px; overflow: hidden; color: var(--text); transition: border-color .15s, transform .15s; }}
.card:hover {{ border-color: var(--amber); text-decoration: none; transform: translateY(-2px); }}
.card .shot img {{ display: block; width: 100%; height: auto; aspect-ratio: 16 / 10; }}
.card .cap {{ padding: 14px 16px 16px; border-top: 1px solid var(--border); }}
.card .ct {{ display: flex; align-items: center; gap: 10px; font: 600 15px 'IBM Plex Mono', monospace; color: var(--bright); letter-spacing: .04em; }}
.card .ct kbd {{ color: var(--amber); border-color: var(--amber-d); }}
.card p {{ margin: 8px 0 0; font-size: 15px; line-height: 1.5; }}

.views {{ display: grid; grid-template-columns: repeat(4, 1fr); gap: 14px; margin-top: 26px; }}
.views figure {{ margin: 0; }} .views img {{ width: 100%; display: block; border: 1px solid var(--border); border-radius: 8px; }}
.views figcaption, .splash figcaption {{ font: 12px 'IBM Plex Mono', monospace; color: var(--dim); margin-top: 7px; letter-spacing: .06em; }}
.splash {{ display: grid; grid-template-columns: repeat(4, 1fr); gap: 14px; margin-top: 26px; }}
.splash figure {{ margin: 0; }} .splash img {{ width: 100%; display: block; border-radius: 8px; border: 1px solid var(--border); }}

pre.file {{ font: 13.5px/1.55 'IBM Plex Mono', monospace; color: var(--text); background: var(--panel); border: 1px solid var(--border); border-radius: 10px; padding: 20px 22px; margin: 0; overflow-x: auto; }}
pre.file .c {{ color: var(--dim); }} pre.file .s {{ color: var(--amber); }} pre.file .k {{ color: var(--cyan); }}

ol.steps {{ counter-reset: s; list-style: none; padding: 0; margin: 30px 0; display: grid; gap: 14px; }}
ol.steps li {{ counter-increment: s; display: grid; grid-template-columns: 44px 1fr; gap: 14px; align-items: start; background: var(--panel); border: 1px solid var(--border); border-radius: 10px; padding: 16px 18px; }}
ol.steps li::before {{ content: counter(s); font: 600 22px/1 'IBM Plex Mono', monospace; color: var(--amber); padding-top: 3px; }}
.facts {{ display: grid; grid-template-columns: repeat(3, 1fr); gap: 18px; margin-top: 10px; }}
.facts div {{ border-top: 2px solid var(--border); padding-top: 12px; font-size: 15px; }}
.facts b {{ display: block; font: 600 13px 'IBM Plex Mono', monospace; color: var(--bright); letter-spacing: .06em; margin-bottom: 6px; }}

.manual {{ display: grid; grid-template-columns: 280px 280px 1fr; gap: 26px; align-items: center; margin-top: 30px; }}
.manual img {{ width: 100%; display: block; border-radius: 4px; box-shadow: 0 24px 60px -18px rgba(0, 0, 0, .9); background: #fff; }}
ul.next {{ list-style: none; padding: 0; margin: 26px 0 0; display: grid; gap: 14px; }}
ul.next li {{ background: var(--panel); border: 1px solid var(--border); border-left: 3px solid var(--amber); border-radius: 8px; padding: 15px 18px; max-width: 80ch; }}
ul.next b {{ color: var(--bright); font-weight: 600; }}

footer {{ margin-top: 80px; border-top: 1px solid var(--border); padding: 36px 0 60px; font-size: 14px; color: var(--dim); }}
footer .wrap {{ display: grid; grid-template-columns: auto 1fr; gap: 36px; align-items: start; }}
footer p {{ margin: 0 0 8px; max-width: 80ch; }}
footer svg.px {{ height: 30px; color: var(--dim); }}

@media (max-width: 860px) {{
  html {{ font-size: 16px; }} .wrap {{ padding: 0 16px; }}
  .split, .manual, footer .wrap {{ grid-template-columns: 1fr; }}
  .views, .splash {{ grid-template-columns: 1fr 1fr; }} .facts {{ grid-template-columns: 1fr; }}
  .cards {{ grid-template-columns: 1fr; }} .hero {{ padding-top: 44px; }} section {{ padding-top: 56px; }}
  .manual img {{ max-width: 320px; }}
}}
</style>
</head>
<body>
<nav class="bar"><div class="wrap">
  <a class="home" href="#top"><b>◆</b> BARE!</a>
  <a class="tab" href="#pages"><span>F1</span>PAGES</a><a class="tab" href="#instruments"><span>F2</span>INSTRUMENTS</a>
  <a class="tab" href="#video"><span>F3</span>VIDEO</a><a class="tab" href="#try"><span>F4</span>TRY IT</a>
  <a class="tab" href="#manual"><span>F5</span>MANUAL</a><a class="tab" href="#next"><span>F6</span>NEXT</a>
  <a class="tab" href="{REPO}"><span>F7</span>SOURCE</a>
</div></nav>

<header class="hero" id="top"><div class="wrap">
  {px('BARE!', 'px big')}
  <p class="tag">A synthesizer that is the whole computer.</p>
  <p class="sub">It starts from a USB stick straight into the instrument, with no operating system underneath, on PCs from the late 1990s to now.</p>
  <div class="cta">
    <a class="btn primary" href="{REPO}/releases/tag/v{RELEASE}">Download {LABEL}</a>
    <a class="btn" href="{MANUAL}">The manual (PDF)</a>
    <a class="btn" href="{REPO}">Source</a>
  </div>
  <div class="screen"><video src="media/loop.mp4" poster="img/splash-gendy.png" autoplay muted loop playsinline></video></div>
  <p class="note">Recorded from the instrument itself: the GENDY splash, UPIC, ANS, SHRUTI, TOUCH, FX, and Doom.</p>
</div></header>

<section id="pages"><div class="wrap">
  <h2>{px('THE PAGES')}</h2>
  <p class="lede">Twelve pages, one for each F key. Everything plays at once: a song on the tracker, a chord held on PLAY, the tape rolling, the camera reading the ANS plate. It runs from memory, so once it has started the stick can come out.</p>
  <div class="cards">{cards}
  </div>
  <div class="views">
    <figure><img src="img/xen-gendy.png" alt="GENDY" loading="lazy"><figcaption>XENAKIS · GENDY</figcaption></figure>
    <figure><img src="img/xen-clouds.png" alt="clouds" loading="lazy"><figcaption>XENAKIS · CLOUDS</figcaption></figure>
    <figure><img src="img/xen-sieves.png" alt="sieves" loading="lazy"><figcaption>XENAKIS · SIEVES</figcaption></figure>
    <figure><img src="img/xen-upic.png" alt="UPIC" loading="lazy"><figcaption>XENAKIS · UPIC</figcaption></figure>
  </div>
</div></section>

<section id="instruments"><div class="wrap">
  <h2>{px('INSTRUMENTS')}</h2>
  <div class="split" style="margin-top:30px">
    <div>
      <p>An instrument is a text file on the stick: what it sounds like, and how it is played — the letter rows, the touchpad as a strip or a grid of pads, an arpeggio, a drone. Five come with it: STYLO, THEREMIN, GRIDPADS, SIEVHARP and SHRUTI.</p>
      <p>SHRUTI is a shruti box, the drone box of Indian music. Its keys open reeds tuned in just intonation, and a finger moving to and fro on the touchpad pumps the bellows.</p>
<pre class="file"><span class="c"># the whole of SHRUTI.TXT, less its comments</span>
<span class="k">name:</span> SHRUTI
<span class="s">[sound]</span>
<span class="k">wave:</span> pulse   <span class="k">width:</span> 22   <span class="k">detune:</span> 8
<span class="k">attack:</span> 160   <span class="k">release:</span> 500
<span class="s">[play]</span>
<span class="k">keys:</span> chromatic C3
<span class="k">tuning:</span> just
<span class="k">hold:</span> toggle
<span class="k">bellows:</span> yes</pre>
    </div>
    <div class="screen"><img src="img/inst-shruti.png" alt="the SHRUTI instrument" loading="lazy"></div>
  </div>
</div></section>

<section id="doom"><div class="wrap">
  <div class="split">
    <div class="screen"><img src="img/doom.png" alt="Doom in BARE!" loading="lazy"></div>
    <div>
      <h2>{px('IDDQD')}</h2>
      <p style="margin-top:22px">Type <kbd>i</kbd><kbd>d</kbd><kbd>d</kbd><kbd>q</kbd><kbd>d</kbd> on any page and it runs Doom, with the WAD on your stick: your own, or Freedoom's. The F keys go back to the synthesizer, and Doom waits. Its music is played by BARE!'s own voices.</p>
    </div>
  </div>
</div></section>

<section id="video"><div class="wrap">
  <h2>{px('THE VIDEO')}</h2>
  <p class="lede">Six and a half minutes of all of it, recorded from the instrument itself, picture and sound together.</p>
  <div class="screen"><video src="media/bare-showcase.mp4" poster="img/showcase-poster.png" controls preload="none"></video></div>
  <div class="splash">
    <figure><img src="img/splash-ans.png" alt="" loading="lazy"><figcaption>SPLASH · ANS</figcaption></figure>
    <figure><img src="img/splash-gendy.png" alt="" loading="lazy"><figcaption>SPLASH · GENDY</figcaption></figure>
    <figure><img src="img/splash-cmi.png" alt="" loading="lazy"><figcaption>SPLASH · CMI</figcaption></figure>
    <figure><img src="img/splash-meta.png" alt="" loading="lazy"><figcaption>SPLASH · METASTASEIS</figcaption></figure>
  </div>
</div></section>

<section id="try"><div class="wrap">
  <h2>{px('TRY IT')}</h2>
  <p class="lede">{RELEASE} is a beta. It needs a PC with a Pentium Pro or later.</p>
  <ol class="steps">{''.join(f'<li><div>{s}</div></li>' for s in steps)}</ol>
  <div class="facts">
    <div><b>PLAYED ON</b>A ThinkPad X250 and an ASUS VivoBook (Intel 12th gen), BIOS and UEFI. It boots on 89 emulated PCs, from a Pentium II with 32 MB to 4K screens.</div>
    <div><b>NOT TRIED YET</b>On real hardware: AC'97, the Sound Blaster 16, SATA and NVMe disks, the installer and Ethernet.</div>
    <div><b>CAREFUL</b>The installer on the FILE page erases the disk you choose. Everything else leaves the computer as it was.</div>
  </div>
</div></section>

<section id="manual"><div class="wrap">
  <h2>{px('THE MANUAL')}</h2>
  <div class="manual">
    <a href="{MANUAL}"><img src="img/manual-cover.png" alt="the manual's cover"></a>
    <a href="{MANUAL}"><img src="img/manual-keys.png" alt="the manual's keys page"></a>
    <div>
      <p>Thirty A4 pages: every page of the instrument with its keys, the instrument files, which machines, and the stick. Made to be printed.</p>
      <p><a class="btn" href="{MANUAL}">bare-manual-{RELEASE}.pdf</a></p>
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
    <p>BARE! {LABEL} is free software, GPL-3.0-or-later, by willbearfruits. <a href="{REPO}">Source</a> · <a href="{REPO}/releases/tag/v{RELEASE}">download</a> · <a href="{MANUAL}">manual</a></p>
    <p>It starts with the Limine bootloader, draws with the Terminus font, and runs Doom with Chocolate Doom's engine (GPL-2.0-or-later); this page is set in IBM Plex. Doom is a trademark of id Software; Freedoom is its own project. None of them is involved.</p>
  </div>
</div></footer>
</body>
</html>
'''

if __name__ == '__main__':
    build()
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(OUT) for f in fs)
    print(f'{OUT}: {total >> 20} MiB')
