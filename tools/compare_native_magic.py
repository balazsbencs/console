#!/usr/bin/env python3
"""Compare complete numerical Magic stereo wrapper, fixed 64-frame cleanup."""
import argparse
import ctypes as C
import gc
import json
import math
import random
import struct
from native_magic_probe import NativeMagic, HASH, ROOT, f32


class Params(C.Structure):
    _fields_ = [(name, C.c_float) for name in ['amount', 'character', 'low_hz', 'high_hz',
                                            'dist_db', 'pass_db', 'mix', 'output_hz']]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--library', default='build-release/libretro_dsp.so')
    ap.add_argument('--report', default='analysis/re/native-magic-comparison.json')
    args = ap.parse_args(); lib = C.CDLL(str((ROOT / args.library).resolve()))
    lib.retro_magic_create.restype = C.c_void_p
    lib.retro_magic_destroy.argtypes = [C.c_void_p]
    lib.retro_magic_set.argtypes = [C.c_void_p, C.POINTER(Params)]
    lib.retro_magic_prepare.argtypes = [C.c_void_p, C.c_float]
    lib.retro_magic_reset.argtypes = [C.c_void_p]
    lib.retro_magic_process.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_size_t]
    count = samples = 0
    grid = [dict(), dict(amount=0, mix=0), dict(amount=100, low_hz=2000, high_hz=5000, mix=72, dist_db=7, pass_db=-3, output_hz=12000),
            dict(amount=13, low_hz=8000, high_hz=1500, mix=30, dist_db=-15, pass_db=4, output_hz=600),
            dict(amount=100, low_hz=20, high_hz=20000, mix=100, dist_db=100, pass_db=-100, output_hz=20),
            dict(amount=1, low_hz=20000, high_hz=20, mix=99, dist_db=-100, pass_db=100, output_hz=20000)]
    rates = [1000, 8000, 11025, 12345, 22050, 44100, 48000, 48001, 44117, 88200, 96000, 123456, 192000, 1536000]
    for sr in rates:
      for character in range(3):
       for variation, initial in enumerate(grid):
        p = dict(amount=50, character=character, low_hz=20, high_hz=20000,
                 dist_db=0, pass_db=0, mix=100, output_hz=20000); p.update(initial)
        n = NativeMagic(sr, **p); s = lib.retro_magic_create()
        assert lib.retro_magic_set(s, C.byref(Params(**p)))
        assert lib.retro_magic_prepare(s, sr)
        rng = random.Random(variation * 19 + sr + character)
        try:
         for block in range(64):
            if block in [8, 16, 24, 40]:
                if block == 8: p.update(amount=31, mix=61, dist_db=-2.5, pass_db=2)
                elif block == 16: p.update(low_hz=122, high_hz=7222, output_hz=933)
                elif block == 24: p.update(character=(character + 1) % 3, amount=82)
                else: p.update(character=character, amount=5)
                n.set(**p); assert lib.retro_magic_set(s, C.byref(Params(**p)))
            if block == 32:
                # Reset all numerical histories by fresh native synthetic state.
                del n; gc.collect(); n = NativeMagic(sr, **p)
                lib.retro_magic_reset(s)
            left = [f32(math.sin((block * 64 + i) * .071) * .7) for i in range(64)]
            right = [f32(math.cos((block * 64 + i) * .103) * .53) for i in range(64)]
            if block % 7 == 0:
                left = [f32(rng.uniform(-1.2, 1.2)) for _ in range(64)]
                right = [f32(rng.uniform(-.9, .9)) for _ in range(64)]
            if block in [14, 15, 60, 61, 62, 63]: left = right = [0.] * 64
            el, er = n.process(left, right)
            l = (C.c_float * 64)(*left); r = (C.c_float * 64)(*right)
            offset = 0
            for chunk in ([1, 7, 13, 3, 29, 11] if block % 2 else [64]):
                lib.retro_magic_process(s, C.cast(C.byref(l, offset * 4), C.POINTER(C.c_float)),
                                        C.cast(C.byref(r, offset * 4), C.POINTER(C.c_float)), chunk)
                offset += chunk
            for c, (expected, actual) in enumerate([(el, l), (er, r)]):
                for i, (a, b) in enumerate(zip(expected, actual)):
                    assert math.isfinite(b) and struct.pack('<f', a) == struct.pack('<f', b), (sr, character, variation, block, c, i, a, b)
                    samples += 1
        finally: lib.retro_magic_destroy(s)
        del n; gc.collect(); count += 1
    report = dict(reference_sha256=HASH, scope='Full numerical Magic stereo wrapper, Tube/Air/Iron; synthetic histories and caller-owned Air rings; fixed64frame cleanup',
                  scenarios=count, samples=samples, maximum_absolute_error=0, all_float_patterns_equal=True,
                  sample_rates=rates, initial_parameter_variations=grid, blocks_per_scenario=64, frames_per_native_call=64,
                  dynamic_controls=True, character_changes=True, reset_against_fresh_native=True,
                  irregular_partitions=[1, 7, 13, 3, 29, 11],
                  limitations=['No original constructor, full Dist block, oversampling, macro controls or plugin host', 'Linux libm bound to both C and reference', 'Native silence optimization disabled'])
    path = ROOT / args.report; path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report))


if __name__ == '__main__': main()
