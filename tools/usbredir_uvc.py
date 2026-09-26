#!/usr/bin/env python3
"""A USB webcam for QEMU (UVC 1.1, uncompressed YUY2), spoken over its usb-redir protocol — QEMU has no camera of its
own. High speed, an isochronous IN endpoint (0x81) every microframe in alternate setting 1 of the streaming interface.
The picture: a white disc going round a grey gradient, 10 frames a second, in 160x120 or 320x240 (whichever the
guest's probe picks).

QEMU side:  -chardev socket,id=ucam,path=SOCK,server=on,wait=off -device usb-redir,chardev=ucam,bus=hc.0
This side:  UsbCam(SOCK).start(); ...; cam.log (what the guest asked), cam.frames_sent
"""
import math, socket, struct, threading, time

HELLO, CONNECT, INTERFACE_INFO, EP_INFO = 0, 1, 4, 5
SET_CONFIGURATION, GET_CONFIGURATION, CONFIGURATION_STATUS = 6, 7, 8
SET_ALT, GET_ALT, ALT_STATUS = 9, 10, 11
START_ISO, STOP_ISO, ISO_STATUS = 12, 13, 14
START_INT, STOP_INT, INT_STATUS = 15, 16, 17
CANCEL = 21
CONTROL, BULK, ISO, INTR = 100, 101, 102, 103
OK, CANCELLED, STALL = 0, 1, 4
CAP_CONNECT_VERSION, CAP_EP_MAX_PACKET, CAP_64BIT_IDS, CAP_32BIT_BULK = 1, 4, 5, 6
MPS = 512                                                  # isochronous packets: a microframe's bytes
SIZES = [(160, 120), (320, 240)]
INTERVAL = 1000000                                         # 100 ns units: 10 fps

DEVICE = bytes([18, 1, 0x00, 0x02, 0xEF, 2, 1, 64, 0x45, 0x23, 0x01, 0xAB, 0x00, 0x01, 1, 2, 0, 1])

def _frame(i, w, h):
    return (bytes([30, 0x24, 0x05, i, 0]) + struct.pack('<HHIIII', w, h, w * h * 16 * 10, w * h * 16 * 10, w * h * 2, INTERVAL)
            + bytes([1]) + struct.pack('<I', INTERVAL))
_fmt = bytes([27, 0x24, 0x04, 1, len(SIZES)]) + b'YUY2' + bytes([0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71, 16, 1, 0, 0, 0, 0])
_frames = b''.join(_frame(i + 1, w, h) for i, (w, h) in enumerate(SIZES))
_vs_len = 14 + len(_fmt) + len(_frames)
_vs_head = bytes([14, 0x24, 0x01, 1]) + struct.pack('<H', _vs_len) + bytes([0x81, 0, 2, 0, 0, 0, 1, 0])
_vc = (bytes([13, 0x24, 0x01, 0x10, 0x01]) + struct.pack('<H', 13 + 18 + 9) + struct.pack('<I', 48000000) + bytes([1, 1])
       + bytes([18, 0x24, 0x02, 1, 0x01, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0])[:18]   # camera terminal
       + bytes([9, 0x24, 0x03, 2, 0x01, 0x01, 0, 1, 0]))   # output terminal (streaming), from 1
_ifaces = (bytes([8, 0x0B, 0, 2, 0x0E, 3, 0, 0])            # interface association
           + bytes([9, 4, 0, 0, 0, 0x0E, 1, 0, 0]) + _vc
           + bytes([9, 4, 1, 0, 0, 0x0E, 2, 0, 0]) + _vs_head + _fmt + _frames
           + bytes([9, 4, 1, 1, 1, 0x0E, 2, 0, 0]) + bytes([7, 5, 0x81, 5]) + struct.pack('<H', MPS) + bytes([1]))
CONFIG = bytes([9, 2]) + struct.pack('<H', 9 + len(_ifaces)) + bytes([2, 1, 0, 0x80, 250]) + _ifaces

def _string(s):
    b = s.encode('utf-16-le')
    return bytes([2 + len(b), 3]) + b
STRINGS = {0: bytes([4, 3, 0x09, 0x04]), 1: _string('bare test'), 2: _string('Test Camera')}

def picture(n, w, h):
    """YUY2: a white disc going round a grey gradient; frame n"""
    a = n * 0.3
    cx, cy, r = w / 2 + w / 3 * math.cos(a), h / 2 + h / 3 * math.sin(a), h / 8
    out = bytearray(w * h * 2)
    for y in range(h):
        row = y * w * 2
        for x in range(w):
            inside = (x - cx) ** 2 + (y - cy) ** 2 < r * r
            out[row + 2 * x] = 235 if inside else 16 + x * 100 // w
            out[row + 2 * x + 1] = 128
    return bytes(out)

