#!/usr/bin/env python3
"""A USB MIDI device for QEMU, spoken over its usb-redir protocol (QEMU has no USB MIDI device of its own).

QEMU side:  -chardev socket,id=umidi,path=SOCK,server=on,wait=off -device usb-redir,chardev=umidi,bus=xhci.0
This side:  UsbMidi(SOCK).start(); dev.send(b'\\x90\\x45\\x64'); ...; dev.received  (MIDI bytes the guest sent)

It plugs in when it connects (so the guest sees a hot-plug), answers the standard descriptors of a class-compliant
USB MIDI keyboard (one IN jack, one OUT jack, bulk endpoints 0x01 / 0x81) and turns MIDI bytes into USB-MIDI packets.
"""
import socket, struct, threading, time

HELLO, CONNECT, INTERFACE_INFO, EP_INFO = 0, 1, 4, 5
SET_CONFIGURATION, GET_CONFIGURATION, CONFIGURATION_STATUS = 6, 7, 8
SET_ALT, GET_ALT, ALT_STATUS = 9, 10, 11
START_INT, STOP_INT, INT_STATUS = 15, 16, 17
CANCEL = 21
CONTROL, BULK = 100, 101
OK, CANCELLED, STALL = 0, 1, 4
CAP_CONNECT_VERSION, CAP_EP_MAX_PACKET, CAP_64BIT_IDS, CAP_32BIT_BULK = 1, 4, 5, 6   # QEMU's xHCI wants the last three

DEVICE = bytes([18, 1, 0x10, 0x01, 0, 0, 0, 64, 0x34, 0x12, 0x78, 0x56, 0x00, 0x01, 1, 2, 0, 1])
_ms = bytes([7, 0x24, 1, 0x00, 0x01, 0, 0,                         # class-specific MS header (total patched below)
             6, 0x24, 2, 1, 1, 0,                                   # IN jack, embedded, id 1
             6, 0x24, 2, 2, 2, 0,                                   # IN jack, external, id 2
             9, 0x24, 3, 1, 3, 1, 2, 1, 0,                          # OUT jack, embedded, id 3, from 2
             9, 0x24, 3, 2, 4, 1, 1, 1, 0])                         # OUT jack, external, id 4, from 1
_ms = _ms[:5] + struct.pack('<H', len(_ms) + 9 + 5 + 9 + 5) + _ms[7:]
_ifaces = (bytes([9, 4, 0, 0, 0, 1, 1, 0, 0]) + bytes([9, 0x24, 1, 0, 1, 9, 0, 1, 1]) +      # audio control, pointing at 1
           bytes([9, 4, 1, 0, 2, 1, 3, 0, 0]) + _ms +                                         # MIDI streaming
           bytes([9, 5, 0x01, 2, 64, 0, 0, 0, 0]) + bytes([5, 0x25, 1, 1, 1]) +              # bulk OUT, jack 1
           bytes([9, 5, 0x81, 2, 64, 0, 0, 0, 0]) + bytes([5, 0x25, 1, 1, 3]))               # bulk IN, jack 3
CONFIG = bytes([9, 2]) + struct.pack('<H', 9 + len(_ifaces)) + bytes([2, 1, 0, 0x80, 50]) + _ifaces

def _string(s):
    b = s.encode('utf-16-le')
    return bytes([2 + len(b), 3]) + b
STRINGS = {0: bytes([4, 3, 0x09, 0x04]), 1: _string('bare test'), 2: _string('Test Keys')}
CIN = {0x80: 8, 0x90: 9, 0xA0: 0xA, 0xB0: 0xB, 0xC0: 0xC, 0xD0: 0xD, 0xE0: 0xE}

def packets(midi):
    """MIDI bytes (whole channel messages or single real-time bytes) → USB-MIDI packets"""
    out, i = b'', 0
    while i < len(midi):
        st = midi[i]
        if st >= 0xF8: out += bytes([0x0F, st, 0, 0]); i += 1; continue
        n = 2 if st & 0xE0 == 0xC0 else 3
        m = midi[i:i + n] + bytes(3 - n)
        out += bytes([CIN[st & 0xF0]]) + m[:3]; i += n
    return out

