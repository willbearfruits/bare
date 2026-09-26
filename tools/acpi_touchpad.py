#!/usr/bin/env python3
"""An SSDT declaring an I2C touchpad the way laptop firmware does, assembled by hand (no iasl needed), for testing
the kernel's ACPI reading in QEMU:  python3 tools/acpi_touchpad.py build/touchpad.aml
                                    HB_QEMU_EXTRA="-acpitable file=build/touchpad.aml" python3 tools/qemu-test.py ...
It holds the equivalent of:
    Scope (\\_SB) {
        Device (I2C0) { Name (_ADR, 0x00150000) }
        Device (TPD0) {
            Name (_HID, "ELAN1200")
            Name (_CID, EISAID ("PNP0C50"))
            Method (_CRS, 0, Serialized) {
                Name (SBFB, ResourceTemplate () { I2cSerialBusV2 (0x15, ControllerInitiated, 400000, AddressingMode7Bit, "\\\\_SB.I2C0") })
                Return (SBFB)
            }
        }
    }
"""
import struct, sys

def pkg(body):
    """AML PkgLength + body: the length counts its own bytes"""
    n = len(body)
    for extra in range(4):
        total = n + 1 + extra
        if extra == 0 and total < 64: return bytes([total]) + body
        if extra and total < (1 << (4 + 8 * extra)):
            head = [(extra << 6) | (total & 0x0F)] + [(total >> (4 + 8 * k)) & 0xFF for k in range(extra)]
            return bytes(head) + body
    raise ValueError('too long')

def name(seg, value): return b'\x08' + seg + value
def string(s): return b'\x0D' + s.encode() + b'\0'
def dword(v): return b'\x0C' + struct.pack('<I', v)
def device(seg, body): return b'\x5B\x82' + pkg(seg + body)

source = b'\\_SB.I2C0\0'
i2c = (bytes([2, 0, 1, 0x02]) + struct.pack('<H', 0) + bytes([1]) + struct.pack('<H', 6) + struct.pack('<IH', 400000, 0x15)
       + source)                                              # revision, source index, I2C, consumer; 7-bit; speed; address
template = b'\x8E' + struct.pack('<H', len(i2c)) + i2c + b'\x79\x00'
buffer = b'\x11' + pkg(b'\x0A' + bytes([len(template)]) + template)
crs = b'\x14' + pkg(b'_CRS' + bytes([0x08]) + name(b'SBFB', buffer) + b'\xA4' + b'SBFB')
body = b'\x10' + pkg(b'\\_SB_' + device(b'I2C0', name(b'_ADR', dword(0x00150000)))
                     + device(b'TPD0', name(b'_HID', string('ELAN1200')) + name(b'_CID', dword(0x500CD041)) + crs))
header = bytearray(b'SSDT' + struct.pack('<I', 36 + len(body)) + bytes([2, 0]) + b'HB    ' + b'TOUCHPAD'
                   + struct.pack('<I', 1) + b'HBPY' + struct.pack('<I', 1))
table = header + body
table[9] = (-sum(table)) & 0xFF
open(sys.argv[1] if len(sys.argv) > 1 else 'touchpad.aml', 'wb').write(table)
