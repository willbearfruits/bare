#!/usr/bin/env python3
"""Headless QEMU test driver: boots the ISO, sends keys, takes screenshots, records audio.
Usage: qemu-test.py [--keys "spec"] [--shot file.ppm] [--seconds N]
Key spec: space-separated QEMU key names; 'wait:MS' pauses; 'shot:NAME' screenshots; 'down:KEY'/'up:KEY' hold/release.
"""
import argparse, os, socket, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument('--keys', default='')
ap.add_argument('--splash', action='store_true')   # let the boot's splash play (else Esc ends it once the instrument runs)
ap.add_argument('--seconds', type=float, default=3.0)
ap.add_argument('--wav', default='')
ap.add_argument('--display', default='none')
ap.add_argument('--arch', default='i386')
ap.add_argument('--cpu', default='')   # e.g. pentium2 (forces TCG)
ap.add_argument('--img', action='store_true')   # boot the disk image as a USB stick instead of the ISO
ap.add_argument('--floppy', action='store_true')   # boot build/i386/bare-floppy.img (make floppy; or --image) from a 1.44 MB floppy drive
ap.add_argument('--image', default='')   # with --img: boot this file instead of build/<arch>/bare.img (e.g. a dd of a real stick)
ap.add_argument('--mem', default='512M')   # RAM size: real laptops have 2-4G, which exercises memory placement the default never does
ap.add_argument('--res', default='')   # WxH: the EDID-preferred mode Limine will pick (e.g. 1024x768 for an old laptop)
ap.add_argument('--sound', default='hda')   # hda, ac97, sb16 (the sound chip), none
ap.add_argument('--codec', default='hda-duplex')   # hda-duplex (line out + line in), hda-micro (speaker + microphone), hda-output
ap.add_argument('--midi', action='store_true')   # a second serial port (COM2) as FIFOs: 'midi:90,45,64' tokens write MIDI bytes
                                                 # into it; what the guest sends lands in build/<arch>/midi-out.bin
ap.add_argument('--uefi', action='store_true')   # OVMF instead of SeaBIOS (a copy of its variables in build/<arch>/)
ap.add_argument('--usb', default='')   # more devices on the xHCI controller, e.g. 'usb-kbd,usb-tablet' or 'usb-hub,usb-kbd@1.2'
                                       # (name@port puts it on a port, 1.2 = port 2 of the hub on port 1)
ap.add_argument('--camera', action='store_true')   # a USB webcam (tools/usbredir_uvc.py), plugged in once the guest runs
ap.add_argument('--usbmidi', action='store_true')   # a USB MIDI keyboard (tools/usbredir_midi.py), plugged in once the
                                                     # guest runs: 'umidi:90,45,64' tokens play into it
ap.add_argument('--disk', action='append', default=[])   # KIND:FILE, an internal disk: ahci, nvme, nvme4k (4 KiB blocks) or ide;
                                                          # repeatable. With no --img and some --disk, the machine starts from them
ap.add_argument('--audiodev', default='')   # replaces the wav audiodev, e.g. an ALSA file plugin that feeds the line input:
                                            # "alsa,id=snd0,in.dev=hbin,out.dev=hbout" with ALSA_CONFIG_PATH naming a file that defines them
a = ap.parse_args()

B = f'build/{a.arch}'
sock = f'{B}/monitor.sock'
qsock = f'{B}/qmp.sock'
if os.path.exists(qsock): os.remove(qsock)
if os.path.exists(sock): os.remove(sock)
kvm = ['-cpu', a.cpu] if a.cpu else (['-enable-kvm', '-cpu', 'host'] if os.access('/dev/kvm', os.W_OK) else [])
boot = ['-device', 'qemu-xhci,id=xhci', '-drive', f'if=none,id=stick,format=raw,file={a.image or B + "/bare.img"}', '-device', 'usb-storage,drive=stick,bus=xhci.0' + (',port=' + os.environ['HB_STICK_PORT'] if 'HB_STICK_PORT' in os.environ else '')] if a.img else ['-cdrom', f'{B}/bare.iso', '-boot', 'd']
if a.floppy: boot = ['-drive', f'if=floppy,format=raw,file={a.image or B + "/bare-floppy.img"}', '-boot', 'a']   # the pc machine: q35 has no floppy controller
disks = []
for i, spec in enumerate(a.disk):
    kind, _, path = spec.partition(':')
    disks += ['-drive', f'if=none,id=hd{i},format=raw,file={path}']
    order = f',bootindex={i + 1}'                  # after the stick: SeaBIOS tries only the first hard disk otherwise
    disks += {'ahci': ['-device', f'ide-hd,drive=hd{i},bus=ide.{i}' + order], 'ide': ['-device', f'ide-hd,drive=hd{i},bus=ide.{i}' + order],
              'nvme': ['-device', f'nvme,drive=hd{i},serial=hd{i}' + order],
              'nvme4k': ['-device', f'nvme,drive=hd{i},serial=hd{i},logical_block_size=4096,physical_block_size=4096' + order]}[kind]
