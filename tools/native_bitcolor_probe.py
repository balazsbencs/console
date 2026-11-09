#!/usr/bin/env python3
"""Run only the recovered cBitColor DSP kernel on Linux x86_64.

This is an isolated kernel experiment, not a macOS plugin host. It maps the
hash-pinned Mach-O segments and binds four math imports to the system libm.
It never runs plugin entry points, constructors, installers or resource scripts.
The object is a synthetic, explicitly initialized DSP state. Execute this tool
in a separate process so a bad reconstruction cannot crash an audio host.
"""
import ctypes as C
import hashlib
import json
import math
import mmap
from pathlib import Path
import platform
import struct

ROOT = Path(__file__).resolve().parents[1]
HASH = 'f7a947340465e22cf5af81a0402d59e5cbff7e517e60d5554e5f2a5861df810a'


class NativeBitColor:
    def __init__(self, sample_rate=48000.0, bits=6.0, rate=4000.0, smooth=0.0,
                 mix=1.0, feedback=0.0, focus=1.0, low=20.0, high=20000.0,
                 round_error=0.01, stereo=1.0, aa2=0.5, aa3=0.65,
                 normalized=0.0, mod_rate=0.0, mod_depth=0.0):
        if platform.machine() != 'x86_64' or platform.system() != 'Linux':
            raise RuntimeError('This probe requires Linux x86_64')
        data = (ROOT / 'analysis/reference/rc20-1.0.5-x86_64.macho').read_bytes()
        if hashlib.sha256(data).hexdigest() != HASH:
            raise ValueError('Reference hash mismatch')
        segments = []
        cursor = 32
        for _ in range(struct.unpack_from('<I', data, 16)[0]):
            command, size = struct.unpack_from('<II', data, cursor)
            if command == 0x19:
                segments.append(struct.unpack_from('<QQQQ', data, cursor + 24))
            cursor += size
        self.image = mmap.mmap(-1, max(vm + size for vm, size, _, _ in segments),
                               prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
        for vm, _, fileoff, size in segments:
            self.image[vm:vm + size] = data[fileoff:fileoff + size]
        self.base = C.addressof(C.c_char.from_buffer(self.image))
        self.libm = C.CDLL('libm.so.6')
        for stub, name in [(0x578C9C, 'powf'), (0x578A38, 'exp2f'),
                           (0x578A80, 'floorf'), (0x578A3E, 'expf'), (0x578BE2, 'logf')]:
            if data[stub:stub + 2] != b'\xff\x25':
                raise ValueError('Unexpected math import stub')
            slot = stub + 6 + struct.unpack_from('<i', data, stub + 2)[0]
            struct.pack_into('<Q', self.image, slot, C.cast(getattr(self.libm, name), C.c_void_p).value)
        self.state = C.create_string_buffer(0x960)
        self.callbacks = []
        self.normalized = bool(normalized)
        self.rate_value = rate
        self.rate_slot = [rate]
        self.scale_fn = C.CFUNCTYPE(C.c_float, C.c_float, C.c_void_p)
        self.low, self.high, self.focus = low, high, focus
        raw_bits, raw_rate = (bits, rate) if normalized else (0.5, 0.5)
        physical_bits = self.scale(bits, 24, 1, 6) if normalized else bits
        self.f32(0xF4, 0.0)
        struct.pack_into('<i', self.state, 0xF4, 2)
        self.f32(0xF8, sample_rate); self.f32(0x93C, sample_rate)
        for offset, value in [(0x108, 1), (0x140, mix), (0x358, mod_rate), (0x390, mod_depth),
                              (0x3C8, smooth), (0x5E0, aa2), (0x618, aa3), (0x650, feedback)]:
            self.wrap(offset, value)
        for offset, raw, physical in [(0x178, self.log_normalize(low), low), (0x218, self.log_normalize(high), high),
                                      (0x2B8, raw_rate, rate), (0x688, raw_bits, physical_bits),
                                      (0x400, round_error, round_error),
                                      (0x4A0, stereo, stereo), (0x540, focus, focus)]:
            self.param(offset, raw, physical)
        self.metadata(0x2B8, 44100 if normalized else 10, 44.1 if normalized else 44100, 200)
        self.metadata(0x688, 24 if normalized else 1, 1 if normalized else 24, 6 if normalized else 5)
        for offset in [0x178, 0x218]:
            self.metadata(offset, 20, 20000, 0)
            struct.pack_into('<Q', self.state, offset + 0x28, self.base + 0x93B50)
        # Physical conversion callback for the current static SR setting.
        callback = C.CFUNCTYPE(C.c_float, C.c_void_p)(lambda context, slot=self.rate_slot: slot[0])
        self.callbacks.append(callback)
        struct.pack_into('<Q', self.state, 0x2E0,
                         self.base + 0x94030 if normalized else C.cast(callback, C.c_void_p).value)
        for offset in [0x72C, 0x740]:
            self.f32(offset, -1); self.f32(offset + 4, 0.5)
        self.f32(0x798, 0); self.f32(0x79C, 1); self.f32(0x794, -1)
        self.f32(0x7AC, 0.5)
        for offset in range(0x7B4, 0x8B4, 4): self.f32(offset, 1)
        for offset, cutoff in [(0x8CC, 320), (0x8E8, 320), (0x904, 10), (0x920, 10)]:
            value = {0x8CC: physical_bits, 0x8E8: raw_rate, 0x904: 1, 0x920: mix}[offset]
            self.f32(offset, value); self.f32(offset + 4, value)
            coefficient = self.exp32(self.float32(cutoff / sample_rate) * self.float32(-6.2831855))
            self.f32(offset + 0x18, coefficient)
            self.f32(offset + 0x10, 1 - coefficient)
        self.process_fn = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p)(self.base + 0x345DF0)
        def pattern_fill(destination, pattern, size):
            C.memmove(destination, C.string_at(pattern, 16) * (size // 16), size)
        pattern_callback = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_size_t)(pattern_fill)
        self.callbacks.append(pattern_callback)
        slot = 0x578C24 + 6 + struct.unpack_from('<i', data, 0x578C24 + 2)[0]
        struct.pack_into('<Q', self.image, slot, C.cast(pattern_callback, C.c_void_p).value)
        self.reset_fn = C.CFUNCTYPE(None, C.c_void_p)(self.base + 0x347CC0)

    def reset(self):
        self.reset_fn(self.state)

    def metadata(self, offset, low, high, middle):
        self.f32(offset + 0x38, low); self.f32(offset + 0x3C, high); self.f32(offset + 0x40, middle)

    def scale(self, x, low, high, middle=0, logarithmic=False):
        context = C.create_string_buffer(0x40)
        struct.pack_into('<fff', context, 0x18, low, high, middle)
        return self.scale_fn(self.base + (0x93B50 if logarithmic else 0x94030))(x, context)

    def log_normalize(self, value):
        self.libm.logf.argtypes = [C.c_float]; self.libm.logf.restype = C.c_float
        return self.float32(self.libm.logf(self.float32(value / 20)) / self.libm.logf(1000))

    def set(self, **values):
        wrappers = dict(mix=0x140, smooth=0x3C8, feedback=0x650, mod_rate=0x358,
                        mod_depth=0x390, aa2=0x5E0, aa3=0x618)
        for name, value in values.items():
            if name in wrappers: self.wrap(wrappers[name], value)
            elif name == 'bits': self.param(0x688, value if self.normalized else 0.5,
                self.scale(value, 24, 1, 6) if self.normalized else value)
            elif name == 'rate':
                self.rate_value = value
                self.rate_slot[0] = value
                self.param(0x2B8, value if self.normalized else 0.5, value)
            elif name in ['low', 'high']:
                setattr(self, name, value)
                self.param(0x178 if name == 'low' else 0x218, self.log_normalize(value), value)
                self.f32(0x794, -1)
            elif name == 'focus':
                self.focus = value; self.param(0x540, value, value); self.f32(0x794, -1)
            elif name in ['round_error', 'stereo']:
                self.param(0x400 if name == 'round_error' else 0x4A0, value, value)
            else: raise ValueError(f'Unsupported update: {name}')

    @staticmethod
    def float32(x): return C.c_float(x).value

    def exp32(self, x):
        self.libm.expf.argtypes = [C.c_float]; self.libm.expf.restype = C.c_float
        return self.libm.expf(x)

    def f32(self, offset, value): struct.pack_into('<f', self.state, offset, value)

    def wrap(self, offset, value):
        self.state[offset + 0x2C] = b'\x01'; self.state[offset + 0x2D] = b'\x01'
        self.f32(offset + 0x30, value)

    def param(self, offset, raw, physical):
        self.state[offset + 0x94] = b'\x01'; self.state[offset + 0x95] = b'\x01'
        self.f32(offset + 0x98, raw); self.f32(offset + 0x9C, physical)

    def process(self, left, right):
        assert len(left) == len(right) == 64
        l = (C.c_float * 64)(*left); r = (C.c_float * 64)(*right)
        pointers = (C.c_void_p * 2)(C.addressof(l), C.addressof(r))
        batch = C.create_string_buffer(32)
        struct.pack_into('<Q', batch, 8, C.addressof(pointers))
        struct.pack_into('<i', batch, 0x10, 2); struct.pack_into('<f', batch, 0x18, 1)
        self.process_fn(self.state, batch)
        return list(l), list(r)


if __name__ == '__main__':
    native = NativeBitColor()
    left, right = native.process([math.sin(i * 0.1) * 0.5 for i in range(64)],
                                 [math.cos(i * 0.13) * 0.4 for i in range(64)])
    assert all(math.isfinite(v) for v in left + right)
    print(json.dumps(dict(scope='isolated native cBitColor kernel', reference_sha256=HASH,
                          left_preview=left[:8], right_preview=right[:8])))
