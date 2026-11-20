#!/usr/bin/env python3
"""Isolated original NXAC numeric table initialization and RenderBlocks.

Constructs synthetic numeric cluster records from validated local containers.
No original constructor, resource stream, allocation or plugin entrypoint runs.
Decoded audio stays in memory; callers may retain hashes and comparison counts.
"""
import ctypes as C
import hashlib
import mmap
import math
from pathlib import Path
import struct
from native_bitcolor_probe import NativeBitColor,ROOT,HASH


class NativeNXAC(NativeBitColor):
    def __init__(self):
        super().__init__()
        for stub, name, library in [(0x578a32, 'exp2', self.libm),
                                    (0x578c1e, 'memset', C.CDLL(None))]:
            slot = stub + 6 + struct.unpack_from('<i', self.image, stub + 2)[0]
            struct.pack_into('<Q', self.image, slot,
                             C.cast(getattr(library, name), C.c_void_p).value)
        self._initialize_tables()
        self.render_fn = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_int,
                                     C.POINTER(C.c_float), C.POINTER(C.c_float))(
                                         self.base + 0x1516f0)
        self.state = C.create_string_buffer(0x68)
        self.left = (C.c_float * 2048)()
        self.right = (C.c_float * 2048)()
        self.info = None

    def _initialize_tables(self):
        # Execute only constructor's numeric loop 0x1684d0..0x16859b.
        # The shim supplies its expected frame and preserves SysV callee-saved registers.
        shim = mmap.mmap(-1, 256, prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
        address = C.addressof(C.c_char.from_buffer(shim))
        prologue = bytes.fromhex('55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec b8 01 00 00')
        epilogue = bytes.fromhex('48 81 c4 b8 01 00 00 5b 41 5c 41 5d 41 5e 41 5f 5d c3')
        shim[:len(prologue)] = prologue
        entry = b'\x48\xb8' + struct.pack('<Q', self.base + 0x1684d0) + b'\xff\xe0'
        shim[len(prologue):len(prologue)+len(entry)] = entry
        shim[128:128+len(epilogue)] = epilogue
        original = self.image[0x16859c:0x1685a8]
        self.image[0x16859c:0x1685a8] = b'\x48\xb8' + struct.pack('<Q', address + 128) + b'\xff\xe0'
        try:
            C.CFUNCTYPE(None)(address)()
        finally:
            self.image[0x16859c:0x1685a8] = original
            shim.close()
        self.gain_bits = self.image[0x8900c0:0x8904c0]
        self.delta_bits = self.image[0x8904c0:0x8908c0]

    def open(self, path):
        path = Path(path)
        if not path.is_file():
            raise ValueError(f'Asset is not a file: {path}')
        return self.open_bytes(path.read_bytes(), path.name)

    def open_bytes(self, encoded, name='<synthetic>'):
        encoded = bytes(encoded)
        # This fixture is only for structurally valid containers. Reject malformed
        # bytes before any pointer is exposed to the original numeric routine.
        if len(encoded) < 0x128 or encoded[:10] != b'NXAC v1.1\n':
            raise ValueError('Not an NXAC v1.1 container')
        magic, channels, frames, cluster, block, rate, table, table_size, data, data_size = \
            struct.unpack_from('<5If4I', encoded, 0x100)
        count = (frames+2047)//2048
        if (magic != 0x3a5920b3 or channels != 2 or frames == 0 or cluster != 2048 or
                block != 64 or not math.isfinite(rate) or rate <= 0 or table != 0x128 or
                table_size != count*16 or data != table+table_size or data+data_size != len(encoded)):
            raise ValueError('Invalid NXAC header or bounds')
        next_offset = 0
        for index in range(count):
            entry_rate, rms, offset, packed = struct.unpack_from('<ffII', encoded, table+index*16)
            formats = [packed & 255, (packed>>8) & 255]
            if (entry_rate != rate or not math.isfinite(rms) or rms < 0 or packed>>16 or
                    max(formats) > 9 or offset != next_offset or offset > 0x7fffffff):
                raise ValueError('Invalid NXAC cluster table')
            next_offset += 32*sum(0 if fmt == 0 else 16*(2+(fmt-1)%3)+2 for fmt in formats)
        if next_offset != data_size:
            raise ValueError('Invalid NXAC encoded data size')
        self.info = dict(file=name, sha256=hashlib.sha256(encoded).hexdigest(),
                         frames=frames, cluster_count=count, data_offset=data,
                         sample_rate=rate)
        self.encoded = bytes(encoded)
        self.encoded_buffer = C.create_string_buffer(self.encoded)
        self.records = C.create_string_buffer(self.info['cluster_count'] * 24)
        for index in range(self.info['cluster_count']):
            rate, rms, offset, packed = struct.unpack_from('<ffII', self.encoded, 0x128 + index * 16)
            struct.pack_into('<ffIIII', self.records, index * 24,
                             rate, rms, 0, offset, packed & 255, (packed >> 8) & 255)
        struct.pack_into('<Q', self.state, 0x30, C.addressof(self.records))
        struct.pack_into('<Q', self.state, 0x50,
                         C.addressof(self.encoded_buffer) + self.info['data_offset'])
        return self.info

    def render(self, cluster, start_block=0, block_count=32):
        if self.info is None or not 0 <= cluster < self.info['cluster_count']:
            raise ValueError('Invalid cluster')
        if not 0 <= start_block < 32 or block_count < 1:
            raise ValueError('Invalid block range')
        blocks = self.render_fn(self.state, cluster, start_block, block_count,
                                self.left, self.right)
        assert blocks == min(block_count, 32-start_block)
        count = blocks * 64 * 4
        return C.string_at(self.left, count), C.string_at(self.right, count)
