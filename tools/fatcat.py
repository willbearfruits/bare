#!/usr/bin/env python3
"""Read a file from the root of a stick image's FAT32 partition (the first partition), without mounting it:
    python3 tools/fatcat.py stick.img HB-LOG.TXT [out]      (no out: to stdout)
    python3 tools/fatcat.py stick.img                       (lists the root)"""
import struct, sys

img = open(sys.argv[1], 'rb').read()
start = struct.unpack('<I', img[446 + 8:446 + 12])[0] * 512
bpb = img[start:start + 512]
bps, spc = struct.unpack('<H', bpb[11:13])[0], bpb[13]
reserved, nfats = struct.unpack('<H', bpb[14:16])[0], bpb[16]
fatsz, root = struct.unpack('<I', bpb[36:40])[0], struct.unpack('<I', bpb[44:48])[0]
fat = start + reserved * bps
data = fat + nfats * fatsz * bps
csize = spc * bps

def chain(c):
    while 2 <= c < 0x0FFFFFF8:
        yield c
        c = struct.unpack('<I', img[fat + c * 4:fat + c * 4 + 4])[0] & 0x0FFFFFFF

def read(c, size=None):
    out = b''.join(img[data + (k - 2) * csize:data + (k - 1) * csize] for k in chain(c))
    return out if size is None else out[:size]

entries = []
d = read(root)
for i in range(0, len(d), 32):
    e = d[i:i + 32]
    if e[0] == 0: break
    if e[0] == 0xE5 or e[11] & 0x0F == 0x0F: continue
    base, ext = e[:8].decode(errors='replace').rstrip(), e[8:11].decode(errors='replace').rstrip()
    name = base + ('.' + ext if ext else '')
    first = struct.unpack('<H', e[20:22])[0] << 16 | struct.unpack('<H', e[26:28])[0]
    entries.append((name, first, struct.unpack('<I', e[28:32])[0], e[11]))

if len(sys.argv) < 3:
    for name, first, size, attr in entries: print(f"{name:12s} {size:10d}{'  <dir>' if attr & 0x10 else ''}")
    sys.exit()
for name, first, size, attr in entries:
    if name.upper() == sys.argv[2].upper():
        blob = read(first, size)
        if len(sys.argv) > 3: open(sys.argv[3], 'wb').write(blob)
        else: sys.stdout.buffer.write(blob)
        sys.exit()
sys.exit(f'{sys.argv[2]}: not in the root')
