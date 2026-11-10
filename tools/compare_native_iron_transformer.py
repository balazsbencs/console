#!/usr/bin/env python3
"""Compare reconstructed Iron core with isolated hash-pinned ShapeMono2.

Runs original numerical setup and mono processing only, with synthetic state.
Reads the final inner filter state before the outer DC/mix stage. No plugin
entry points, constructors, resources, host or original user presets execute.
"""
import argparse
import ctypes as C
import json
import math
import random
import struct
from pathlib import Path
from native_bitcolor_probe import NativeBitColor, HASH, ROOT


class NativeIron(NativeBitColor):
    def __init__(self):
        super().__init__()
        self.state = C.create_string_buffer(0x700)
        data = (ROOT / 'analysis/reference/rc20-1.0.5-x86_64.macho').read_bytes()
        for stub, name in [(0x57895a, 'atan'), (0x578a2c, 'exp'), (0x578c96, 'pow')]:
            assert data[stub:stub + 2] == b'\xff\x25'
            slot = stub + 6 + struct.unpack_from('<i', data, stub + 2)[0]
            struct.pack_into('<Q', self.image, slot, C.cast(getattr(self.libm, name), C.c_void_p).value)
        self.setup_fn = C.CFUNCTYPE(None, C.c_void_p, C.c_float, C.c_float)(self.base + 0xb1f60)
        self.mono_fn = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.POINTER(C.c_float))(self.base + 0xb05d0)
        for offset in [0x644, 0x630, 0x634, 0x5e8, 0x66c]: self.f32(offset, 1.0)
        self.sample = C.c_float()

    def setup(self, sr, amount):
        self.setup_fn(C.byref(self.state, 8), sr, amount)

    def process(self, sample):
        # Force the synthetic identity splitter history to zero so its
        # subtract/add round trip cannot perturb the input before the core.
        self.f32(0x64c, 0.0)
        self.sample.value = sample
        self.mono_fn(self.state, 1, C.byref(self.sample))
        # Last output filter's channel zero history; unaffected by outer path.
        return struct.unpack_from('<f', self.state, 0x4c)[0]

    def inner(self): return self.state.raw[8:8 + 168]

    def reset(self):
        for offset in [0, 4, 8, 12] + [base + i * 4 for base in [28, 56, 84, 112, 140] for i in range(3, 7)]:
            self.f32(8 + offset, 0)


class Iron(C.Structure):
    _fields_ = [('state', C.c_float * 42)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--library', default='build-release/libretro_dsp.so')
    parser.add_argument('--report', default='analysis/re/native-iron-transformer-comparison.json')
    args = parser.parse_args()
    lib = C.CDLL(str((ROOT / args.library).resolve()))
    lib.rs_iron_transformer_setup.argtypes = [C.POINTER(Iron), C.c_float, C.c_float]
    lib.rs_iron_transformer_setup.restype = C.c_int
    lib.rs_iron_transformer_reset.argtypes = [C.POINTER(Iron)]
    lib.rs_iron_transformer_process.argtypes = [C.POINTER(Iron), C.c_float, C.c_uint]
    lib.rs_iron_transformer_process.restype = C.c_float
    native = NativeIron()
    rng = random.Random(20261110)
    cases = []
    total = 0
    for sr in [1000, 8000, 44100, 48000, 96000, 192000, 1536000]:
        for amount in [0, 0.001, 0.125, 0.5, 0.875, 1]:
            port = Iron()
            native.reset()
            native.setup(sr, amount)
            assert lib.rs_iron_transformer_setup(C.byref(port), sr, amount)
            assert native.inner() == bytes(port), ('setup', sr, amount)
            for i in range(8192):
                if i == 2048:
                    amount2 = 1 - amount
                    native.setup(sr, amount2)
                    lib.rs_iron_transformer_setup(C.byref(port), sr, amount2)
                if i == 4096:
                    native.reset()
                    lib.rs_iron_transformer_reset(C.byref(port))
                sample = C.c_float((rng.uniform(-2, 2) if i % 4 == 0 else math.sin(i * .037) * 0.8)
                                   if i < 6144 else 0).value
                expected = native.process(sample)
                actual = lib.rs_iron_transformer_process(C.byref(port), sample, 0)
                assert struct.pack('<f', actual) == struct.pack('<f', expected), ('sample', sr, amount, i, expected, actual)
                assert native.inner() == bytes(port), ('state', sr, amount, i)
                total += 1
            cases.append(dict(sample_rate=sr, amount=amount, samples=8192))
    # Channel 1 has identical numerical rules with independent histories.
    native.reset(); port = Iron()
    native.setup(48000, .7); lib.rs_iron_transformer_setup(C.byref(port), 48000, .7)
    for i in range(8192):
        sample = C.c_float(rng.uniform(-1, 1)).value
        expected = native.process(sample)
        actual = lib.rs_iron_transformer_process(C.byref(port), sample, 1)
        assert struct.pack('<f', actual) == struct.pack('<f', expected), ('channel1', i)
        total += 1
    report = dict(scope='Isolated numerical Iron transformer setup and core, before outer DC/mix; synthetic zero histories',
                  reference_sha256=HASH, scenarios=len(cases) + 1, samples=total, maximum_absolute_error=0,
                  state_bit_exact=True, dynamic_setup=True, reset=True, cases=cases)
    path = ROOT / args.report
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'cases'}))


if __name__ == '__main__': main()
