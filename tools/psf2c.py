#!/usr/bin/env python3
"""psf2c.py — convert PSF2 console fonts into a C header with a custom 256-entry code page.

Usage: psf2c.py OUT.h NAME:FILE.psf[.gz] [NAME:FILE ...]
Each font is emitted as `font_<NAME>` (struct font). All fonts share one code page
(see CODEPAGE below) so UI code can address glyphs by index or by Unicode code point.
"""
import gzip, struct, sys

# index -> code point. 0x00-0x7F is ASCII; 0x80+ are the symbols the UI uses.
SYMBOLS = {
    0x80: '─', 0x81: '│', 0x82: '┌', 0x83: '┐', 0x84: '└', 0x85: '┘',
    0x86: '├', 0x87: '┤', 0x88: '┬', 0x89: '┴', 0x8A: '┼',
    0x8B: '═', 0x8C: '║', 0x8D: '╔', 0x8E: '╗', 0x8F: '╚', 0x90: '╝',
    0x91: '╠', 0x92: '╣', 0x93: '╦', 0x94: '╩', 0x95: '╬',
    0x96: '╭', 0x97: '╮', 0x98: '╰', 0x99: '╯', 0x9A: '━', 0x9B: '┃',
    0xA0: '█', 0xA1: '▀', 0xA2: '▄', 0xA3: '▌', 0xA4: '▐', 0xA5: '░', 0xA6: '▒', 0xA7: '▓',
    0xA8: '▁', 0xA9: '▂', 0xAA: '▃', 0xAB: '▄', 0xAC: '▅', 0xAD: '▆', 0xAE: '▇', 0xAF: '█',
    0xB0: '●', 0xB1: '○', 0xB2: '■', 0xB3: '□', 0xB4: '▪', 0xB5: '▶', 0xB6: '◀',
    0xB7: '▲', 0xB8: '▼', 0xB9: '♪', 0xBA: '♫', 0xBB: '♭', 0xBC: '♯', 0xBD: '◆', 0xBE: '◇', 0xBF: '•',
    0xC0: '·', 0xC1: '…', 0xC2: '→', 0xC3: '←', 0xC4: '↑', 0xC5: '↓', 0xC6: '°', 0xC7: '±',
    0xC8: '✓', 0xC9: '▸', 0xCA: '◂', 0xCB: '★', 0xCC: '☆', 0xCD: '§', 0xCE: '¶', 0xCF: '≈',
    0xD0: '▔', 0xD1: '▕', 0xD2: '▏', 0xD3: '◼', 0xD4: '◻', 0xD5: '▬', 0xD6: '⌂', 0xD7: '∞',
    0xD8: '▎', 0xD9: '▍', 0xDA: '▋', 0xDB: '▊', 0xDC: '▉', 0xDD: '×', 0xDE: '⇧',
}
# Fallbacks when a font lacks a glyph.
FALLBACK = {'●': '■', '▪': '■', '□': '○', '◇': '◆', '♭': 'b', '♯': '#', '±': '+', '▸': '▶', '◂': '◀', '╭': '┌', '╮': '┐', '╰': '└', '╯': '┘', '━': '─', '┃': '│', '✓': 'v', '★': '*', '☆': '*',
            '◼': '■', '◻': '□', '▬': '■', '⌂': '^', '∞': '8', '▔': '▀', '▕': '▐', '▏': '▌',
            '▁': '_', '▂': '_', '▃': '▄', '▅': '▄', '▆': '▀', '▇': '█', '≈': '~', '…': '.', '·': '.'}

def load_psf(path):
    data = gzip.open(path).read() if path.endswith('.gz') else open(path, 'rb').read()
    table = {}
    if data[:2] == b'\x36\x04':                      # PSF1
        mode, charsize = data[2], data[3]
        length = 512 if mode & 1 else 256
        width, height, hdrsize = 8, charsize, 4
        glyphs = [data[hdrsize + i * charsize: hdrsize + (i + 1) * charsize] for i in range(length)]
        if mode & 2:
            pos = hdrsize + length * charsize
            for gi in range(length):
                in_seq = False
                while pos + 1 < len(data):
                    u = struct.unpack('<H', data[pos:pos + 2])[0]; pos += 2
                    if u == 0xFFFF: break
                    if u == 0xFFFE: in_seq = True; continue
                    if not in_seq: table.setdefault(chr(u), gi)
        return glyphs, table, width, height
    if data[:4] != b'\x72\xb5\x4a\x86':
        raise SystemExit(f'{path}: not PSF1/PSF2')
    (ver, hdrsize, flags, length, charsize, height, width) = struct.unpack('<7I', data[4:32])
    glyphs = [data[hdrsize + i * charsize: hdrsize + (i + 1) * charsize] for i in range(length)]
    if flags & 1:
        pos = hdrsize + length * charsize
        for gi in range(length):
            seq = bytearray()
            while pos < len(data) and data[pos] != 0xFF:
                if data[pos] == 0xFE:      # sequence start: skip combining sequences
                    pos += 1
                    while pos < len(data) and data[pos] not in (0xFE, 0xFF):
                        pos += 1
                    continue
                seq.append(data[pos]); pos += 1
            pos += 1
            for ch in seq.decode('utf-8', 'replace'):
                table.setdefault(ch, gi)
    return glyphs, table, width, height