if a.disk and not a.img: boot = []                  # started from the internal disks: no CD
elif a.disk: boot[-1] += ',bootindex=0'
boot += disks
if a.usb:
    if not a.img: boot += ['-device', 'qemu-xhci,id=xhci']
    for dev in a.usb.split(','):
        name, _, port = dev.partition('@')
        boot += ['-device', f'{name},bus=xhci.0' + (f',port={port}' if port else '')]
if a.usbmidi:
    if not a.img and not a.usb: boot += ['-device', 'qemu-xhci,id=xhci']
    umidi_sock = f'{B}/umidi.sock'
    if os.path.exists(umidi_sock): os.remove(umidi_sock)
    boot += ['-chardev', f'socket,id=umidi,path={umidi_sock},server=on,wait=off', '-device', 'usb-redir,chardev=umidi,bus=xhci.0']
if a.camera:
    if not a.img and not a.usb and not a.usbmidi: boot += ['-device', 'qemu-xhci,id=xhci']
    ucam_sock = f'{B}/ucam.sock'
    if os.path.exists(ucam_sock): os.remove(ucam_sock)
    boot += ['-chardev', f'socket,id=ucam,path={ucam_sock},server=on,wait=off', '-device', 'usb-redir,chardev=ucam,bus=xhci.0']
if a.uefi:
    import shutil
    shutil.copy('/usr/share/edk2/x64/OVMF_VARS.4m.fd', f'{B}/ovmf-vars.fd')
    boot += ['-drive', 'if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd', '-drive', f'if=pflash,format=raw,file={B}/ovmf-vars.fd']
vga = ['-vga', 'none', '-device', f'VGA,edid=on,xres={a.res.split("x")[0]},yres={a.res.split("x")[1]}'] if a.res else []
midi = []
if a.midi:
    for end in ('in', 'out'):
        f = f'{B}/midi.{end}'
        if os.path.exists(f): os.remove(f)
        os.mkfifo(f)
    midi_in = os.open(f'{B}/midi.in', os.O_RDWR); midi_out = os.open(f'{B}/midi.out', os.O_RDWR | os.O_NONBLOCK)
    midi = ['-serial', f'pipe:{B}/midi']
cmd = ['qemu-system-x86_64', '-M', ('pc' if a.floppy else 'q35') + ',pcspk-audiodev=snd0' + os.environ.get('HB_MACHINE', ''), '-m', a.mem, *kvm, *boot, *vga,
       *({'hda': ['-device', 'intel-hda,debug=' + os.environ.get('HDA_DEBUG', '0'), '-device', f'{a.codec},audiodev=snd0'],
          'ac97': ['-device', 'AC97,audiodev=snd0'], 'sb16': ['-device', 'sb16,audiodev=snd0'], 'none': []}[a.sound]), '-audiodev', a.audiodev or f'wav,id=snd0,path={a.wav or B + "/out.wav"}',
       '-serial', f'file:{B}/serial.log', *midi, *os.environ.get('HB_QEMU_EXTRA', '').split(), '-display', a.display, '-monitor', f'unix:{sock},server,nowait', '-qmp', f'unix:{qsock},server,nowait']
if os.environ.get("HB_SHOW_CMD"): print(" ".join(cmd))
p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
for _ in range(100):
    if os.path.exists(sock): break
    time.sleep(0.05)