class UsbCam:
    def __init__(self, path):
        self.path, self.log, self.lock = path, [], threading.Lock()
        self.configured, self.alt, self.streaming = False, 0, False
        self.probe = bytearray(34); self.frame_index = 1; self.frames_sent = 0

    def start(self):
        for _ in range(100):
            try:
                self.s = socket.socket(socket.AF_UNIX); self.s.connect(self.path); break
            except OSError: time.sleep(0.05)
        threading.Thread(target=self.run, daemon=True).start()

    def msg(self, typ, body=b'', data=b'', id=0):
        head = struct.pack('<III', typ, len(body) + len(data), id) if typ == HELLO else struct.pack('<IIQ', typ, len(body) + len(data), id)
        with self.lock: self.s.sendall(head + body + data)

    def ep_info(self):
        types, intervals, owner, mps = [255] * 32, [0] * 32, [0] * 32, [0] * 32
        types[0] = types[16] = 0; mps[0] = mps[16] = 64
        if self.alt == 1: types[17] = 1; intervals[17] = 1; owner[17] = 1; mps[17] = MPS   # 0x81 isochronous IN
        self.msg(EP_INFO, bytes(types) + bytes(intervals) + bytes(owner) + struct.pack('<32H', *mps))

    def plug(self):
        caps = (1 << CAP_CONNECT_VERSION) | (1 << CAP_EP_MAX_PACKET) | (1 << CAP_64BIT_IDS) | (1 << CAP_32BIT_BULK)
        self.msg(HELLO, b'bare-uvc'.ljust(64, b'\0'), struct.pack('<I', caps))
        ifaces = bytes([0, 1] + [0] * 30), bytes([0x0E, 0x0E] + [0] * 30), bytes([1, 2] + [0] * 30), bytes(32)
        self.msg(INTERFACE_INFO, struct.pack('<I', 2) + b''.join(ifaces))
        self.ep_info()
        self.msg(CONNECT, struct.pack('<BBBBHHH', 2, 0xEF, 2, 1, 0x2345, 0xAB01, 0x0100))            # high speed

    def recv(self, n):
        b = b''
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c: raise EOFError
            b += c
        return b

    def run(self):
        try:
            self.plug()
            first = True
            while True:
                if first: typ, length, id = struct.unpack('<III', self.recv(12))
                else: typ, length, id = struct.unpack('<IIQ', self.recv(16))
                first = False
                body = self.recv(length) if length else b''
                self.handle(typ, body, id)
        except (EOFError, OSError):
            self.streaming = False

    def stream(self):
        """the frames, as payloads of MPS bytes (a 2-byte header each), paced a few microframes at a time"""
        fid, n = 0, 0
        while self.streaming:
            w, h = SIZES[self.frame_index - 1]
            pic, t0 = picture(n, w, h), time.time()
            at, room = 0, MPS - 2
            while at < len(pic) and self.streaming:
                for _ in range(8):                                             # a millisecond's worth
                    if at >= len(pic): break
                    chunk = pic[at:at + room]; at += len(chunk)
                    hdr = bytes([2, fid | (2 if at >= len(pic) else 0)])
                    self.msg(ISO, struct.pack('<BBH', 0x81, OK, len(chunk) + 2), hdr + chunk)
                time.sleep(0.001)
            fid ^= 1; n += 1; self.frames_sent += 1
            time.sleep(max(0.0, 0.1 - (time.time() - t0)))

    def handle(self, typ, body, id):
        if typ == CONTROL:
            ep, req, rt, _, value, index, length = struct.unpack('<BBBBHHH', body[:10])
            data, status = b'', OK
            if rt == 0x80 and req == 6:                                           # GET_DESCRIPTOR
                kind, i = value >> 8, value & 0xFF
                data = DEVICE if kind == 1 else CONFIG if kind == 2 else STRINGS.get(i, b'') if kind == 3 else b''
                if not data: status = STALL
            elif rt == 0x21 and req == 0x01 and value in (0x0100, 0x0200):       # SET_CUR probe / commit
                self.probe[:len(body) - 10] = body[10:10 + 34]
                self.frame_index = min(max(self.probe[3], 1), len(SIZES))
            elif rt == 0xA1 and req in (0x81, 0x82, 0x83, 0x87) and value in (0x0100, 0x0200):   # GET_CUR/MIN/MAX/DEF
                p = bytearray(self.probe); w, h = SIZES[self.frame_index - 1]
                p[2] = 1; p[3] = self.frame_index
                struct.pack_into('<I', p, 4, INTERVAL); struct.pack_into('<I', p, 18, w * h * 2); struct.pack_into('<I', p, 22, MPS)
                data = bytes(p)
            elif rt & 0x80: status = STALL
            data = data[:length]
            self.log.append(('control', hex(rt), hex(req), hex(value), status))
            self.msg(CONTROL, struct.pack('<BBBBHHH', ep, req, rt, status, value, index, len(data)), data, id)
        elif typ == SET_CONFIGURATION:
            self.configured = body[0] == 1
            self.msg(CONFIGURATION_STATUS, bytes([OK, body[0]]), id=id)
        elif typ == GET_CONFIGURATION:
            self.msg(CONFIGURATION_STATUS, bytes([OK, 1 if self.configured else 0]), id=id)
        elif typ == SET_ALT:
            if body[0] == 1: self.alt = body[1]
            if self.alt == 0: self.streaming = False
            self.log.append(('alt', body[0], body[1]))
            self.ep_info()
            self.msg(ALT_STATUS, bytes([OK, body[0], body[1]]), id=id)
        elif typ == GET_ALT:
            self.msg(ALT_STATUS, bytes([OK, body[0], self.alt if body[0] == 1 else 0]), id=id)
        elif typ == START_ISO:
            self.log.append(('iso start', body[0], body[1], body[2]))
            self.msg(ISO_STATUS, bytes([OK, body[0]]), id=id)
            if not self.streaming:
                self.streaming = True
                threading.Thread(target=self.stream, daemon=True).start()
        elif typ == STOP_ISO:
            self.streaming = False
            self.log.append(('iso stop',))
            self.msg(ISO_STATUS, bytes([OK, body[0]]), id=id)
        elif typ == CANCEL:
            pass
