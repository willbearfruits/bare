#!/usr/bin/env python3
"""Boot the instrument on many emulated PCs and check each one: it boots, draws, sounds, and — where the firmware can —
saves a project and brings it back after a reset. Writes a PNG per machine and a summary to build/matrix/.
PASS: works. LIMIT: falls short the way it is expected to (below the minimum CPU, no EHCI or virtio-gpu driver).
EMU: an emulator bug (traced, see the note) breaks a feature and the instrument reports it. FAIL: anything else.
Usage: tools/qemu-matrix.py [-j N] [--only TEXT[,TEXT...]] [--list]        (run `make`, `make img`, `make ARCH=x86_64` first)"""
import argparse, concurrent.futures as cf, os, shutil, socket, struct, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', 'matrix')
OVMF_CODE, OVMF_VARS = '/usr/share/edk2/x64/OVMF_CODE.4m.fd', '/usr/share/edk2/x64/OVMF_VARS.4m.fd'
KVM = os.access('/dev/kvm', os.W_OK)
UHCI_NOTE = "QEMU's UHCI model drops a packet on long BIOS reads (usb_uhci_packet_cancel done=1), so Limine fails some boots"
OHCI_NOTE = "QEMU's OHCI model dies (usb_ohci_die) during SeaBIOS bulk writes: boots and plays, save reports failure"
SHEAR_NOTE = "QEMU's VGA rounds 1366 down to 1360 but OVMF reports 1366, so the display shears; the framebuffer itself is right"

def M(name, **kw):
    """One machine. build i386/x86_64, media iso/img, machine, cpu (None = the host's, with KVM), fw bios/uefi, mem,
    vga, res WxH (EDID), audio hda/hda-output/hda-micro/ich9/two-hda/ac97/sb16/none, disk (img: usb-xhci usb-ehci
    usb-uhci usb-ohci usb-hub ide ahci nvme virtio lsi megasas virtio-scsi sd), kbd ps2/usb/usb-only (no PS/2 at all, a USB
    keyboard and tablet), smp, screen gfx/text/none,
    expect ok/no-boot/no-keys/no-save/no-storage/no-picture, boot_tries (resets allowed per boot), log (lines the serial
    log must have), image ('i386-only': a stick without the 64-bit kernel), extra (more QEMU arguments), pre (keys before
    the save; 'wait:MS' pauses)."""
    c = dict(name=name, build='i386', media='iso', machine='q35', cpu=None, fw='bios', mem='512M', vga=None, res=None,
             audio='hda', disk='usb-xhci', kbd='ps2', smp=1, screen='gfx', expect='ok', boot_tries=1, log=[], image='', extra=[], pre=[],
             note='')
    c.update(kw)
    return c

