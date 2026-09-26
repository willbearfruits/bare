#!/usr/bin/env python3
"""The long showcase: every part of BARE! in one video. Each part is recorded by build/host/video (a scripted
performance through the host harness: picture and sound from the same simulated clock), with a title card before it
in BARE!'s own font, and the parts are joined with short fades.
Usage: tools/showcase.py OUT.mp4        (needs build/host/video, build/i386/bare-doom.img — make doom-img — and ffmpeg)"""
import os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__)) + '/..'
WORK = f'{HERE}/build/host/out/showcase'
VIDEO = f'{HERE}/build/host/video'
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else f'{HERE}/build/host/out/showcase.mp4')

# (title, line) cards and the parts after them: video's options, or a splash piece's number
PARTS = [
    ('card', 'BARE!', 'a synthesizer with no operating system', 4),
    ('card', 'SPLASH', 'a different piece each time it boots', 3),
    ('splash', 0), ('splash', 1), ('splash', 2), ('splash', 3),
    ('card', 'THE PAGES', 'PLAY  SEQ  WAVE  OPERATOR  STRETCH  TAPE  MIX', 3),
    ('video', []),
    ('card', 'SEQ', 'a tracker, playing the BARE METAL demo', 3),
    ('video', ['--seq']),
    ('card', 'COLOURS', 'ten colour schemes, Shift+H', 3),
    ('video', ['--themes']),
    ('card', 'INSTRUMENTS', 'made from text files: GRIDPADS, SIEVHARP, STYLO, THEREMIN', 3),
    ('video', ['--inst']),
    ('card', 'SHRUTI', 'a drone box: the keys open reeds, the touchpad is its bellows', 3),
    ('video', ['--shruti']),
    ('card', 'TOUCH', 'a crackle box: your fingers close the circuit', 3),
    ('video', ['--touch']),
    ('card', 'FX', 'effects played live over a song', 3),
    ('video', ['--fx']),
    ('card', 'ANS', 'a plate of 360 tones, played by a moving slit', 3),
    ('video', ['--ans']),
    ('card', 'XENAKIS', 'GENDY, sieves, clouds and UPIC', 3),
    ('video', ['--xen']),
    ('card', 'FILE', 'projects on the stick, songs, MIDI and the log', 3),
    ('video', ['--file', 'STICK']),
    ('card', 'DOOM', 'type iddqd on any page', 3),
    ('video', ['--doom', 'STICK']),
    ('card', 'BARE!', 'free software, GPL-3.0 · for any PC since the Pentium Pro', 5),
]

def record(i, part):
    path = f'{WORK}/{i:02d}.mp4'
    if part[0] == 'card':
        args = ['--card', part[1], part[2], str(part[3])]
    elif part[0] == 'splash':
        args = ['--splash', str(part[1])]
    else:                                                  # a stick of its own for each part that needs one
        stick = f'{WORK}/{i:02d}-stick.img'
        args = [stick if a == 'STICK' else a for a in part[1]]
        if 'STICK' in part[1]: shutil.copyfile(f'{HERE}/build/i386/bare-doom.img', stick)
    r = subprocess.run([VIDEO, *args, path], capture_output=True, text=True, cwd=HERE)
    if r.returncode: sys.exit(f'part {i} {args}: {r.stderr or r.stdout}')
    if os.path.exists(f'{WORK}/{i:02d}-stick.img'): os.remove(f'{WORK}/{i:02d}-stick.img')
    return path

def duration(path):
    return float(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', path],
                                capture_output=True, text=True).stdout)

os.makedirs(WORK, exist_ok=True)
with ThreadPoolExecutor(max_workers=4) as pool:
    files = list(pool.map(lambda ip: record(*ip), enumerate(PARTS)))
# joined: each part fades in and out (a quarter second), then all in a row
F = 0.25
inputs, chains, labels = [], [], []
for i, f in enumerate(files):
    d = duration(f)
    inputs += ['-i', f]
    chains.append(f'[{i}:v]fade=t=in:st=0:d={F},fade=t=out:st={d - F:.3f}:d={F},setsar=1[v{i}];'
                  f'[{i}:a]afade=t=in:st=0:d={F},afade=t=out:st={d - F:.3f}:d={F}[a{i}]')
    labels.append(f'[v{i}][a{i}]')
graph = ';'.join(chains) + ';' + ''.join(labels) + f'concat=n={len(files)}:v=1:a=1[v][a]'
subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', *inputs, '-filter_complex', graph, '-map', '[v]', '-map', '[a]',
                '-c:v', 'libx264', '-preset', 'slow', '-crf', '21', '-tune', 'animation', '-pix_fmt', 'yuv420p',
                '-c:a', 'aac', '-b:a', '256k', '-movflags', '+faststart', out], check=True)
print(f'{out}: {duration(out):.1f} s, {os.path.getsize(out) >> 20} MiB, {len(files)} parts')