class UsbMidi:
    def __init__(self, path):
        self.path, self.received, self.log = path, bytearray(), []
        self.pending_in, self.to_send, self.lock = [], bytearray(), threading.Lock()
        self.configured = False

    def start(self):
        for _ in range(100):
            try:
                self.s = socket.socket(socket.AF_UNIX); self.s.connect(self.path); break
            except OSError: time.sleep(0.05)
        threading.Thread(target=self.run, daemon=True).start()

    def msg(self, typ, body=b'', data=b'', id=0):
        head = struct.pack('<III', typ, len(body) + len(data), id) if typ == HELLO else struct.pack('<IIQ', typ, len(body) + len(data), id)
        self.s.sendall(head + body + data)

    def bulk(self, ep, status, data=b'', length=None, id=0):
        n = len(data) if length is None else length
        self.msg(BULK, struct.pack('<BBHIH', ep, status, n & 0xFFFF, 0, n >> 16), data, id)

    def plug(self):
        caps = (1 << CAP_CONNECT_VERSION) | (1 << CAP_EP_MAX_PACKET) | (1 << CAP_64BIT_IDS) | (1 << CAP_32BIT_BULK)
        self.msg(HELLO, b'bare-usbmidi'.ljust(64, b'\0'), struct.pack('<I', caps))
        ifaces = bytes([0, 1] + [0] * 30), bytes([1, 1] + [0] * 30), bytes([1, 3] + [0] * 30), bytes(32)
        self.msg(INTERFACE_INFO, struct.pack('<I', 2) + b''.join(ifaces))
        types, intervals, owner, mps = [255] * 32, [0] * 32, [0] * 32, [0] * 32
        types[0] = types[16] = 0; mps[0] = mps[16] = 64                        # EP0 both ways
        types[1] = 2; owner[1] = 1; mps[1] = 64                               # 0x01 bulk OUT
        types[17] = 2; owner[17] = 1; mps[17] = 64                            # 0x81 bulk IN
        self.msg(EP_INFO, bytes(types) + bytes(intervals) + bytes(owner) + struct.pack('<32H', *mps))
        self.msg(CONNECT, struct.pack('<BBBBHHH', 1, 0, 0, 0, 0x1234, 0x5678, 0x0100))   # full speed

    def send(self, midi):
        with self.lock: self.to_send += packets(bytes(midi))
        self.flush()

    def flush(self):
        with self.lock:
            while self.pending_in and self.to_send:
                id, length = self.pending_in.pop(0)
                n = min(length, 64, len(self.to_send)) // 4 * 4
                data, self.to_send = bytes(self.to_send[:n]), self.to_send[n:]
                self.bulk(0x81, OK, data, id=id)

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
            first = True                                              # QEMU's hello has 32-bit ids, the rest 64
            while True:
                if first: typ, length, id = struct.unpack('<III', self.recv(12))
                else: typ, length, id = struct.unpack('<IIQ', self.recv(16))
                first = False
                body = self.recv(length) if length else b''
                self.handle(typ, body, id)
        except (EOFError, OSError):
            pass

    def handle(self, typ, body, id):
        if typ == CONTROL:
            ep, req, rt, _, value, index, length = struct.unpack('<BBBBHHH', body[:10])
            data, status = b'', OK
            if rt == 0x80 and req == 6:                                       # GET_DESCRIPTOR
                kind, i = value >> 8, value & 0xFF
                data = DEVICE if kind == 1 else CONFIG if kind == 2 else STRINGS.get(i, b'') if kind == 3 else b''
                if not data: status = STALL
            elif rt & 0x80: status = STALL
            data = data[:length]
            self.log.append(('control', hex(rt), req, hex(value), status))
            self.msg(CONTROL, struct.pack('<BBBBHHH', ep, req, rt, status, value, index, len(data)), data, id)
        elif typ == SET_CONFIGURATION:
            self.configured = body[0] == 1
            self.msg(CONFIGURATION_STATUS, bytes([OK, body[0]]), id=id)
        elif typ == GET_CONFIGURATION:
            self.msg(CONFIGURATION_STATUS, bytes([OK, 1 if self.configured else 0]), id=id)
        elif typ == SET_ALT:
            self.msg(ALT_STATUS, bytes([OK, body[0], body[1]]), id=id)
        elif typ == GET_ALT:
            self.msg(ALT_STATUS, bytes([OK, body[0], 0]), id=id)
        elif typ == BULK:
            ep, status, length, stream, high = struct.unpack('<BBHIH', body[:10])
            length |= high << 16
            if ep & 0x80:
                with self.lock: self.pending_in.append((id, length))
                self.flush()
            else:
                self.received += body[10:]
                self.bulk(ep, OK, length=length, id=id)
        elif typ == CANCEL:
            with self.lock:
                for k, (pid, length) in enumerate(self.pending_in):
                    if pid == id:
                        del self.pending_in[k]
                        self.bulk(0x81, CANCELLED, id=id)
                        break

    def midi_out(self):
        """what the guest sent, as MIDI bytes"""
        lens = {2: 2, 3: 3, 4: 3, 5: 1, 6: 2, 7: 3, 8: 3, 9: 3, 0xA: 3, 0xB: 3, 0xC: 2, 0xD: 2, 0xE: 3, 0xF: 1}
        out = bytearray()
        for i in range(0, len(self.received) - 3, 4):
            out += self.received[i + 1:i + 1 + lens.get(self.received[i] & 15, 0)]
        return bytes(out)