CONFIGS = [
    # firmware and boot media
    M('bios-iso'),
    M('bios-usb-stick', media='img'),
    M('bios-usb-takeover', media='img', pre=['f7', 'tab', 'tab', 'u', 'wait:4000', 'tab', 'tab', 'tab'], log=['xhci: ', 'usb: QEMU USB HARDDRIVE: drive 0'],
      note='U in the MIDI view: our USB stack takes over from the BIOS and saves; after the reset the BIOS loads'),
    M('uefi-iso', fw='uefi'),
    M('uefi-usb-stick', media='img', fw='uefi', note='no BIOS under UEFI: the stick through our own xHCI driver'),
    M('uefi-usb-hub', media='img', fw='uefi', disk='usb-hub', log=['usb: hub', 'drive 1'], note='a keyboard and a second stick behind a USB hub'),
    M('uefi-i386-kernel-usb', media='img', fw='uefi', image='i386-only', extra=['-fw_cfg', 'name=opt/ovmf/X-PciMmio64Mb,string=0'],
      note='the 32-bit kernel under UEFI (a stick without the 64-bit one), the xHCI below 4 GiB'),
    M('uefi-usb2-ehci-stick', media='img', fw='uefi', disk='usb-ehci', expect='no-storage', note='no EHCI driver yet: runs without storage'),
    M('uefi-i440fx', fw='uefi', machine='pc'),
    M('uefi-x86_64', build='x86_64', fw='uefi'),
    M('uefi-no-8254', fw='uefi', machine='q35,pit=off', note='8254 off, as on many UEFI-only laptops: the HPET drives the tick'),
    M('uefi-no-timers', fw='uefi', machine='q35,pit=off,hpet=off', note='no timer interrupts at all: TSC time, the main loop pumps'),
    M('bios-x86_64', build='x86_64'),
    # chipsets and where the stick sits
    M('i440fx-iso', machine='pc'),
    M('i440fx-ide-disk', machine='pc', media='img', disk='ide'),
    M('i440fx-usb1-uhci', machine='pc', media='img', disk='usb-uhci', boot_tries=6, note=UHCI_NOTE),
    M('i440fx-usb1-ohci', machine='pc', media='img', disk='usb-ohci', expect='no-save', note=OHCI_NOTE),
    M('i440fx-usb2-ehci', machine='pc', media='img', disk='usb-ehci'),
    M('q35-sata-ahci', media='img', disk='ahci'),
    M('q35-nvme', media='img', disk='nvme'),
    M('net-e1000', extra=['-netdev', 'user,id=n0', '-device', 'e1000,netdev=n0'], log=['from DHCP'], note='Ethernet: an address from DHCP'),
    M('uefi-net-e1000e', fw='uefi', extra=['-netdev', 'user,id=n0', '-device', 'e1000e,netdev=n0'], log=['from DHCP'], note='the 64-bit kernel on an 82574'),
    M('uefi-usb-ethernet', media='img', fw='uefi', extra=['-netdev', 'user,id=n0', '-device', 'usb-net,netdev=n0,bus=hc.0'], log=['(ECM): Ethernet', 'from DHCP'],
      note='a USB Ethernet adapter (CDC-ECM, its second configuration)'),
    M('uefi-sata-ahci', media='img', disk='ahci', fw='uefi', log=['disk: SATA 0: '], note='the stick installed on a SATA disk: our AHCI driver'),
    M('uefi-nvme', media='img', disk='nvme', fw='uefi', log=['disk: NVMe '], note='the stick installed on an NVMe drive: our NVMe driver'),
    M('uefi-i386-kernel-ahci', media='img', disk='ahci', fw='uefi', image='i386-only', log=['disk: SATA 0: '],
      note='the 32-bit kernel under UEFI on a SATA disk'),
    M('uefi-i386-kernel-nvme', media='img', disk='nvme', fw='uefi', image='i386-only', extra=['-fw_cfg', 'name=opt/ovmf/X-PciMmio64Mb,string=0'],
      log=['disk: NVMe '], note='the 32-bit kernel under UEFI on an NVMe drive (its registers below 4 GiB)'),
    M('q35-virtio-blk', media='img', disk='virtio'),
    M('scsi-lsi53c895a', machine='pc', media='img', disk='lsi'),
    M('scsi-megaraid-sas', media='img', disk='megasas'),
    M('scsi-virtio', media='img', disk='virtio-scsi'),
    M('sd-card', media='img', disk='sd'),
    M('isa-only-pc', machine='isapc', cpu='pentium3', audio='none', mem='128M', screen='text',
      note='no PCI, no VESA framebuffer: text notice, plays without a picture'),
    # processors, oldest to newest (no KVM for named models: emulated)
    M('486', cpu='486', expect='no-boot', note='below the Pentium Pro minimum'),
    M('pentium-p5', cpu='pentium', expect='no-boot', note='below the Pentium Pro minimum'),
    M('pentium2', cpu='pentium2'),
    M('pentium3', cpu='pentium3', machine='pc'),
    M('qemu32', cpu='qemu32', note='generic 32-bit CPU'),
    M('atom-n270', cpu='n270', note='32-bit only netbook CPU'),
    M('core-duo', cpu='coreduo', note='32-bit only laptop CPU'),
    M('core2duo', cpu='core2duo'),
    M('nehalem', cpu='Nehalem'),
    M('sandybridge', cpu='SandyBridge'),
    M('haswell', cpu='Haswell-noTSX'),
    M('athlon', cpu='athlon', machine='pc'),
    M('opteron-g1', cpu='Opteron_G1'),
    M('phenom', cpu='phenom'),
    M('opteron-g5', cpu='Opteron_G5'),
    M('broadwell-like-x250', cpu='Broadwell-noTSX', mem='4G', note='the X250 has a Broadwell i5'),
    M('skylake', cpu='Skylake-Client'),
    M('icelake-server', cpu='Icelake-Server'),
    M('epyc', cpu='EPYC', smp=4),
    # sound
    M('no-sound-card', audio='none', note='PC speaker only'),
    M('ac97-only', audio='ac97', log=['audio: AC97']),
    M('ac97-pentium3', audio='ac97', machine='pc', cpu='pentium3', mem='128M', log=['audio: AC97'], note='a 2000 PC: Pentium III, AC97'),
    M('uefi-ac97', audio='ac97', fw='uefi', log=['audio: AC97'], note='the 64-bit kernel with AC97'),
    M('sb16-only', audio='sb16', machine='pc', log=['audio: Sound Blaster 16']),
    M('sb16-pentium2', audio='sb16', machine='pc', cpu='pentium2', mem='64M', log=['audio: Sound Blaster 16'], note='a 1998 PC: Pentium II, 64 MB, SB16'),
    M('hda-output-codec', audio='hda-output'),
    M('hda-micro-codec', audio='hda-micro'),
    M('ich9-hda', audio='ich9'),
    M('two-hda-controllers', audio='two-hda'),
    # graphics and screens
    M('cirrus', vga='cirrus-vga', machine='pc'),
    M('vmware-svga', vga='vmware-svga'),
    M('qxl', vga='qxl-vga'),
    M('virtio-vga', vga='virtio-vga'),
    M('ati-rage128', vga='ati-vga'),
    M('bochs-display-uefi', vga='bochs-display', fw='uefi'),
    M('bochs-display-bios', vga='bochs-display', note='no legacy VGA: VBE from its own option ROM'),
    M('ramfb', vga='ramfb', note='a framebuffer in plain RAM, no graphics card'),
    M('virtio-gpu-uefi', vga='virtio-gpu-pci', fw='uefi', screen='none', expect='no-picture',
      note="its UEFI driver only blits, no framebuffer: plays without a picture"),
    M('no-graphics-card', vga='none', screen='none', note='nothing to draw on: plays without a picture'),
    M('no-graphics-uefi', vga='none', screen='none', fw='uefi', note='nothing to draw on: plays without a picture'),
    M('640x480', res='640x480', note='below the 800x600 the pages are laid out for'),
    M('800x600', res='800x600'),
    M('1024x768', res='1024x768'),
    M('1366x768-x250-panel', res='1366x768', note='SeaBIOS offers Limine no 1366x768 VESA mode: 1024x768'),
    M('uefi-1366x768', res='1366x768', fw='uefi', note=SHEAR_NOTE),
    M('1280x1024', res='1280x1024'),
    M('1920x1080', res='1920x1080'),
    M('2560x1440', res='2560x1440'),
    M('3840x2160', res='3840x2160', note='SeaBIOS offers Limine no 4K VESA mode: 1024x768'),
    M('uefi-3840x2160', res='3840x2160', fw='uefi', note='OVMF offers Limine no 4K mode: 1280x800'),
    # memory
    M('32MB', mem='32M'),
    M('64MB', mem='64M'),
    M('8GB', mem='8G', smp=8),
    # input
    M('usb-keyboard-only', kbd='usb', expect='no-keys', note="BIOS boot: the BIOS's USB stays in charge, and SeaBIOS doesn't pass USB keys on as PS/2"),
    M('uefi-usb-input-only', fw='uefi', kbd='usb-only', note='no PS/2 controller: a USB keyboard and tablet through our own driver'),
    # a machine like the X250: Broadwell, UEFI, 1366x768 panel, ICH HDA, 8 GB
    M('x250-like-uefi', cpu='Broadwell-noTSX', fw='uefi', res='1366x768', audio='ich9', mem='8G', smp=2, note=SHEAR_NOTE),
]

