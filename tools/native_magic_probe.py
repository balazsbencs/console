#!/usr/bin/env python3
"""Isolated original Magic Process with synthetic state; no constructors/host.

Loads the supplied hash-pinned Mach-O through the established native loader.
Only numerical CalcInternals and stereo Process execute. Air delay rings are
caller-owned zeroed buffers. The full RC-20 plugin is never loaded by a host.
"""
import ctypes as C
import math
import struct
from native_bitcolor_probe import NativeBitColor, HASH, ROOT


def f32(x): return C.c_float(x).value


class NativeMagic(NativeBitColor):
    def __init__(self, sample_rate=48000.0, **params):
        super().__init__()
        self.state = C.create_string_buffer(0x700)
        for stub, name in [(0x578a2c, 'exp'), (0x57895a, 'atan'), (0x578c96, 'pow')]:
            slot = stub + 6 + struct.unpack_from('<i', self.image, stub + 2)[0]
            struct.pack_into('<Q', self.image, slot, C.cast(getattr(self.libm, name), C.c_void_p).value)
        self.libm.exp.argtypes = [C.c_double]; self.libm.exp.restype = C.c_double
        self.params = dict(amount=50, character=0, low_hz=20, high_hz=20000,
                           dist_db=0, pass_db=0, mix=100, output_hz=20000)
        self.params.update(params)
        self.rate = f32(sample_rate)
        self.f32(0x608, self.rate)
        struct.pack_into('<I', self.state, 0x60c, 64)
        capacity = 4
        while capacity < int(f32(self.rate * f32(2.2675736545352265e-05))) + 4: capacity *= 2
        self.rings = [(C.c_float * capacity)(), (C.c_float * capacity)()]
        for c in range(2):
            struct.pack_into('<QII', self.state, 0x290 + c * 16, C.addressof(self.rings[c]), capacity - 1, 0)
        # ResetAudio's numerical mix-filter initialization, without its broad
        # resets/constructor-dependent ring ownership.
        cutoff = min(f32(self.rate * f32(.499)), 50.0)
        angle = f32((float(cutoff) * -6.2831852) / self.rate)
        pole = f32(self.libm.exp(angle))
        b = f32(f32(1 - pole) / f32(1.12))
        self.f32(0x5e4, pole); self.f32(0x5e8, b); self.f32(0x5ec, f32(b * f32(.12)))
        self.calc_fn = C.CFUNCTYPE(None, C.c_void_p)(self.base + 0xafc70)
        self.magic_fn = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_bool,
                                   C.POINTER(C.c_bool), C.POINTER(C.c_float), C.POINTER(C.c_float))(self.base + 0xaf570)
        self.set(**self.params)

    def set(self, **params):
        self.params.update(params)
        for name, offset in dict(amount=0x614, low_hz=0x624, high_hz=0x620,
                                 dist_db=0x618, pass_db=0x61c, mix=0x628, output_hz=0x668).items():
            self.f32(offset, self.params[name])
        self.f32(0x62c, 0)
        struct.pack_into('<I', self.state, 0x604, int(self.params['character']))
        self.calc_fn(self.state)
        self.state[0x600] = b'\x00'

    def process(self, left, right):
        assert len(left) == len(right) == 64
        l = (C.c_float * 64)(*left); r = (C.c_float * 64)(*right)
        silent = C.c_bool(False)
        self.magic_fn(self.state, 64, False, C.byref(silent), l, r)
        assert not silent.value
        return list(l), list(r)
