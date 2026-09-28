#!/usr/bin/env python3
"""BARE! on a floppy: a 3.5-inch 1.44 MB diskette (or a 5.25-inch 1.2 MB one) that boots the 32-bit kernel on a BIOS
PC. Limine can't: it reads with the BIOS's extended disk calls, which floppy drives don't answer. GRUB still reads
floppies the old way (cylinder, head, sector), so its boot sector — with the filesystem's BPB kept inside it — and its
core image (disk reads, FAT, multiboot2, VESA modes, gunzip) fill the reserved sectors in front of one FAT12 filesystem
over the whole disk. That holds the kernel, stripped and gzipped (GRUB unpacks it), README.TXT, LICENSE.TXT and
NOTICE.TXT (the notices, the licences after them, and where GRUB's source is).
It plays like the stick but can't save: projects live in the stick's own partition.
A 720 KB disk doesn't boot yet: GRUB takes the drive's geometry (18 sectors a track in a 1.44 MB drive) for the disk's.
Usage: tools/mkfloppy.py GRUB_DIR KERNEL.elf OUT.img [1440|1200]
       GRUB_DIR holds usr/lib/grub/i386-pc and usr/bin/grub-mkimage (make grub unpacks Arch's package into build/grub)"""
import gzip, os, re, struct, subprocess, sys, tempfile

HERE = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/..')
gdir, kernel, out = sys.argv[1], sys.argv[2], sys.argv[3]
kib = int(sys.argv[4]) if len(sys.argv) > 4 else 1440
GRUB_VERSION = '2.14 (Arch Linux grub 2:2.14-1)'
GEOMETRY = {1440: (18, 2, 224, 0xF0), 1200: (15, 2, 224, 0xF9)}       # sectors a track, heads, root entries, media
if kib not in GEOMETRY: sys.exit(f'{kib} KiB: 1440 or 1200 (a 720 KB disk does not boot yet)')
spt, heads, root_entries, media = GEOMETRY[kib]
mods = f'{gdir}/usr/lib/grub/i386-pc'
mkimage = f'{gdir}/usr/bin/grub-mkimage'
app = open(f'{HERE}/core/app.h').read()
release = (re.search(r'#define BARE_RELEASE\s+"([^"]+)"', app).group(1) + re.search(r'#define BARE_STAGE\s+"([^"]*)"', app).group(1)).strip()
elf = open(kernel, 'rb').read()
at = elf.find(b'HBVER001')
build = struct.unpack_from('<I', elf, at + 8)[0] if at >= 0 else 0

def dos(t): return t.replace('\r\n', '\n').replace('\n', '\r\n').encode()

with tempfile.TemporaryDirectory() as tmp:
    subprocess.run(['llvm-strip', '-o', f'{tmp}/k.elf', kernel], check=True)
    bare = gzip.compress(open(f'{tmp}/k.elf', 'rb').read(), 9, mtime=0)
    open(f'{tmp}/grub.cfg', 'w').write('set root=(fd0)\nmultiboot2 /BARE.GZ\nboot\n')
    subprocess.run([mkimage, '-O', 'i386-pc', '-d', mods, '-o', f'{tmp}/core.img', '-c', f'{tmp}/grub.cfg', '-p', '(fd0)/boot/grub',
                    'biosdisk', 'fat', 'multiboot2', 'vbe', 'gzio'], check=True)
    core = open(f'{tmp}/core.img', 'rb').read()
boot = bytearray(open(f'{mods}/boot.img', 'rb').read())

notice = open(f'{HERE}/NOTICE.md').read()
for f in sorted(os.listdir(f'{HERE}/LICENSES')): notice += f'\n\n---- {f} ----\n\n' + open(f'{HERE}/LICENSES/{f}').read()
readme = (f'BARE! {release} (build {build}) - a synthesizer with no operating system, on a floppy.\n\n'
          'Put the diskette in and start the PC from it: the floppy first in the BIOS boot order, or picked in its boot menu.\n'
          'Loading takes a minute from a real drive. It plays as the USB stick does, but cannot save: projects live on\n'
          'the stick, in a partition of its own. Any key ends the splash; then hold 6 and run a finger along Z to /.\n\n'
          f'The boot loader is GRUB {GRUB_VERSION}, GPL-3.0-or-later (LICENSE.TXT). Its source:\n'
          '  https://ftp.gnu.org/gnu/grub/grub-2.14.tar.xz\n'
          '  https://gitlab.archlinux.org/archlinux/packaging/packages/grub (tag 2-2.14-1), how Arch built it\n\n'
          'BARE! is free software, GPL-3.0-or-later: https://github.com/willbearfruits/bare\n')
