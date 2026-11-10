#!/usr/bin/env python3
"""Isolated original ShapeStereo2 against C Iron stereo pair, before outer DC/mix."""
import argparse
import ctypes as C
import json
import math
import random
import struct
from compare_native_iron_transformer import NativeIron, Iron, HASH, ROOT


class NativeStereoIron(NativeIron):
    def __init__(self):
        super().__init__()
        self.stereo_fn = C.CFUNCTYPE(None, C.c_void_p, C.c_int,
                                    C.POINTER(C.c_float), C.POINTER(C.c_float))(self.base + 0xb14d0)
        self.right_sample = C.c_float()

    def process_stereo(self, left, right):
        self.f32(0x64c, 0); self.f32(0x654, 0)
        self.sample.value = left; self.right_sample.value = right
        self.stereo_fn(self.state, 1, C.byref(self.sample), C.byref(self.right_sample))
        return struct.unpack_from('<ff', self.state, 0x4c)


def f32(x): return C.c_float(x).value


def split_roundtrip(x):
    # Original stereo identity splitter still adds and subtracts 1e-4.
    return f32(f32(x + f32(.0001)) + f32(-.0001))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--library', default='build-release/libretro_dsp.so')
    parser.add_argument('--report', default='analysis/re/native-iron-stereo-comparison.json')
    args = parser.parse_args()
    lib = C.CDLL(str((ROOT / args.library).resolve()))
    lib.rs_iron_transformer_setup.argtypes = [C.POINTER(Iron), C.c_float, C.c_float]
    lib.rs_iron_transformer_reset.argtypes = [C.POINTER(Iron)]
    lib.rs_iron_transformer_process_stereo.argtypes = [C.POINTER(Iron), C.POINTER(C.c_float), C.POINTER(C.c_float)]
    native = NativeStereoIron()
    rng = random.Random(731)
    cases = 0
    samples = 0
    for sr in [1000, 8000, 44100, 48000, 96000, 192000, 1536000]:
        for amount in [0, .001, .125, .5, .875, 1]:
            port = Iron(); native.reset()
            native.setup(sr, amount); lib.rs_iron_transformer_setup(C.byref(port), sr, amount)
            assert native.inner() == bytes(port), ('initial', sr, amount)
            for i in range(8192):
                if i == 2048:
                    native.setup(sr, 1 - amount)
                    lib.rs_iron_transformer_setup(C.byref(port), sr, 1 - amount)
                if i == 4096:
                    native.reset(); lib.rs_iron_transformer_reset(C.byref(port))
                left = f32(rng.uniform(-2, 2) if i % 4 == 0 else math.sin(i * .037) * .8)
                right = f32(rng.uniform(-1.7, 1.7) if i % 3 == 0 else math.cos(i * .017) * .63)
                if i >= 6144: left = right = 0
                expected = native.process_stereo(left, right)
                l = C.c_float(split_roundtrip(left)); r = C.c_float(split_roundtrip(right))
                lib.rs_iron_transformer_process_stereo(C.byref(port), C.byref(l), C.byref(r))
                assert struct.pack('<ff', l.value, r.value) == struct.pack('<ff', *expected), ('audio', sr, amount, i, expected, l.value, r.value)
                assert native.inner() == bytes(port), ('state', sr, amount, i)
                samples += 2
            cases += 1
    report = dict(reference_sha256=HASH, scope='Numerical Iron stereo pair against original ShapeStereo2; synthetic identity splitter rounding applied to C input; output before outer DC/mix',
                  scenarios=cases, samples=samples, maximum_absolute_error=0, state_bit_exact=True,
                  dynamic_setup=True, reset=True, arithmetic_order='Existing mono per-channel process agrees with original stereo kernel')
    path = ROOT / args.report; path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report))


if __name__ == '__main__': main()
