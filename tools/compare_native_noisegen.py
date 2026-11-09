#!/usr/bin/env python3
"""Compare isolated original NoiseGen primitives with their C port.

Uses the same hash-pinned synthetic-image experiment as native_bitcolor_probe;
executes no plugin entrypoints, constructors or resource scripts.
Build libretro_dsp.so with CMake before running this comparison.
"""
import argparse
import ctypes as C
import itertools
import json
import math
from pathlib import Path
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path,
                        default=ROOT / 'build-release/libretro_dsp.so')
    args = parser.parse_args()
    image = NativeBitColor()
    stub = 0x578c96  # double pow, omitted/mistyped arguments in decompilation
    slot = stub + 6 + struct.unpack_from('<i', image.image, stub + 2)[0]
    struct.pack_into('<Q', image.image, slot, C.cast(image.libm.pow, C.c_void_p).value)
    reset_type = C.CFUNCTYPE(None, C.c_void_p, C.c_int32)
    params_type = C.CFUNCTYPE(None, C.c_void_p, C.c_float, C.c_float,
                             C.c_float, C.c_float, C.c_bool, C.c_float)
    value_type = C.CFUNCTYPE(C.c_float, C.c_void_p)
    native_reset = reset_type(image.base + 0x1a5980)
    native_params = params_type(image.base + 0x1a59b0)
    native_value = value_type(image.base + 0x1a5c80)
    port = C.CDLL(str(args.library.resolve()))
    port.rs_noisegen_reset.argtypes = [C.c_void_p, C.c_int32]
    port.rs_noisegen_set_params.argtypes = params_type._argtypes_
    port.rs_noisegen_get_value.argtypes = [C.c_void_p]
    port.rs_noisegen_get_value.restype = C.c_float
    rng = random.Random(920)
    cases = list(itertools.product([False, True], [0, 1, -1, 2147483647],
                                  [44100., 48000., 96000.],
                                  [0., .1, .5, .99, 1.],
                                  [0., .5, .94847450187182, 1.],
                                  [0., .3, .99, 1.]))
    cases.extend((bool(rng.randrange(2)), rng.randrange(-2147483648, 2147483648),
                  rng.choice([44100., 48000., 96000., 192000.]),
                  rng.random(), rng.random(), rng.random()) for _ in range(256))
    rng.shuffle(cases)
    max_error = 0.0
    samples = 0
    for case, (bipolar, seed, rate, chaos, smooth, decay) in enumerate(cases):
        a, b = C.create_string_buffer(40), C.create_string_buffer(40)
        native_reset(a, seed); port.rs_noisegen_reset(b, seed)
        assert a.raw == b.raw, ('reset', case)
        for gain in [.0, .2, 1.]:
            args = (gain, chaos, smooth, decay, bipolar, rate)
            native_params(a, *args); port.rs_noisegen_set_params(b, *args)
            if a.raw != b.raw:
                af = struct.unpack('<10I', a.raw); bf = struct.unpack('<10I', b.raw)
                raise AssertionError(('params', case, args, af, bf))
            for step in range(64):
                x, y = native_value(a), port.rs_noisegen_get_value(b)
                assert math.isfinite(x) and math.isfinite(y), ('nonfinite', case, args)
                error = abs(x - y)
                max_error = max(max_error, error)
                samples += 1
                if struct.pack('<f', x) != struct.pack('<f', y) or a.raw != b.raw:
                    raise AssertionError(('value/state', case, step, args, x, y,
                                          struct.unpack('<10I', a.raw), struct.unpack('<10I', b.raw)))
        # Reset must clear dynamic state while preserving all parameter bytes.
        original_parameters = a.raw[20:]
        native_reset(a, seed + 1); port.rs_noisegen_reset(b, seed + 1)
        assert a.raw == b.raw and a.raw[20:] == original_parameters
    result = dict(scope='isolated NoiseGen Reset/SetParams/GetValue with synthetic state',
                  reference_sha256=HASH, cases=len(cases), parameter_updates=len(cases)*3,
                  samples=samples, max_error=max_error, bit_exact_state=True,
                  caveat='system libm; not full-plugin Flux scheduling or audio validation')
    path = ROOT / 'analysis/re/native-noisegen-comparison.json'
    path.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))

if __name__ == '__main__':
    main()
