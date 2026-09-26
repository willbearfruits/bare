#!/usr/bin/env python3
"""Build the bootable disk image: MBR + FAT32 boot partition (Limine, kernel slots A/B) + HBTAPE project partition.
Usage: mkimage.py kernel.elf out.img [--kernel64 k64.elf] [--upd BARE.UPD] [--doom file.wad]
(needs limine; the build number is read from the ELF). kernel64 is what UEFI boots on 64-bit CPUs (BOOT/H64_A.ELF, with
BOOT/H64_B.ELF beside it for updates, as HB_A/HB_B are for the 32-bit kernel)."""
import os, struct, subprocess, sys

args = sys.argv[1:]
def option(name):
    if name not in args: return None
    i = args.index(name); v = args[i + 1]; del args[i:i + 2]; return v
kernel64 = option('--kernel64')
with_upd = option('--upd')                                # test aid: bake an update file into the root
doom_wad = option('--doom')                               # test aid: a WAD in the DOOM folder (never in a release)
kernel, out = args[0], args[1]
LIMINE = '/usr/share/limine'
FAT_MB, TAPE_MB, SLOT_BYTES = 64, 128, 4 * 1024 * 1024
SEC = 512
p1_start = 2048                       # 1 MiB gap for Limine's stage 2
p1_secs = FAT_MB * 2048
p2_start = p1_start + p1_secs
p2_secs = TAPE_MB * 2048
total = p2_start + p2_secs

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fat32 import Fat32
fs = Fat32(p1_secs * SEC)
kdata = open(kernel, 'rb').read()
assert len(kdata) <= SLOT_BYTES, 'kernel bigger than a slot'
tag = kdata.find(b'HBVER001'); assert tag >= 0, 'no build tag in the kernel'
version = struct.unpack('<I', kdata[tag + 8:tag + 12])[0]
slot = kdata + b'\0' * (SLOT_BYTES - len(kdata))
conf = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'boot', 'i386', 'limine.img.conf')).read()
if not kernel64:                                  # no 64-bit kernel: UEFI boots the 32-bit one too (a test of its USB stack)
    head, _, rest = conf.partition('/BARE (UEFI)')
    conf = head + rest[rest.index('/BARE'):]
confb = (conf.encode() + b'\n' * 2048)[:2048]     # fixed size (bytes) so the updater can rewrite it in place
boot = fs.mkdir(fs.root, 'boot'); lim = fs.mkdir(boot, 'limine'); efi = fs.mkdir(fs.root, 'EFI'); efib = fs.mkdir(efi, 'BOOT')
fs.add(lim, 'limine.conf', confb)
fs.add(lim, 'limine-bios.sys', open(f'{LIMINE}/limine-bios.sys', 'rb').read())
fs.add(efib, 'BOOTX64.EFI', open(f'{LIMINE}/BOOTX64.EFI', 'rb').read())
fs.add(efib, 'BOOTIA32.EFI', open(f'{LIMINE}/BOOTIA32.EFI', 'rb').read())
fs.add(boot, 'HB_A.ELF', slot)
fs.add(boot, 'HB_B.ELF', slot)
if kernel64:
    k64 = open(kernel64, 'rb').read()
    assert len(k64) <= SLOT_BYTES, '64-bit kernel bigger than a slot'
    fs.add(boot, 'H64_A.ELF', k64 + b'\0' * (SLOT_BYTES - len(k64)))
    fs.add(boot, 'H64_B.ELF', k64 + b'\0' * (SLOT_BYTES - len(k64)))
fs.add(fs.root, 'README.TXT', f'BARE! build {version}\r\nBoot this stick, in BIOS/legacy or UEFI mode.\r\nTo update: put BARE.UPD in the root of any FAT drive and boot.\r\n'.encode())
# the licences travel with the binaries: BARE!'s (GPL-3.0-or-later), and the notices for what it carries (Limine, Terminus)
here = os.path.dirname(os.path.abspath(__file__)) + '/..'
def dos(text): return text.replace('\r\n', '\n').replace('\n', '\r\n').encode()
fs.add(fs.root, 'LICENSE.TXT', dos(open(f'{here}/LICENSE').read()))
notice = open(f'{here}/NOTICE.md').read()
for f in sorted(os.listdir(f'{here}/LICENSES')): notice += f'\n\n---- {f} ----\n\n' + open(f'{here}/LICENSES/{f}').read()
fs.add(fs.root, 'NOTICE.TXT', dos(notice))
if with_upd: fs.add(fs.root, 'BARE.UPD', open(with_upd, 'rb').read())
# the example instruments, in the folder BARE! reads them from (files added there on a computer join them)
instr = fs.mkdir(fs.root, 'INSTR')
for f in sorted(os.listdir(f'{here}/instruments')):
    if f.endswith('.txt'): fs.add(instr, os.path.splitext(f)[0].upper()[:8] + '.TXT', dos(open(f'{here}/instruments/{f}').read()))
dirs = [fs.root, boot, lim, efi, efib, instr]
if doom_wad:
    doom = fs.mkdir(fs.root, 'DOOM'); dirs.append(doom)
    fs.add(doom, os.path.basename(doom_wad), open(doom_wad, 'rb').read())
fat_bytes = fs.build(dirs)

def chs(lba):
    return bytes([0xFE, 0xFF, 0xFF])          # LBA-only, CHS maxed out
def entry(boot, typ, start, secs):
    return bytes([0x80 if boot else 0]) + chs(start) + bytes([typ]) + chs(start + secs - 1) + struct.pack('<II', start, secs)

with open(out, 'wb') as o:
    o.truncate(total * SEC)
    mbr = bytearray(SEC)
    mbr[446:462] = entry(True, 0x0C, p1_start, p1_secs)
    mbr[462:478] = entry(False, 0x7F, p2_start, p2_secs)
    mbr[510:512] = b'\x55\xAA'
    o.seek(0); o.write(mbr)
    o.seek(p1_start * SEC); o.write(fat_bytes)
    hdr = bytearray(SEC)
    slot_secs = 2048
    hdr[0:8] = b'HBTAPE01'
    hdr[8:24] = struct.pack('<IIII', slot_secs, (p2_secs - 1) // slot_secs, 0, p2_secs)
    o.seek(p2_start * SEC); o.write(hdr)
subprocess.run(['limine', 'bios-install', out], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print(f'{out}: {total * SEC // (1024*1024)} MiB, FAT {FAT_MB} MiB + tape {TAPE_MB} MiB, build {version}')