m = socket.socket(socket.AF_UNIX); m.connect(sock); m.settimeout(2)
def mon(c):
    m.sendall((c + '\n').encode()); time.sleep(0.15)
    try: return m.recv(65536).decode(errors='replace')
    except socket.timeout: return ''
mon('')
import json
q = socket.socket(socket.AF_UNIX); q.connect(qsock); q.settimeout(2)
q.recv(65536); q.sendall(b'{"execute":"qmp_capabilities"}\n'); q.recv(65536)
def qmp(cmd, args):
    q.sendall((json.dumps({'execute': cmd, 'arguments': args}) + '\n').encode()); time.sleep(0.03)
    try: return q.recv(65536)
    except socket.timeout: return b''
def abs_move(x, y):   # 0..32767
    qmp('input-send-event', {'events': [{'type': 'abs', 'data': {'axis': 'x', 'value': x}}, {'type': 'abs', 'data': {'axis': 'y', 'value': y}}]})
def btn(down):
    qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
if a.usbmidi:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from usbredir_midi import UsbMidi
    umidi = UsbMidi(umidi_sock)
time.sleep(a.seconds)                      # let it boot
def logtext():
    try: return open(f'{B}/serial.log', errors='replace').read()
    except OSError: return ''
if not a.splash:                           # the splash takes the first key: end it before the script starts
    end = time.time() + 60
    while time.time() < end and 'running' not in logtext().splitlines(): time.sleep(0.1)
    if 'splash:' in logtext():
        mon('sendkey esc')
        end = time.time() + 5
        while time.time() < end and 'splash: skipped' not in logtext() and 'splash: done' not in logtext(): time.sleep(0.05)
if a.usbmidi: umidi.start(); time.sleep(1.0)   # plugged in now: the guest finds it as a hot-plug
if a.camera:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from usbredir_uvc import UsbCam
    ucam = UsbCam(ucam_sock); ucam.start(); time.sleep(1.5)
for tok in a.keys.split():
    if tok.startswith('wait:'): time.sleep(int(tok[5:]) / 1000)
    elif tok.startswith('shot:'): mon(f'screendump {B}/{tok[5:]}.ppm')
    elif tok.startswith('abs:'): x, y = tok[4:].split(','); abs_move(int(x), int(y))
    elif tok == 'down': btn(True)
    elif tok == 'up': btn(False)
    elif tok.startswith('mm:'): dx, dy = tok[3:].split(','); mon(f'mouse_move {dx} {dy}')
    elif tok.startswith('mb:'): mon(f'mouse_button {tok[3:]}')
    elif tok.startswith('midi:'): os.write(midi_in, bytes(int(h, 16) for h in tok[5:].split(',')))
    elif tok.startswith('umidi:'): umidi.send(bytes(int(h, 16) for h in tok[6:].split(',')))
    elif ':' in tok: k, ms = tok.rsplit(':', 1); mon(f'sendkey {k} {ms}')
    else: mon(f'sendkey {tok}')
time.sleep(0.5)
if a.midi:
    got = b''
    try:
        while True: got += os.read(midi_out, 4096)
    except BlockingIOError: pass
    open(f'{B}/midi-out.bin', 'wb').write(got)
    print(f'midi out: {len(got)} bytes, {got.count(0xF8)} clocks, {sum(1 for i in range(len(got) - 2) if got[i] & 0xF0 == 0x90 and got[i + 2])} note-ons')
if a.usbmidi:
    got = umidi.midi_out()
    open(f'{B}/umidi-out.bin', 'wb').write(got)
    print(f'usb midi out: {len(got)} bytes, {got.count(0xF8)} clocks, {sum(1 for i in range(len(got) - 2) if got[i] & 0xF0 == 0x90 and got[i + 2])} note-ons')
if a.camera: print('camera: %d frames sent; %s' % (ucam.frames_sent, ucam.log[-12:]))
mon(f'screendump {B}/final.ppm')
mon('quit')
try: p.wait(5)
except subprocess.TimeoutExpired: p.kill()
err = p.stderr.read().decode()
if err.strip(): print('qemu stderr:', err.strip()[:2000])
print(open(f'{B}/serial.log', errors='replace').read())
