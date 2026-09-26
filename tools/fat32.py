"""Tiny FAT32 image writer (no mtools needed). Files are stored contiguously; long names get VFAT entries."""
import struct

SEC = 512

class Fat32:
    def __init__(self, size_bytes, label='BARE'):
        self.total = size_bytes // SEC
        self.spc = 1
        self.reserved = 32
        clusters = self.total - self.reserved
        self.fatsz = (clusters * 4 + SEC - 1) // SEC + 1
        self.data_start = self.reserved + 2 * self.fatsz
        self.clusters = (self.total - self.data_start) // self.spc
        assert self.clusters >= 65525, 'too small for FAT32'
        self.fat = [0] * (self.clusters + 2)
        self.fat[0], self.fat[1] = 0x0FFFFFF8, 0x0FFFFFFF
        self.next_free = 2
        self.data = bytearray(self.clusters * self.spc * SEC)
        self.label = label
        self.root = self._new_dir()
        # the volume label as a root directory entry too: that is the one desktops show (the boot sector's is ignored)
        self.root['entries'].append(label.upper().ljust(11).encode()[:11] + bytes([0x08]) + bytes(20))

    def _alloc(self, n):
        start = self.next_free
        for i in range(n):
            c = start + i
            self.fat[c] = c + 1 if i + 1 < n else 0x0FFFFFFF
        self.next_free += n
        return start

    def _new_dir(self):
        return {'cluster': self._alloc(1), 'entries': []}

    def _write_cluster_data(self, first, data):
        off = (first - 2) * self.spc * SEC
        self.data[off:off + len(data)] = data

    @staticmethod
    def _short(name):
        base, _, ext = name.partition('.')
        plain = len(base) <= 8 and len(ext) <= 3 and all(ch.isalnum() or ch in '_-' for ch in base + ext)
        if plain:
            return (base.upper().ljust(8) + ext.upper().ljust(3)).encode(), name != name.upper()
        b = ''.join(ch for ch in base.upper() if ch.isalnum())[:6] + '~1'
        return (b.ljust(8) + ext.upper()[:3].ljust(3)).encode(), True

    def _entries(self, name, attr, cluster, size):
        if name in ('.', '..'):
            short, need_lfn = name.ljust(11).encode(), False
        else:
            short, need_lfn = self._short(name)
        out = b''
        if need_lfn:
            csum = 0
            for b in short: csum = (((csum >> 1) | ((csum & 1) << 7)) + b) & 0xFF
            u = name.encode('utf-16-le') + b'\x00\x00'
            u += b'\xff' * ((-len(u)) % 26)
            chunks = [u[i:i + 26] for i in range(0, len(u), 26)]
            for i, ch in reversed(list(enumerate(chunks))):
                seq = (i + 1) | (0x40 if i == len(chunks) - 1 else 0)
                out += bytes([seq]) + ch[0:10] + bytes([0x0F, 0, csum]) + ch[10:22] + b'\x00\x00' + ch[22:26]
        out += short + bytes([attr]) + b'\x00' * 8 + struct.pack('<H', cluster >> 16) + b'\x00' * 4 + struct.pack('<HI', cluster & 0xFFFF, size)
        return out

    def mkdir(self, parent, name):
        d = self._new_dir()
        parent['entries'].append(self._entries(name, 0x10, d['cluster'], 0))
        d['entries'].append(self._entries('.', 0x10, d['cluster'], 0))
        d['entries'].append(self._entries('..', 0x10, 0 if parent is self.root else parent['cluster'], 0))
        return d

    def add(self, parent, name, data):
        n = max(1, (len(data) + self.spc * SEC - 1) // (self.spc * SEC))
        first = self._alloc(n) if data else 0
        if data: self._write_cluster_data(first, data)
        parent['entries'].append(self._entries(name, 0x20, first, len(data)))

    def _finish_dir(self, d):
        blob = b''.join(d['entries'])
        assert len(blob) <= self.spc * SEC, 'directory too big for one cluster'
        self._write_cluster_data(d['cluster'], blob.ljust(self.spc * SEC, b'\x00'))

    def build(self, all_dirs):
        for d in all_dirs: self._finish_dir(d)
        bs = bytearray(SEC)
        bs[0:3] = b'\xEB\x58\x90'; bs[3:11] = b'BARE    '
        struct.pack_into('<HBHBHHBHHHII', bs, 11, SEC, self.spc, self.reserved, 2, 0, 0, 0xF8, 0, 63, 255, 0, self.total)
        struct.pack_into('<IHHIHH', bs, 36, self.fatsz, 0, 0, 2, 1, 6)
        bs[64] = 0x80; bs[66] = 0x29; struct.pack_into('<I', bs, 67, 0x1234ABCD)
        bs[71:82] = self.label.ljust(11).encode()[:11]; bs[82:90] = b'FAT32   '
        bs[510:512] = b'\x55\xAA'
        fsinfo = bytearray(SEC)
        fsinfo[0:4] = b'RRaA'; fsinfo[484:488] = b'rrAa'
        struct.pack_into('<II', fsinfo, 488, self.clusters - (self.next_free - 2), self.next_free)
        fsinfo[510:512] = b'\x55\xAA'
        img = bytearray(self.total * SEC)
        img[0:SEC] = bs; img[SEC:2 * SEC] = fsinfo; img[6 * SEC:7 * SEC] = bs; img[7 * SEC:8 * SEC] = fsinfo
        fat = b''.join(struct.pack('<I', v) for v in self.fat).ljust(self.fatsz * SEC, b'\x00')
        for i in range(2):
            o = (self.reserved + i * self.fatsz) * SEC
            img[o:o + len(fat)] = fat
        o = self.data_start * SEC
        img[o:o + len(self.data)] = self.data
        return bytes(img)