files = [('BARE.GZ', bare), ('README.TXT', dos(readme)), ('LICENSE.TXT', dos(open(f'{HERE}/LICENSE').read())), ('NOTICE.TXT', dos(notice))]

# the layout: boot sector and GRUB's core in the reserved sectors, two FATs, the root directory, the files in a row
bps, total = 512, kib * 2
reserved = 1 + (len(core) + bps - 1) // bps
root_secs = root_entries * 32 // bps
fat_secs = 1
while True:                                               # FAT12: 1.5 bytes a cluster (one sector each), clusters from 2
    clusters = total - reserved - 2 * fat_secs - root_secs
    if (clusters + 2) * 3 // 2 <= fat_secs * bps: break
    fat_secs += 1
fat0 = reserved * bps
root0 = (reserved + 2 * fat_secs) * bps
data0 = root0 + root_secs * bps
img = bytearray(total * bps)
fat = bytearray(fat_secs * bps)
def fat_set(n, v):
    o = n + n // 2
    if n % 2 == 0: fat[o] = v & 0xFF; fat[o + 1] = (fat[o + 1] & 0xF0) | (v >> 8 & 0x0F)
    else: fat[o] = (fat[o] & 0x0F) | (v << 4 & 0xF0); fat[o + 1] = v >> 4 & 0xFF
fat_set(0, 0xF00 | media); fat_set(1, 0xFFF)
entries = [b'BARE       ' + bytes([0x08]) + bytes(20)]           # the volume label
cluster = 2
for name, data in files:
    n = max(1, (len(data) + bps - 1) // bps)
    if cluster + n > clusters + 2: sys.exit(f'{out}: {name} does not fit ({(clusters + 2 - cluster) * bps} bytes left)')
    for i in range(n): fat_set(cluster + i, 0xFFF if i == n - 1 else cluster + i + 1)
    img[data0 + (cluster - 2) * bps:data0 + (cluster - 2) * bps + len(data)] = data
    base, ext = name.split('.')
    e = bytearray(32); e[0:11] = (base.ljust(8) + ext.ljust(3)).encode(); e[11] = 0x20
    struct.pack_into('<HH', e, 22, 0, (2026 - 1980) << 9 | 9 << 5 | 28)   # a date, so no system shows 1980
    struct.pack_into('<HI', e, 26, cluster, len(data))
    entries.append(bytes(e))
    cluster += n
for i, e in enumerate(entries): img[root0 + i * 32:root0 + i * 32 + 32] = e
for i in range(2): img[fat0 + i * fat_secs * bps:fat0 + (i + 1) * fat_secs * bps] = fat

# GRUB's boot sector, with the BPB (bytes 3 to 0x5A) written into it; its core from sector 1, as its blocklist expects
bpb = struct.pack('<8sHBHBHHBHHHII', b'BARE!   ', bps, 1, reserved, 2, root_entries, total, media, fat_secs, spt, heads, 0, 0)
ebpb = struct.pack('<BBBI11s8s', 0, 0, 0x29, 0x0B1E2026, b'BARE       ', b'FAT12   ')
boot[3:3 + len(bpb)] = bpb
boot[3 + len(bpb):3 + len(bpb) + len(ebpb)] = ebpb
assert 3 + len(bpb) + len(ebpb) <= 0x5A
img[0:bps] = boot
img[bps:bps + len(core)] = core
open(out, 'wb').write(img)
free = (clusters + 2 - cluster) * bps
print(f'{out}: {kib} KiB, build {build}: GRUB {bps + len(core)} bytes, BARE.GZ {len(bare)} bytes, {free >> 10} KiB free')