def saves(c):
    """machines where a project is saved, the machine reset and the project expected back: BIOS boots from the stick
    image, and UEFI ones where our own USB, SATA or NVMe driver reaches it"""
    return c['media'] == 'img' and (c['fw'] == 'bios' or c['disk'] in ('usb-xhci', 'usb-hub', 'ahci', 'nvme'))

def qemu_cmd(c, d):
    img = os.path.join(d, 'stick.img')
    cmd = ['qemu-system-x86_64', '-M', c['machine'] + ',pcspk-audiodev=snd0' + (',i8042=off,vmport=off' if c['kbd'] == 'usb-only' else ''),
           '-m', c['mem'], '-smp', str(c['smp']), '-display', 'none', '-serial', 'file:' + os.path.join(d, 'serial.log'),
           '-monitor', 'unix:' + os.path.join(d, 'mon.sock') + ',server,nowait', '-audiodev', 'wav,id=snd0,path=' + os.path.join(d, 'out.wav')]
    cmd += ['-accel', 'kvm', '-cpu', 'host'] if c['cpu'] is None and KVM else ['-accel', 'tcg'] + (['-cpu', c['cpu']] if c['cpu'] else [])
    if c['fw'] == 'uefi':
        shutil.copy(OVMF_VARS, os.path.join(d, 'vars.fd'))
        cmd += ['-drive', 'if=pflash,format=raw,readonly=on,file=' + OVMF_CODE, '-drive', 'if=pflash,format=raw,file=' + os.path.join(d, 'vars.fd')]
    if c['res']:
        x, y = c['res'].split('x')
        mb = 16
        while mb * 2**20 < int(x) * int(y) * 4: mb *= 2
        cmd += ['-vga', 'none', '-device', f'VGA,edid=on,xres={x},yres={y},vgamem_mb={mb}']
    elif c['vga'] == 'none':
        cmd += ['-vga', 'none']
    elif c['vga']:
        cmd += ['-vga', 'none', '-device', c['vga']]
    a = c['audio']
    if a in ('hda', 'hda-output', 'hda-micro'): cmd += ['-device', 'intel-hda', '-device', {'hda': 'hda-duplex'}.get(a, a) + ',audiodev=snd0']
    elif a == 'ich9': cmd += ['-device', 'ich9-intel-hda', '-device', 'hda-duplex,audiodev=snd0']
    elif a == 'two-hda': cmd += ['-device', 'intel-hda,id=h1', '-device', 'hda-output,audiodev=snd0,bus=h1.0', '-device', 'intel-hda,id=h2', '-device', 'hda-duplex,audiodev=snd0,bus=h2.0']
    elif a == 'ac97': cmd += ['-device', 'AC97,audiodev=snd0']
    elif a == 'sb16': cmd += ['-device', 'sb16,audiodev=snd0']
    if c['kbd'] == 'usb': cmd += ['-device', 'qemu-xhci,id=kbdhc', '-device', 'usb-kbd,bus=kbdhc.0']
    if c['kbd'] == 'usb-only': cmd += ['-device', 'qemu-xhci,id=kbdhc', '-device', 'usb-kbd,bus=kbdhc.0', '-device', 'usb-tablet,bus=kbdhc.0']
    if c['media'] == 'iso':
        cmd += ['-cdrom', os.path.join(ROOT, 'build', c['build'], 'bare.iso'), '-boot', 'd']
    else:
        if c['image'] == 'i386-only':
            subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'mkimage.py'), os.path.join(ROOT, 'build', 'i386', 'bare.elf'), img],
                           check=True, capture_output=True)
        else: shutil.copy(os.path.join(ROOT, 'build', 'i386', 'bare.img'), img)
        drive = f'if=none,id=stick,format=raw,file={img}'
        k = c['disk']
        if k == 'ide': cmd += ['-drive', f'if=ide,format=raw,file={img}']
        elif k == 'ahci': cmd += ['-drive', drive, '-device', 'ide-hd,drive=stick,bus=ide.0']
        elif k == 'nvme': cmd += ['-drive', drive, '-device', 'nvme,drive=stick,serial=hb']
        elif k == 'virtio': cmd += ['-drive', drive, '-device', 'virtio-blk-pci,drive=stick']
        elif k in ('lsi', 'megasas', 'virtio-scsi'):
            hba = {'lsi': 'lsi53c895a', 'megasas': 'megasas', 'virtio-scsi': 'virtio-scsi-pci'}[k]
            cmd += ['-device', hba + ',id=scsi', '-drive', drive, '-device', 'scsi-hd,drive=stick,bus=scsi.0']
        elif k == 'sd':
            with open(img, 'r+b') as f: f.truncate(256 * 2**20)          # QEMU wants a power-of-two card
            cmd += ['-device', 'sdhci-pci', '-drive', drive, '-device', 'sd-card,drive=stick']
        elif k == 'usb-hub':                           # OVMF can't boot from behind QEMU's hub: the stick on a root port,
            spare = os.path.join(d, 'spare.img')        # a keyboard and a blank second stick behind the hub
            with open(spare, 'wb') as f: f.truncate(16 * 2**20)
            cmd += ['-device', 'qemu-xhci,id=hc', '-drive', drive, '-device', 'usb-storage,drive=stick,bus=hc.0,port=1',
                    '-device', 'usb-hub,bus=hc.0,port=2', '-device', 'usb-kbd,bus=hc.0,port=2.1',
                    '-drive', f'if=none,id=spare,format=raw,file={spare}', '-device', 'usb-storage,drive=spare,bus=hc.0,port=2.2']
        else:
            hc = {'usb-xhci': 'qemu-xhci', 'usb-ehci': 'usb-ehci', 'usb-uhci': 'piix3-usb-uhci', 'usb-ohci': 'pci-ohci'}[k]
            cmd += ['-device', hc + ',id=hc', '-drive', drive, '-device', 'usb-storage,drive=stick,bus=hc.0']
        cmd += ['-boot', 'menu=off']
    return cmd + c['extra']

