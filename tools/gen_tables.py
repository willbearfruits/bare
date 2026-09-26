#!/usr/bin/env python3
"""Regenerates core/tables.c and core/tables.h (fixed-point lookup tables). Usage: gen_tables.py [core/]"""
import math, os, sys

def rows(vals, per=16):
    return ''.join('  ' + ', '.join(str(v) for v in vals[i:i + per]) + ',\n' for i in range(0, len(vals), per))

RATE = 48000
svf = [min(29000, int(2 * math.sin(math.pi * 30 * 2 ** (i / 14) / RATE) * 32768)) for i in range(128)]
sine256 = [round(32767 * math.sin(2 * math.pi * i / 256)) for i in range(256)]
sine8192 = [round(32767 * math.sin(2 * math.pi * i / 8192)) for i in range(8192)]

# GENDY's step distributions (Xenakis's list, all made symmetric about 0): inverse CDFs at 256 even steps of u, scaled
# and clipped to +-1 (Q15). LINEAR even; CAUCHY mostly tiny, now and then huge; LOGIST and HYPCOS (density 1/cosh x) bells
# with long tails; ARCSIN mostly near the ends; EXPON (two-sided) a sharp peak at 0.
def gendy_dist(kind, u):
    if kind == 0: x = 2 * u - 1
    elif kind == 1: x = math.tan(math.pi * (u - 0.5)) / 8
    elif kind == 2: x = math.log(u / (1 - u)) / 5.5
    elif kind == 3: x = (2 / math.pi) * math.log(math.tan(math.pi * u / 2)) / 3.5
    elif kind == 4: x = math.sin(math.pi * (u - 0.5))
    else: w = abs(2 * u - 1); x = math.copysign(-math.log(1 - w * 0.9985) / 5, u - 0.5)
    return max(-32767, min(32767, round(x * 32767)))
gendy = [gendy_dist(k, (i + 0.5) / 256) for k in range(6) for i in range(256)]
neglog = [round(-math.log((i + 0.5) / 256) * 256) for i in range(256)]

tables = [
    ('uint16_t', 'svf_f_q15', 128, svf, 'filter cutoff index 0..127 -> f coefficient (Q15), fc = 30*2^(i/14) Hz at 48 kHz'),
    ('uint16_t', 'neglog_q8', 256, neglog, '-ln u at 256 even steps of u (Q8): exponential waiting times, as a Poisson stream has'),
    ('int16_t', 'gendy_dist_q15', 6 * 256, gendy, "GENDY's six step distributions, 256 values each (Q15, +-1): LINEAR CAUCHY LOGIST HYPCOS ARCSIN EXPON"),
    ('int16_t', 'sine_q15', 256, sine256, 'full-cycle sine, 256 entries, Q15'),
    ('int16_t', 'sine_q15_8192', 8192, sine8192, 'full-cycle sine, 8192 entries, Q15 — oscillators, FFT twiddles, windows'),
]

out_dir = sys.argv[1] if len(sys.argv) > 1 else 'core'
if out_dir.endswith('.h'): out_dir = os.path.dirname(out_dir) or '.'
h = '/* Generated fixed-point lookup tables (tools/gen_tables.py) — regenerate, do not edit. */\n#pragma once\n#include <stdint.h>\n\n'
c = '/* Generated fixed-point lookup tables (tools/gen_tables.py) — regenerate, do not edit. */\n#include "tables.h"\n\n'
for ctype, name, n, vals, doc in tables:
    h += f'/* {doc} */\nextern const {ctype} {name}[{n}];\n'
    c += f'const {ctype} {name}[{n}] = {{\n' + rows(vals) + '};\n'
open(os.path.join(out_dir, 'tables.h'), 'w').write(h)
open(os.path.join(out_dir, 'tables.c'), 'w').write(c)
