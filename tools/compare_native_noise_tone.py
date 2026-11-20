#!/usr/bin/env python3
"""Compare Noise Tone against original prepare/control/filter instruction slices.

Only numeric slices run, with synthetic cached wrappers and stereo buffers.
No constructor, resource stream, texture playback or full Noise process executes.
"""
import argparse
import ctypes as C
import json
import math
import mmap
from pathlib import Path
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


class Tone(C.Structure):
    _fields_ = [('rate', C.c_float), ('requested', C.c_float), ('smoothed', C.c_float),
                ('applied', C.c_float), ('max_step', C.c_float), ('smoothing', C.c_float),
                ('coefficient', C.c_float*5), ('history', (C.c_float*4)*2),
                ('correction', C.c_float), ('prior_correction', C.c_float),
                ('correction_step', C.c_float), ('remaining', C.c_uint),
                ('snap', C.c_bool), ('filtering', C.c_bool)]


class NativeTone(NativeBitColor):
    def __init__(self):
        super().__init__()
        for stub, name in [(0x578dda, 'sinf'), (0x5789e4, 'cosf')]:
            slot = stub+6+struct.unpack_from('<i', self.image, stub+2)[0]
            struct.pack_into('<Q', self.image, slot,
                             C.cast(getattr(self.libm, name), C.c_void_p).value)
        self.state = C.create_string_buffer(0xf00)
        self.audio = (C.c_float*128)()
        self.channels = (C.c_void_p*2)(C.addressof(self.audio), C.addressof(self.audio)+256)
        self.shim = mmap.mmap(-1, 1024, prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
        address = C.addressof(C.c_char.from_buffer(self.shim))
        prologue = bytes.fromhex('55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec 98 00 00 00 49 89 fd')
        epilogue = bytes.fromhex('48 81 c4 98 00 00 00 5b 41 5c 41 5d 41 5e 41 5f 5d c3')
        for offset, entry in [(0, 0x3a54e9), (64, 0x3a61ea)]:
            code = prologue+b'\x48\xb8'+struct.pack('<Q', self.base+entry)+b'\xff\xe0'
            self.shim[offset:offset+len(code)] = code
        self.shim[128:128+len(epilogue)] = epilogue
        prepare_return = bytes.fromhex('48 83 c4 10 5b 41 5e 5d c3')
        self.shim[256:256+len(prepare_return)] = prepare_return
        # Only exit boundaries are patched in this private numeric image.
        for exit_address, target in [(0x3a4530, address+256), (0x3a5600, address+128),
                                      (0x3a643a, address+128)]:
            self.image[exit_address:exit_address+12] = b'\x48\xb8'+struct.pack('<Q', target)+b'\xff\xe0'
        self.prepare_fn = C.CFUNCTYPE(None, C.c_void_p, C.c_float)(self.base+0x3a43a0)
        self.control_fn = C.CFUNCTYPE(None, C.c_void_p)(address)
        self.filter_fn = C.CFUNCTYPE(None, C.c_void_p)(address+64)

    def prepare(self, rate, tone):
        C.memset(self.state, 0, C.sizeof(self.state))
        struct.pack_into('<Q', self.state, 0xe40, C.addressof(self.channels))
        struct.pack_into('<i', self.state, 0xeb4, 2)
        struct.pack_into('<2f', self.state, 0xcb8, 1, 1)
        self.state[0x7f4] = b'\1'
        self.wrap(0x220, tone)
        self.prepare_fn(self.state, rate)
        self.wrap(0x220, tone)

    def set(self, tone):
        self.wrap(0x220, tone)

    def process(self, left, right):
        assert len(left) == len(right) == 64
        for i, value in enumerate(left+right):
            self.audio[i] = value
        self.control_fn(self.state)
        self.filter_fn(self.state)
        return list(self.audio[:64]), list(self.audio[64:])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--library', type=Path, default=ROOT/'build-release/libretro_dsp.so')
    ap.add_argument('--report', type=Path, default=ROOT/'analysis/re/native-noise-tone-comparison.json')
    args = ap.parse_args()
    if not args.library.is_file():
        ap.error(f'Library not found: {args.library}')
    if not args.report.parent.is_dir() or args.report.is_dir():
        ap.error(f'Invalid report path: {args.report}')
    lib = C.CDLL(str(args.library.resolve()))
    lib.rs_noise_tone_prepare.argtypes = [C.c_void_p, C.c_float]
    lib.rs_noise_tone_prepare.restype = C.c_bool
    lib.rs_noise_tone_set.argtypes = [C.c_void_p, C.c_float]
    lib.rs_noise_tone_set.restype = C.c_bool
    lib.rs_noise_tone_reset.argtypes = [C.c_void_p]
    lib.rs_noise_tone_process.argtypes = [C.c_void_p, C.POINTER(C.c_float),
                                        C.POINTER(C.c_float), C.c_size_t]
    native = NativeTone()
    counts = dict(scenarios=0, samples=0, state_bytes=0)
    rates = [1000, 22050, 44100, 44117, 48000, 48001, 96000, 192000, 1536000]
    tones = [0, .001, .25, .499999, .5, .500001, .75, .999, 1]
    changes = {3: 0, 7: .5, 17: 1, 31: .500001, 47: .25,
               79: .499999, 111: .75, 151: .001, 191: .999, 223: .5}
    for rate in rates:
        for tone in tones:
            for dynamic in [False, True]:
                state = C.create_string_buffer(C.sizeof(Tone))
                current = C.c_float(tone).value
                assert lib.rs_noise_tone_prepare(state, rate)
                assert lib.rs_noise_tone_set(state, current)
                native.prepare(rate, current)
                rng = random.Random(rate)
                for block in range(256):
                    if dynamic and block in changes:
                        current = C.c_float(changes[block]).value
                        assert lib.rs_noise_tone_set(state, current)
                        native.set(current)
                    if block == 128:
                        lib.rs_noise_tone_reset(state)
                        native.prepare(rate, current)
                    left = [C.c_float(math.sin((block*64+i)*.071)*.7).value for i in range(64)]
                    right = [C.c_float(rng.uniform(-1, 1)).value for i in range(64)]
                    if block % 17 in [3, 4]:
                        left = right = [0.]*64
                    expected = native.process(left, right)
                    l, r = (C.c_float*64)(*left), (C.c_float*64)(*right)
                    offset = 0
                    for chunk in ([1, 7, 13, 3, 29, 11] if block % 2 else [64]):
                        lib.rs_noise_tone_process(state,
                            C.cast(C.byref(l, offset*4), C.POINTER(C.c_float)),
                            C.cast(C.byref(r, offset*4), C.POINTER(C.c_float)), chunk)
                        offset += chunk
                    for channel, (a, b) in enumerate(zip(expected, [l, r])):
                        for index, (x, y) in enumerate(zip(a, b)):
                            assert math.isfinite(y) and struct.pack('<f', x) == struct.pack('<f', y), (
                                rate, tone, dynamic, block, channel, index, x, y)
                    snapshot = Tone.from_buffer(state)
                    native_state = native.state.raw
                    for name, position in [('rate', 0xe28), ('requested', 0x250),
                                           ('smoothed', 0x7e8), ('applied', 0x7f8),
                                           ('max_step', 0x7f0), ('smoothing', 0x7ec),
                                           ('correction', 0xcb8), ('prior_correction', 0xcbc)]:
                        actual = struct.pack('<f', getattr(snapshot, name))
                        assert actual == native_state[position:position+4], (
                            'state', rate, tone, dynamic, block, name, getattr(snapshot, name),
                            struct.unpack_from('<f', native.state, position)[0])
                        counts['state_bytes'] += 4
                    assert bytes(snapshot.coefficient) == native_state[0xc9c:0xcb0], (
                        'coefficients', rate, tone, dynamic, block)
                    assert bytes(snapshot.history) == native_state[0xc7c:0xc9c], (
                        'history', rate, tone, dynamic, block)
                    assert snapshot.snap == bool(native.state[0x7f4][0])
                    assert snapshot.filtering == bool(native.state[0xcc0][0])
                    counts['state_bytes'] += 54
                    counts['samples'] += 128
                counts['scenarios'] += 1
    report = dict(reference_sha256=HASH, **counts, max_error=0, state_bit_exact=True, sample_rates=rates,
                  tones=tones, irregular_partitions=[1, 7, 13, 3, 29, 11],
                  scope='Original numeric prepare prefix, Tone control/smoother and stereo filter slices',
                  limitations=['Synthetic original caches/buffers, no constructor or complete Noise process',
                               'Isolated Tone stage only, no texture synthesis or Follow/Stereo path',
                               'Reset uses fresh numerical Tone state; shared Linux libm'])
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
