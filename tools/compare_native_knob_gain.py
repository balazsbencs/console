#!/usr/bin/env python3
"""Compare Knob2AnyGain C primitive with original relation handler opcode 'K'."""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


def f(value):
    return C.c_float(value).value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=ROOT / 'build-release/libretro_dsp.so')
    parser.add_argument('--report', type=Path, default=ROOT / 'analysis/re/native-knob-gain-comparison.json')
    args = parser.parse_args()
    lib = C.CDLL(str(args.library.resolve()))
    port = lib.rs_relation_knob_to_gain
    port.argtypes = [C.c_float] * 5; port.restype = C.c_float
    native = NativeBitColor()
    handler = C.create_string_buffer(0x15030)
    store = C.create_string_buffer(8); vtable = C.create_string_buffer(0x40)
    current = [0.]; result = [0.]
    getter = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_int)(lambda *_: current[0])
    setter = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_float)(lambda _, key, value: result.__setitem__(0, value))
    struct.pack_into('<Q', store, 0, C.addressof(vtable))
    struct.pack_into('<Q', handler, 0x10, C.addressof(store))
    struct.pack_into('<Q', vtable, 0x38, C.cast(getter, C.c_void_p).value)
    struct.pack_into('<Q', vtable, 0x30, C.cast(setter, C.c_void_p).value)
    # Record index zero: input at +0, destination at +8; -1 avoids logging.
    off = 0x20
    struct.pack_into('<3i', handler, off, 100, -1, -1)
    struct.pack_into('<i', handler, off + 0x60, ord('K'))
    changed = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int)(native.base + 0xf0d70)
    comparisons = 0; worst = 0.; digest = hashlib.sha256(); distortion_anchors = []
    def compare(x, low, high, lower, upper):
        nonlocal comparisons, worst
        x, low, high, lower, upper = [f(v) for v in (x, low, high, lower, upper)]
        current[0] = x
        # Original serialization stores high dB, low dB, upper curve, lower curve.
        struct.pack_into('<4f', handler, off + 0xc, high, low, upper, lower)
        changed(handler, 0, 0)
        expected = result[0]; actual = port(x, low, high, lower, upper)
        assert math.isfinite(actual) and math.isfinite(expected)
        worst = max(worst, abs(actual - expected))
        bits = struct.pack('<f', actual)
        assert bits == struct.pack('<f', expected), ((x, low, high, lower, upper), actual, expected)
        digest.update(struct.pack('<5f', x, low, high, lower, upper) + bits)
        comparisons += 1
        return expected
    adjacent = [struct.unpack('<f', struct.pack('<I', word))[0] for word in [
        1, 0x3effffff, 0x3f000001, 0x3f7fffff]]
    inputs = sorted(set([f(i / 1000) for i in range(1001)] + adjacent))
    anchors = [(-3, 20), (3, -20), (-100, 100), (0, 0), (-24, 6), (18, -9), (.3, .7)]
    curves = [.001, .01, .15, .5, .85, .99, .999]
    for anchor in anchors:
        for lower in curves:
            for upper in curves:
                for x in inputs: compare(x, *anchor, lower, upper)
    # Opcode K extrapolates rather than clamping, unlike ordinary FromTo.
    for anchor in anchors:
        for x in [-1., -.25, 1.25, 2.]: compare(x, *anchor, .5, .5)
    rng = random.Random(0x4b)
    for _ in range(16384):
        compare(rng.random(), *(rng.uniform(-100, 100) for _ in range(2)),
                rng.uniform(.001, .999), rng.uniform(.001, .999))
    for x in [0, .25, .5, .75, 1]:
        pre = compare(x, -3, 20, .5, .5)
        post = compare(x, 3, -20, .5, .5)
        distortion_anchors.append(dict(input=x, pre_gain=pre, post_gain=post, product=pre*post))
    report = dict(reference_sha256=HASH, library=str(args.library.resolve()),
                  scope="original cRelationSpecHandler::objectChanged opcode K, synthetic model callbacks",
                  comparisons=comparisons, max_error=worst, all_float_patterns_equal=True,
                  result_sha256=digest.hexdigest(), distortion_mapping=distortion_anchors,
                  limitations=['Finite input and anchors; curvature strictly between zero and one',
                               'Extrapolation cases use .5 curves to avoid rational poles',
                               'Synthetic callbacks; original production scheduling not executed'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
