#!/usr/bin/env python3
"""Compare recovered scaling curves with isolated hash-pinned native functions."""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=ROOT / 'build-release/libretro_dsp.so')
    args = parser.parse_args()
    native = NativeBitColor()
    port = C.CDLL(str(args.library.resolve()))
    port.retro_scale_fit3.argtypes = [C.c_float] * 4
    port.retro_scale_fit3.restype = C.c_float
    port.retro_scale_log.argtypes = [C.c_float] * 3
    port.retro_scale_log.restype = C.c_float
    count = 0
    for low, high, middle, logarithmic in [(24, 1, 6, False),
            (44100, 44.1, 200, False), (20, 20000, 0, True), (1, 1000, 0, True)]:
        for i in range(1001):
            x = i / 1000
            expected = native.scale(x, low, high, middle, logarithmic)
            actual = port.retro_scale_log(x, low, high) if logarithmic else port.retro_scale_fit3(x, low, high, middle)
            assert struct.pack('<f', actual) == struct.pack('<f', expected), (x, low, high, actual, expected)
            count += 1
    report = dict(reference_sha256=HASH, cases=count, bit_exact=True, max_error=0,
                  scope='isolated Scale_Fit3 and Scale_Log functions with synthetic metadata')
    (ROOT / 'analysis/re/native-curves-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
