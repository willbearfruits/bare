#!/usr/bin/env python3
"""BARE! stick tool.
  hb.py mkupd kernel.elf out.upd [k64.elf]   build an update file (BARE.UPD) with the 32-bit kernel and, when given,
                                            the 64-bit one (what UEFI boots); the build number is read from the ELF
  hb.py flash image.img /dev/sdX [--force]   write the image to a stick, keeping an existing project partition; refuses a
                                            mounted or non-removable disk (--force: the latter anyway), reads back
"""
import fcntl, os, struct, subprocess, sys, zlib

def elf_version(data):
    """The build number the kernel will report: core/app.c stores it behind an 8-byte magic."""
    i = data.find(b'HBVER001')
    assert i >= 0, 'no HBVER001 build tag in the kernel'
    return struct.unpack('<I', data[i + 8:i + 12])[0]

def mkupd(kernel, out, kernel64=None):
    """HBUPD001: magic, build, size, crc, the 32-bit kernel. HBUPD002 adds the 64-bit kernel's build, size and crc to
    the header and the kernel after the first; each kernel compares its own build number, so neither can loop."""
    data = open(kernel, 'rb').read()
    version = elf_version(data)
    if kernel64:
        d64 = open(kernel64, 'rb').read()
        v64 = elf_version(d64)
        hdr = b'HBUPD002' + struct.pack('<6I', version, len(data), zlib.crc32(data) & 0xFFFFFFFF, v64, len(d64), zlib.crc32(d64) & 0xFFFFFFFF)
        open(out, 'wb').write(hdr + data + d64)
        print(f'{out}: build {version} ({len(data)} bytes) and 64-bit build {v64} ({len(d64)} bytes)')
    else:
        hdr = b'HBUPD001' + struct.pack('<III', version, len(data), zlib.crc32(data) & 0xFFFFFFFF)
        open(out, 'wb').write(hdr + data)
        print(f'{out}: build {version}, {len(data)} bytes')

def parts(mbr):
    out = []
    for i in range(4):
        e = mbr[446 + i * 16: 462 + i * 16]
        typ, start, secs = e[4], struct.unpack('<I', e[8:12])[0], struct.unpack('<I', e[12:16])[0]
        out.append((typ, start, secs))
    return out

BLKFLSBUF = 0x1261                                      # ioctl: drop a block device's cached pages

def flash(image, dev, force=False):
    """Write the image to a stick, keeping its project partition. Refuses a disk that is mounted or not removable (an
    internal drive) unless forced, and reads back what it wrote, past the cache."""
    name = os.path.basename(os.path.realpath(dev))
    info = subprocess.run(['lsblk', '-dn', '-o', 'MODEL,SERIAL,TRAN,SIZE', dev], capture_output=True, text=True).stdout.split()
    try: removable = open(f'/sys/block/{name}/removable').read().strip() == '1'
    except OSError: removable = False
    if not removable and not force:
        sys.exit(f'{dev} ({" ".join(info)}) is not a removable disk: not written (--force writes it anyway)')
    mounted = [l.split()[0] for l in open('/proc/mounts') if os.path.realpath(l.split()[0]).startswith('/dev/' + name)]
    if mounted: sys.exit(f'{dev} is mounted ({", ".join(mounted)}): unmount it first')
    print(f'{dev}: {" ".join(info)}')
    img = open(image, 'rb').read()
    new = parts(img[:512])
    keep = None
    try:
        with open(dev, 'rb') as d:
            old_mbr = d.read(512)
            for typ, start, secs in parts(old_mbr):
                if typ == 0x7F:
                    d.seek(start * 512)
                    if d.read(8) == b'HBTAPE01': keep = (start, secs)
    except OSError: pass
    if keep and keep[0] == new[1][1]:
        print(f'keeping existing project partition ({keep[1] // 2048} MiB)')
        mbr = bytearray(img[:512]); mbr[462:478] = old_mbr[462:478]
        data = bytes(mbr) + img[512:new[1][1] * 512]          # everything up to the tape partition
    else:
        data = img
    with open(dev, 'r+b') as d:
        d.seek(0); d.write(data)
        d.flush(); os.fsync(d.fileno())
    with open(dev, 'rb') as d:
        try: fcntl.ioctl(d.fileno(), BLKFLSBUF)
        except OSError: pass                                  # a plain file, not a device
        if d.read(len(data)) != data: sys.exit(f'{dev}: what was read back differs from what was written')
    print(f'flashed {image} to {dev}, {len(data) >> 20} MiB read back and verified')

cmd = sys.argv[1] if len(sys.argv) > 1 else ''
if cmd == 'mkupd': mkupd(*sys.argv[2:5])
elif cmd == 'flash': flash(sys.argv[2], sys.argv[3], '--force' in sys.argv[4:])
else: print(__doc__)