def to_rows(glyph, w, h, stride):
    return [[(glyph[r * stride + (c >> 3)] >> (7 - (c & 7))) & 1 for c in range(w)] for r in range(h)]

def from_rows(rows, w, h, stride):
    out = bytearray(h * stride)
    for r in range(h):
        for c in range(w):
            if rows[r][c]: out[r * stride + (c >> 3)] |= 0x80 >> (c & 7)
    return bytes(out)

def line_metrics(glyphs, table, w, h, stride):
    """Row span of the font's '─' and column span of its '│', so synthesised glyphs join its box drawing."""
    hl, vl = to_rows(glyphs[table['─']], w, h, stride), to_rows(glyphs[table['│']], w, h, stride)
    ys = [r for r in range(h) if sum(hl[r]) > w // 2]
    xs = [c for c in range(w) if sum(vl[r][c] for r in range(h)) > h // 2]
    return ys[0], len(ys), xs[0], len(xs)

def synth(ch, glyphs, table, w, h, stride):
    """Glyphs the console font lacks, drawn to match its metrics (Terminus has 256/512 glyphs; the UI wants more)."""
    import math
    g = [[0] * w for _ in range(h)]
    def fill(x0, y0, x1, y1):
        for y in range(max(0, y0), min(h, y1)):
            for x in range(max(0, x0), min(w, x1)): g[y][x] = 1
    ly, lt, lx, lw = line_metrics(glyphs, table, w, h, stride)
    eighths = {'▁': 1, '▂': 2, '▃': 3, '▅': 5, '▆': 6, '▇': 7}
    if ch in eighths:
        fill(0, h - round(h * eighths[ch] / 8), w, h)
    elif ch in '▏▎▍▋▊▉':
        fill(0, 0, max(1, round(w * ('▏▎▍ ▋▊▉'.index(ch) + 1) / 8)), h)
    elif ch in '▕▔':
        if ch == '▕': fill(w - max(1, w // 8), 0, w, h)
        else: fill(0, 0, w, max(1, h // 8))
    elif ch in '╭╮╰╯':
        # quarter circle from the vertical line to the horizontal one; straight line pieces beyond it
        r = min(w - lx, lx + lw) - 1
        r = max(2, min(r, h // 2 - 1))
        right = ch in '╭╰'              # the horizontal arm goes right
        down = ch in '╭╮'               # the vertical arm goes down
        cx = lx + (r if right else -r) + (0 if right else lw - 1)
        cy = ly + (r if down else -r) + (0 if down else lt - 1)
        for i in range(0, 91):
            a = math.radians(i)
            px = cx + (-1 if right else 1) * r * math.cos(a)
            py = cy + (-1 if down else 1) * r * math.sin(a)
            fill(round(px - (lw - 1) / 2) if lw > 1 else round(px), round(py - (lt - 1) / 2) if lt > 1 else round(py),
                 round(px - (lw - 1) / 2) + lw, round(py - (lt - 1) / 2) + lt)
        if down: fill(lx, cy, lx + lw, h)
        else: fill(lx, 0, lx + lw, cy + 1)
        if right: fill(cx, ly, w, ly + lt)
        else: fill(0, ly, cx + 1, ly + lt)
    elif ch in '━┃':
        if ch == '━': fill(0, ly - 1, w, ly + lt + 1)
        else: fill(lx - 1, 0, lx + lw + 1, h)
    elif ch in '●○◉':
        cx, cy, rad = (w - 1) / 2, h * 0.55 - 0.5, w * 0.34
        for y in range(h):
            for x in range(w):
                d = math.hypot(x - cx, (y - cy))
                if ch == '●' and d <= rad + 0.3: g[y][x] = 1
                if ch == '○' and rad - 1.1 <= d <= rad + 0.3: g[y][x] = 1
    elif ch in '▪◼■□◻':
        s = round(w * (0.45 if ch == '▪' else 0.7)); x0 = (w - s) // 2; y0 = round(h * 0.55 - s / 2)
        if ch in '□◻': fill(x0, y0, x0 + s, y0 + 1); fill(x0, y0 + s - 1, x0 + s, y0 + s); fill(x0, y0, x0 + 1, y0 + s); fill(x0 + s - 1, y0, x0 + s, y0 + s)
        else: fill(x0, y0, x0 + s, y0 + s)
    elif ch in '◇':
        cx, cy, rad = (w - 1) / 2, h * 0.55 - 0.5, w * 0.42
        for y in range(h):
            for x in range(w):
                if abs(abs(x - cx) + abs(y - cy) - rad) < 0.6: g[y][x] = 1
    elif ch in '▸◂':
        cy = round(h * 0.55) - 1; half = max(2, w // 3)
        for i in range(half + 1):
            x = (w // 2 - half // 2 + i) if ch == '▸' else (w // 2 + half // 2 - i)
            fill(x, cy - (half - i), x + 1, cy + (half - i) + 1)
    elif ch == '×':
        s_ = max(3, w // 2); x0 = (w - s_) // 2; y0 = round(h * 0.55 - s_ / 2)
        for i in range(s_): fill(x0 + i, y0 + i, x0 + i + 1, y0 + i + 1); fill(x0 + s_ - 1 - i, y0 + i, x0 + s_ - i, y0 + i + 1)
    elif ch == '⇧':
        # the shift key's outlined arrow: a triangle head over a stem
        cx = (w - 1) / 2; top = round(h * 0.22); base = round(h * 0.52); bottom = round(h * 0.78)
        hw = (w - 1) / 2; sw = max(1, round(w * 0.18))
        sl, sr = round(cx - sw), round(cx + sw)
        for y in range(top, base + 1):
            d = hw * (y - top) / max(1, base - top)
            fill(round(cx - d), y, round(cx - d) + 1, y + 1); fill(round(cx + d), y, round(cx + d) + 1, y + 1)
        fill(round(cx - hw), base, sl + 1, base + 1); fill(sr, base, round(cx + hw) + 1, base + 1)
        fill(sl, base, sl + 1, bottom + 1); fill(sr, base, sr + 1, bottom + 1); fill(sl, bottom, sr + 1, bottom + 1)
    elif ch == '…':
        y = h - h // 4 - 1; d = max(1, w // 8)
        for x in (w // 6, w // 2, w - w // 6 - 1): fill(x, y, x + d, y + d)
    elif ch == '±':
        m = w // 2; top = h // 4
        fill(1, top + h // 5, w - 1, top + h // 5 + 1); fill(m, top, m + 1, top + 2 * h // 5 + 1); fill(1, top + 2 * h // 5 + 3, w - 1, top + 2 * h // 5 + 4)
    else:
        return None
    return from_rows(g, w, h, stride)

def main():
    out = sys.argv[1]
    fonts = [a.split(':', 1) for a in sys.argv[2:]]
    lines = ['/* Generated by tools/psf2c.py — do not edit. Terminus Font, SIL OFL 1.1 */',
             '#pragma once', '#include <stdint.h>', '',
             'struct font { uint8_t width, height, stride; const uint8_t *bits; /* 256 glyphs */ };', '']
    cp = [chr(i) if i >= 0x20 else ' ' for i in range(128)] + [SYMBOLS.get(i, ' ') for i in range(128, 256)]
    for name, path in fonts:
        glyphs, table, w, h = load_psf(path)
        stride = (w + 7) // 8
        missing = []
        blob = bytearray()
        synthesised = []
        for i, ch in enumerate(cp):
            gi = table.get(ch)
            if gi is None:
                made = synth(ch, glyphs, table, w, h, stride)
                if made is not None:
                    blob += made; synthesised.append(ch); continue
            if gi is None and ch in FALLBACK:
                gi = table.get(FALLBACK[ch]); missing.append(ch)
            if gi is None:
                gi = table.get('?', 0)
                if ch != ' ':
                    missing.append(ch)
            blob += glyphs[gi]
        if synthesised:
            print(f'{name}: drawn: {" ".join(synthesised)}', file=sys.stderr)
        if missing:
            print(f'{name}: fallback used for: {" ".join(missing)}', file=sys.stderr)
        lines.append(f'static const uint8_t font_{name}_bits[{len(blob)}] = {{')
        for i in range(0, len(blob), 16):
            lines.append('  ' + ','.join(f'0x{b:02x}' for b in blob[i:i + 16]) + ',')
        lines.append('};')
        lines.append(f'static const struct font font_{name} = {{ {w}, {h}, {stride}, font_{name}_bits }};')
        lines.append('')
    # code page table for runtime UTF-8 lookup: sorted (codepoint, index)
    pairs = sorted((ord(SYMBOLS[i]), i) for i in SYMBOLS)
    lines.append(f'#define CODEPAGE_SYMBOLS {len(pairs)}')
    lines.append('static const struct { uint16_t cp; uint8_t idx; } codepage_symbols[CODEPAGE_SYMBOLS] = {')
    for c, i in pairs:
        lines.append(f'  {{ 0x{c:04x}, 0x{i:02x} }},')
    lines.append('};')
    open(out, 'w').write('\n'.join(lines) + '\n')

main()