class Monitor:
    def __init__(self, path):
        for _ in range(100):
            if os.path.exists(path): break
            time.sleep(0.05)
        self.s = socket.socket(socket.AF_UNIX); self.s.connect(path); self.s.settimeout(2)
        self('')
    def __call__(self, c):
        self.s.sendall((c + '\n').encode()); time.sleep(0.15)
        try: return self.s.recv(65536).decode(errors='replace')
        except socket.timeout: return ''

def log(d):
    try: return open(os.path.join(d, 'serial.log'), errors='replace').read()
    except OSError: return ''

def seen(d, text):
    """log lines that are `text` or start with it (so 'running' is not 'hda: stream running')"""
    return sum(l == text or l.startswith(text + ' ') for l in log(d).splitlines())

def wait_for(d, text, count, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if seen(d, text) >= count: return True
        if 'EXCEPTION' in log(d): return False
        time.sleep(0.25)
    return False

def skip_splash(mon, d, count):
    """the boot's splash: Esc ends it at once (its sound would count as the machine's, and it takes the first key)"""
    if seen(d, 'splash:') < count: return
    mon('sendkey esc')
    end = time.time() + 5
    while time.time() < end and seen(d, 'splash: skipped') + seen(d, 'splash: done') < count: time.sleep(0.05)

def boot(mon, d, count, timeout, tries):
    """wait for the count-th 'running'; a boot that stalls in the loader gets reset, up to `tries` times.
    Returns the number of tries it took, 0 if it never came up."""
    for t in range(1, tries + 1):
        if wait_for(d, 'running', count, timeout): skip_splash(mon, d, count); return t
        if 'EXCEPTION' in log(d): return 0
        if t < tries: mon('system_reset')
    return 0

def ppm(path):
    data = open(path, 'rb').read()
    parts, pos = [], 0
    while len(parts) < 4:                                   # P6, width, height, maxval
        while data[pos:pos + 1].isspace(): pos += 1
        start = pos
        while not data[pos:pos + 1].isspace(): pos += 1
        parts.append(data[start:pos])
    return int(parts[1]), int(parts[2]), data[pos + 1:]

def screen_ok(path, kind):
    """gfx: the title bar's active tab is amber (#ffb454), in the top rows. text: the notice's yellow title line."""
    try: w, h, px = ppm(path)
    except Exception: return False, 0, 0
    rows = range(min(h, 26)) if kind == 'gfx' else range(28, min(h, 52))
    hits = 0
    for y in rows:
        row = px[y * w * 3:(y + 1) * w * 3]
        for x in range(0, w * 3, 3):
            r, g, b = row[x], row[x + 1], row[x + 2]
            if kind == 'gfx' and r > 225 and 150 < g < 200 and b < 120: hits += 1
            if kind == 'text' and r > 225 and g > 225 and b < 120: hits += 1
    return hits > (150 if kind == 'gfx' else 40), w, h

def loudness(path):
    """largest RMS over 50 ms windows of QEMU's wav (44.1 kHz stereo, header sizes left at zero)"""
    try: data = open(path, 'rb').read()[44:]
    except OSError: return 0
    n = len(data) // 4
    samples = struct.unpack('<%dh' % (n * 2), data[:n * 4])
    best, win = 0, 2205 * 2
    for i in range(0, len(samples) - win, win):
        s = samples[i:i + win]
        best = max(best, (sum(v * v for v in s) / len(s)) ** 0.5)
    return best

def run(c):
    d = os.path.join(OUT, c['name'])
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    slow = c['cpu'] is not None or not KVM
    wait = 150 if slow else 40
    p = subprocess.Popen(qemu_cmd(c, d), stdout=subprocess.DEVNULL, stderr=open(os.path.join(d, 'qemu.err'), 'w'))
    r = dict(name=c['name'], note=c['note'], expect=c['expect'], tries=[])
    try:
        mon = Monitor(os.path.join(d, 'mon.sock'))
        r['tries'].append(boot(mon, d, 1, wait, c['boot_tries']))
        r['boot'] = r['tries'][0] > 0
        if r['boot']:
            time.sleep(2 if slow else 0.5)
            mon('sendkey 4 1600'); time.sleep(0.3)
            for k in 'asdfgh': mon('sendkey ' + k); time.sleep(0.12)
            time.sleep(0.8)
            mon('screendump ' + os.path.join(d, 'screen.ppm')); time.sleep(0.6 if slow else 0.3)
            for k in c['pre']:
                if k.startswith('wait:'): time.sleep(int(k[5:]) / 1000 * (3 if slow else 1))
                else: mon('sendkey ' + k); time.sleep(0.25 if slow else 0.12)
            if saves(c):
                for k in ['f2', 'minus', 'minus', 'minus', 'f7', 's', 't', 'e', 's', 't', 'ret']: mon('sendkey ' + k); time.sleep(0.25 if slow else 0.12)
                r['saved'] = wait_for(d, 'disk: saved', 1, 30)
                mon('system_reset')
                r['tries'].append(boot(mon, d, 2, wait, c['boot_tries']))
                r['reloaded'] = r['tries'][1] > 0 and seen(d, 'app: autoloaded last project') > 0
        else:
            mon('screendump ' + os.path.join(d, 'screen.ppm')); time.sleep(0.5)
        mon('quit')
    except Exception as e:
        r['error'] = str(e)
    try: p.wait(10)
    except subprocess.TimeoutExpired: p.kill()
    text = log(d)
    r['exception'] = next((l.strip() for l in text.splitlines() if 'EXCEPTION' in l), '')
    r['audio'] = ('HDA' if 'audio: Intel HDA' in text else 'AC97' if 'audio: AC97' in text else 'SB16' if 'audio: Sound Blaster 16' in text
                  else 'speaker' if 'PC speaker fallback' in text else '-')
    fbl = next((l for l in text.splitlines() if l.startswith('framebuffer ')), '')
    r['fb'] = (fbl.split()[1] + ' ' + fbl.split()[5] + 'bpp' if fbl.startswith('framebuffer ') and len(fbl.split()) > 5
               else 'no picture' if 'without a picture' in text else '-')
    r['storage'] = 'yes' if 'project slots' in text else 'no'
    shot = os.path.join(d, 'screen.ppm')
    w = 0
    if c['screen'] == 'none': r['screen'] = 'without a picture' in text        # no display device to dump
    else: r['screen'], w, h = screen_ok(shot, c['screen']) if os.path.exists(shot) else (False, 0, 0)
    fb_w = int(fbl.split()[1].split('x')[0]) if fbl.startswith('framebuffer ') else 0
    if os.path.exists(shot): subprocess.run(['magick', shot, os.path.join(d, 'screen.png')], capture_output=True)
    r['loud'] = loudness(os.path.join(d, 'out.wav'))
    up = r.get('boot') and not r['exception'] and r['screen']
    stored = r.get('saved') and r.get('reloaded') if saves(c) else True
    loud = r['loud'] > 300
    missing = [t for t in c['log'] if t not in text]
    if missing: r['note'] = 'log lacks: ' + ', '.join(missing)
    if c['expect'] == 'no-boot': r['result'] = 'LIMIT' if not r.get('boot') else 'PASS'
    elif c['expect'] == 'no-picture': r['result'] = 'LIMIT' if up and loud else 'FAIL'
    elif c['expect'] == 'no-storage': r['result'] = 'LIMIT' if up and loud and r['storage'] == 'no' else 'FAIL'
    elif up and loud and stored and not missing: r['result'] = 'PASS'
    elif c['expect'] == 'no-keys' and up and not loud: r['result'] = 'LIMIT'
    elif c['expect'] == 'no-save' and up and loud and not r.get('saved') and seen(d, 'blk: write') > 0: r['result'] = 'EMU'
    else: r['result'] = 'FAIL'
    if r['result'] == 'PASS' and w and fb_w and w != fb_w: r['result'] = 'EMU'   # the display disagrees with the mode
    with open(os.path.join(d, 'result.txt'), 'w') as f: f.write(line(r) + '\n')
    return r

def line(r):
    store = ('saved+reloaded' if r.get('saved') and r.get('reloaded') else 'NOT RELOADED' if r.get('saved')
             else 'SAVE FAILED' if 'saved' in r else r['storage'])
    tries = r['tries'] if any(t > 1 for t in r['tries']) else []
    note = r['exception'] or r.get('error') or ((f"boot tries {'/'.join(map(str, tries))}. " if tries else '') + r['note'])
    return (f"{r['result']:5s} {r['name']:22s} boot {'yes' if r.get('boot') else 'NO ':3s}  screen {'ok' if r['screen'] else '--'}  "
            f"{r['fb']:15s} audio {r['audio']:7s} loud {r['loud']:6.0f}  storage {store:14s} {note}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=max(2, (os.cpu_count() or 4) // 3))
    ap.add_argument('--only', default='')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    todo = [c for c in CONFIGS if any(o in c['name'] for o in a.only.split(','))]
    if a.list:
        for c in todo: print(c['name'], c['note'])
        return
    os.makedirs(OUT, exist_ok=True)
    results = []
    with cf.ThreadPoolExecutor(a.j) as ex:
        for r in ex.map(run, todo):
            results.append(r)
            print(line(r), flush=True)
    counts = {k: sum(r['result'] == k for r in results) for k in ('PASS', 'LIMIT', 'EMU', 'FAIL')}
    fails = [r['name'] for r in results if r['result'] == 'FAIL']
    print(f"{len(results)} machines: {counts['PASS']} pass, {counts['LIMIT']} at a known limit, {counts['EMU']} hit an emulator bug, "
          f"{counts['FAIL']} fail"
          + (' — ' + ', '.join(fails) if fails else ''))
    # summary and contact sheet cover every machine with a result, from this run or an earlier one (--only re-runs)
    rows = []
    for c in CONFIGS:
        res = os.path.join(OUT, c['name'], 'result.txt')
        if os.path.exists(res): rows.append((c['name'], open(res).read().rstrip('\n')))
    with open(os.path.join(OUT, 'summary.txt'), 'w') as f: f.write('\n'.join(l for _, l in rows) + '\n')
    sheet = []
    for name, l in rows:
        png = os.path.join(OUT, name, 'screen.png')
        if os.path.exists(png): sheet += ['-label', f"{name}  {l.split()[0]}", png]
    if sheet:
        subprocess.run(['magick', 'montage', '-background', '#0c0f14', '-fill', '#b6c2ce', '-pointsize', '15', *sheet,
                        '-tile', '6x', '-geometry', '320x200+6+6', os.path.join(OUT, 'contact.png')], capture_output=True)
    sys.exit(1 if fails else 0)

main()
