#!/usr/bin/env python3
"""Compare isolated TubePair setup/process with hash-pinned native numerics.

No plugin constructor or host runs. Native state consists of five first-order
filter records and two gains. exp/atan imports bind to the system double libm.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


class Tube(C.Structure):
    _fields_ = [('state', C.c_float * 37)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=ROOT / 'build-release/libretro_dsp.so')
    parser.add_argument('--report', type=Path, default=ROOT / 'analysis/re/native-tube-pair-comparison.json')
    args = parser.parse_args()
    lib = C.CDLL(str(args.library.resolve()))
    lib.rs_tube_pair_setup.argtypes = [C.POINTER(Tube), C.c_float, C.c_float]
    lib.rs_tube_pair_setup.restype = None
    lib.rs_tube_pair_reset.argtypes = [C.POINTER(Tube)]; lib.rs_tube_pair_reset.restype = None
    lib.rs_tube_pair_process.argtypes = [C.POINTER(Tube), C.c_float, C.c_uint]
    lib.rs_tube_pair_process.restype = C.c_float
    image = NativeBitColor()
    # Verified through Mach-O indirect symbol records: 578a2c=_exp, 57895a=_atan.
    for stub, name in [(0x578a2c, 'exp'), (0x57895a, 'atan')]:
        assert image.image[stub:stub + 2] == b'\xff\x25'
        slot = stub + 6 + struct.unpack_from('<i', image.image, stub + 2)[0]
        struct.pack_into('<Q', image.image, slot, C.cast(getattr(image.libm, name), C.c_void_p).value)
    setup = C.CFUNCTYPE(None, C.c_void_p, C.c_float, C.c_float)(image.base + 0xb1ab0)
    process = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_float, C.c_int)(image.base + 0xb2550)
    f = lambda value: C.c_float(value).value
    counts = dict(scenarios=0, setups=0, resets=0, samples=0, state_comparisons=0)
    worst = 0.; digest = hashlib.sha256()
    def same_state(original, port, context):
        a, b = bytes(original), bytes(port)
        assert a == b, (context, [(i, struct.unpack_from('<f', a, i * 4)[0],
                                  struct.unpack_from('<f', b, i * 4)[0])
                                 for i in range(37) if a[i*4:i*4+4] != b[i*4:i*4+4]])
        counts['state_comparisons'] += 1
    rates = [1000., 22050., 44100., 48000., 96000., 192000., 384000., 1536000.]
    amounts = [0., .0001, .01, .05, .2, .5, .8, .99, 1.]
    for rate in rates:
      for amount in amounts + [None]:
        original, port = Tube(), Tube()
        current = f(.5 if amount is None else amount)
        setup(C.byref(original), rate, current); lib.rs_tube_pair_setup(C.byref(port), rate, current)
        counts['setups'] += 1; same_state(original, port, ('setup', rate, amount))
        rng = random.Random(int(rate) + int(current * 100000))
        for index in range(8192 if amount is None else 2048):
            if amount is None and index % 191 == 17:
                current = f(rng.random())
                setup(C.byref(original), rate, current); lib.rs_tube_pair_setup(C.byref(port), rate, current)
                counts['setups'] += 1; same_state(original, port, ('dynamic setup', rate, index))
            if index in [1023, 3500]:
                # State-level reset contract: preserve coefficient/gain words,
                # clear only each filter's two input and two output histories.
                for base in range(0, 35, 7):
                    for j in range(3, 7): original.state[base + j] = 0.
                lib.rs_tube_pair_reset(C.byref(port)); counts['resets'] += 1
                same_state(original, port, ('reset', rate, index))
            for channel in [0, 1]:
                mode = (index // 128) % 8
                if mode == 0: sample = 0. if index % 128 else (1. if channel == 0 else -1.)
                elif mode == 1: sample = (-1. if channel else 1.) * .7
                elif mode == 2: sample = math.sin(index * (.071 if channel == 0 else .093)) * .8
                elif mode == 3: sample = rng.uniform(-8, 8)
                elif mode == 4: sample = (-1. if index & 1 else 1.) * 100.
                elif mode == 5: sample = -0. if channel else 0.
                elif mode == 6: sample = rng.uniform(-1, 1) * 1e-25
                else: sample = math.cos(index * .113) * 2.
                sample = f(sample)
                expected = process(C.byref(original), sample, channel)
                actual = lib.rs_tube_pair_process(C.byref(port), sample, channel)
                assert math.isfinite(expected) and math.isfinite(actual)
                worst = max(worst, abs(actual - expected))
                assert struct.pack('<f', actual) == struct.pack('<f', expected), (
                    'sample', rate, current, index, channel, sample, expected, actual)
                digest.update(struct.pack('<f', actual)); counts['samples'] += 1
            same_state(original, port, ('history', rate, current, index))
        counts['scenarios'] += 1
    # Dense additional setup-only grid covers coefficient conversions separately.
    for rate in rates:
        original, port = Tube(), Tube()
        for i in range(1001):
            amount = f(i / 1000)
            setup(C.byref(original), rate, amount); lib.rs_tube_pair_setup(C.byref(port), rate, amount)
            counts['setups'] += 1; same_state(original, port, ('dense setup', rate, amount))
    result = dict(reference_sha256=HASH, library=str(args.library.resolve()),
                  scope='isolated cAEMagicDistortion::tubePair setup/process; synthetic 148-byte state',
                  counts=counts, max_error=worst, bit_exact_coefficients_histories_outputs=True,
                  output_sha256=digest.hexdigest(), limitations=[
                      'No original factory constructor, full plugin, focus filters, gains, Mix or Flux graph',
                      'Native double exp/atan imports bound to system libm',
                      'Reset checks explicit history-clearing contract, not a separately invoked native reset routine'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n'); print(json.dumps(result))


if __name__ == '__main__':
    main()
