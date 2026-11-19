#!/usr/bin/env python3
"""Compare the Noise sample reader's cubic arithmetic using preloaded PCM."""
import argparse
import ctypes as C
import json
import random
import struct
from pathlib import Path

from native_bitcolor_probe import NativeBitColor, ROOT, HASH


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=ROOT / 'build-release/libretro_dsp.so')
    parser.add_argument('--report', type=Path, default=ROOT / 'analysis/re/native-noise-interp.json')
    args = parser.parse_args()
    image = NativeBitColor()
    lib = C.CDLL(str(args.library.resolve()))
    interp = lib.rs_noise_interpolate
    interp.argtypes = [C.c_float] * 5
    interp.restype = C.c_float
    float_p = C.POINTER(C.c_float)
    read = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, float_p, float_p)(image.base + 0x39f1b0)

    rng = random.Random(390)
    samples = 0
    for step in (0.25, .5, 44100/48000, 48000/44100, 1, 1.75, 3.5):
        for mono in (False, True):
            for _ in range(64):
                state = C.create_string_buffer(0x458)
                struct.pack_into('<Q', state, 0x10, C.addressof(state))
                struct.pack_into('<d', state, 0x30, step)
                state[0x38] = b'\x01'
                struct.pack_into('<I', state, 0x48, 128)
                phase = rng.uniform(1, 8)
                struct.pack_into('<d', state, 0x450, phase)
                pcm = [(C.c_float * 128)(*(rng.uniform(-1, 1) for _ in range(128))) for _ in range(2)]
                for offset, data in zip((0x4c, 0x24c), pcm):
                    C.memmove(C.addressof(state)+offset, data, C.sizeof(data))
                frames = min(64, int((124-phase)/step))
                left, right = (C.c_float * frames)(), (C.c_float * frames)()
                assert read(state, frames, left, None if mono else right) == frames
                for i in range(frames):
                    center = int(phase)
                    fraction = phase-center
                    for data, result in zip(pcm, (left,) if mono else (left, right)):
                        expected = interp(*data[center-1:center+3], fraction)
                        assert struct.pack('<f', expected) == struct.pack('<f', result[i]), (step, mono, i, expected, result[i])
                        samples += 1
                    phase += step
                assert struct.unpack_from('<d', state, 0x450)[0] == phase
    result = dict(reference_sha256=HASH, samples=samples, bit_exact=True,
                  scope='Original interpolated mono/stereo branch with preloaded float PCM',
                  exclusions=['Source refill and exact-rate copy shortcuts', 'NXAC decoding and looping'])
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
