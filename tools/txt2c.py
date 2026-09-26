#!/usr/bin/env python3
"""The example instruments (instruments/*.txt) as C strings: core/inst_builtin.c. Usage: txt2c.py out.c in.txt ..."""
import os, sys

out, files = sys.argv[1], sorted(sys.argv[2:])
c = '/* Generated from the files in instruments/ by tools/txt2c.py: the instruments built in (a stick\'s INSTR folder adds to them). */\n'
c += '#include "inst.h"\n\nconst struct inst_text inst_builtin[] = {\n'
for f in files:
    name = os.path.splitext(os.path.basename(f))[0].upper()[:8] + '.TXT'
    text = open(f).read()
    lit = ''.join('\\n"\n    "' if ch == '\n' else '\\"' if ch == '"' else '\\\\' if ch == '\\' else ch for ch in text)
    c += f'  {{ "{name}",\n    "{lit}" }},\n'
c += '};\nconst int inst_builtin_count = (int)(sizeof inst_builtin / sizeof inst_builtin[0]);\n'
open(out, 'w').write(c.replace('\n    "" }', ' }'))
