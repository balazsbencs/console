#!/usr/bin/env python3
"""Original stereo Noise Follow envelope slice versus C, using settled defaults.

The original numeric control/filter/envelope instructions execute against
synthetic cached metadata, stereo input and scratch output. No texture playback
or complete original Noise process runs. Zero Follow with HostIsPlaying true
checks native state reset and the C adapter's explicit unity-output contract.
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


class Follow(C.Structure):
    _fields_ = [('rate', C.c_float), ('requested', C.c_float), ('coefficient', C.c_float*5),
                ('history', C.c_float*4), ('envelope', C.c_float), ('peak', C.c_float),
                ('attack', C.c_float), ('release', C.c_float), ('ceiling', C.c_float),
                ('detector_scale', C.c_float), ('offset', C.c_float), ('recenter', C.c_float),
                ('maximum', C.c_float), ('remaining', C.c_uint),
                ('playing', C.c_bool), ('bypass', C.c_bool)]


class NativeFollow(NativeBitColor):
    def __init__(self):
        super().__init__()
        for stub, name in [(0x578dda, 'sinf'), (0x5789e4, 'cosf')]:
            slot = stub+6+struct.unpack_from('<i', self.image, stub+2)[0]
            struct.pack_into('<Q', self.image, slot,
                             C.cast(getattr(self.libm, name), C.c_void_p).value)
        self.state = C.create_string_buffer(0xf00)
        self.audio = (C.c_float*128)()
        self.channels = (C.c_void_p*2)(C.addressof(self.audio), C.addressof(self.audio)+256)
        self.gains = (C.c_float*64)()
        self.output = (C.c_void_p*1)(C.addressof(self.gains))
        self.model = C.create_string_buffer(8)
        self.vtable = C.create_string_buffer(0x40)
        self.playing = True
        self.getter = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_int)(lambda *_: float(self.playing))
        struct.pack_into('<Q', self.model, 0, C.addressof(self.vtable))
        struct.pack_into('<Q', self.vtable, 0x38, C.cast(self.getter, C.c_void_p).value)
        self.shim = mmap.mmap(-1, 512, prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC)
        address = C.addressof(C.c_char.from_buffer(self.shim))
        prologue = bytes.fromhex('55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec 98 00 00 00 49 89 fd')
        entry = prologue+b'\x48\xb8'+struct.pack('<Q', self.base+0x3a5775)+b'\xff\xe0'
        self.shim[:len(entry)] = entry
        epilogue = bytes.fromhex('48 81 c4 98 00 00 00 5b 41 5c 41 5d 41 5e 41 5f 5d c3')
        self.shim[128:128+len(epilogue)] = epilogue
        self.image[0x3a5ff4:0x3a6000] = b'\x48\xb8'+struct.pack('<Q', address+128)+b'\xff\xe0'
        self.process_fn = C.CFUNCTYPE(None, C.c_void_p)(address)

    def prepare(self, rate, follow, playing):
        C.memset(self.state, 0, C.sizeof(self.state))
        struct.pack_into('<Q', self.state, 0xe40, C.addressof(self.channels))
        struct.pack_into('<Q', self.state, 0xe10, C.addressof(self.output))
        struct.pack_into('<Q', self.state, 0x7b0, C.addressof(self.model))
        struct.pack_into('<i', self.state, 0xeb4, 2)
        struct.pack_into('<f', self.state, 0xe28, rate)
        for offset, raw, physical in [(0x298, follow, follow), (0x338, .5, .5),
                (0x658, .94, .94), (0x3d8, 0, 3.000000238418579),
                (0x478, 0, .009999999776482582), (0x518, 0, 2.9970028400421143),
                (0x5b8, 0, .015235929749906063), (0x6f8, 0, 79.99999237060547)]:
            self.param(offset, raw, physical)
        self.set(follow, playing)
        self.filter_initialized = False

    def set(self, follow, playing):
        self.param(0x298, follow, follow)
        self.playing = bool(playing)

    def process(self, left, right):
        assert len(left) == len(right) == 64
        for i, value in enumerate(left+right):
            self.audio[i] = value
        for i in range(64):
            self.gains[i] = 123.0
        self.process_fn(self.state)
        bypass = struct.unpack_from('<f', self.state, 0x330)[0] == 0 and self.playing
        if bypass:
            assert all(value == 123.0 for value in self.gains)
            return [1.]*64
        self.filter_initialized = True
        return list(self.gains)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--library', type=Path, default=ROOT/'build-release/libretro_dsp.so')
    ap.add_argument('--report', type=Path, default=ROOT/'analysis/re/native-noise-follow-comparison.json')
    args = ap.parse_args()
    if not args.library.is_file():
        ap.error(f'Library not found: {args.library}')
    if not args.report.parent.is_dir() or args.report.is_dir():
        ap.error(f'Invalid report path: {args.report}')
    lib = C.CDLL(str(args.library.resolve()))
    lib.rs_noise_follow_prepare.argtypes = [C.c_void_p, C.c_float]
    lib.rs_noise_follow_prepare.restype = C.c_bool
    lib.rs_noise_follow_set.argtypes = [C.c_void_p, C.c_float]
    lib.rs_noise_follow_set.restype = C.c_bool
    lib.rs_noise_follow_set_playing.argtypes = [C.c_void_p, C.c_bool]
    lib.rs_noise_follow_reset.argtypes = [C.c_void_p]
    lib.rs_noise_follow_process.argtypes = [C.c_void_p, C.POINTER(C.c_float),
                                          C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_size_t]
    native = NativeFollow()
    rates = [1000, 22050, 44100, 44117, 48000, 48001, 96000, 192000, 1536000]
    follows = [0, .001, .1, .49, .5, .51, .9, 1]
    changes = {3: 0, 7: .5, 17: 1, 31: .001, 47: .25, 79: .49,
               111: .51, 151: .9, 191: 0, 223: 1}
    counts = dict(scenarios=0, gain_samples=0, state_bytes=0)
    for rate in rates:
        for follow in follows:
            for dynamic in [False, True]:
                state = C.create_string_buffer(C.sizeof(Follow))
                current, playing = C.c_float(follow).value, True
                assert lib.rs_noise_follow_prepare(state, rate)
                assert lib.rs_noise_follow_set(state, current)
                lib.rs_noise_follow_set_playing(state, playing)
                native.prepare(rate, current, playing)
                rng = random.Random(rate)
                for block in range(256):
                    if dynamic and block in changes:
                        current = C.c_float(changes[block]).value
                        assert lib.rs_noise_follow_set(state, current)
                    if dynamic:
                        playing = (block//13)%2 == 0
                        lib.rs_noise_follow_set_playing(state, playing)
                    native.set(current, playing)
                    if block == 128:
                        lib.rs_noise_follow_reset(state)
                        native.prepare(rate, current, playing)
                    scale = [0., .00001, .01, .3, 1., 4., 100.][block%7]
                    left = [C.c_float(math.sin((block*64+i)*.071)*scale).value for i in range(64)]
                    right = [C.c_float(rng.uniform(-scale, scale)).value for i in range(64)]
                    if block%11 == 0:
                        right = [-value for value in left]
                    elif block%11 == 1:
                        left = right = [C.c_float(scale).value]*64
                    expected = native.process(left, right)
                    l, r, output = (C.c_float*64)(*left), (C.c_float*64)(*right), (C.c_float*64)()
                    offset = 0
                    for chunk in ([1, 7, 13, 3, 29, 11] if block%2 else [64]):
                        pointer = lambda array: C.cast(C.byref(array, offset*4), C.POINTER(C.c_float))
                        lib.rs_noise_follow_process(state, pointer(l), pointer(r), pointer(output), chunk)
                        offset += chunk
                    for index, (x, y) in enumerate(zip(expected, output)):
                        assert math.isfinite(y) and struct.pack('<f', x) == struct.pack('<f', y), (
                            rate, follow, dynamic, block, index, x, y)
                    snapshot = Follow.from_buffer(state)
                    original = native.state.raw
                    for name, position in [('rate', 0xe28), ('requested', 0x330),
                                           ('envelope', 0xe00), ('peak', 0xe04)]:
                        assert struct.pack('<f', getattr(snapshot, name)) == original[position:position+4], (
                            'state', rate, follow, dynamic, block, name, getattr(snapshot, name),
                            struct.unpack_from('<f', original, position)[0])
                        counts['state_bytes'] += 4
                    assert bytes(snapshot.history) == original[0xdf0:0xe00], (
                        'history', rate, follow, dynamic, block)
                    counts['state_bytes'] += 16
                    if native.filter_initialized:
                        assert bytes(snapshot.coefficient) == original[0xdd4:0xde8], (
                            'coefficients', rate, follow, dynamic, block)
                        counts['state_bytes'] += 20
                    assert snapshot.playing == playing
                    assert snapshot.bypass == (current == 0 and playing)
                    counts['gain_samples'] += 64
                counts['scenarios'] += 1
    report = dict(reference_sha256=HASH, **counts, max_error=0, state_bit_exact=True, sample_rates=rates,
                  follows=follows, irregular_partitions=[1, 7, 13, 3, 29, 11],
                  scope='Original stereo Follow control/filter/envelope slice 3a5775..3a5ff3',
                  limitations=['Synthetic metadata caches, no constructor or complete Noise process',
                               'Zero Follow playing=true checks native bypass states and C unity contract',
                               'Stereo active input; settled resource defaults; shared Linux libm',
                               'Coefficient state compared after original first Setup; C prepares this cache eagerly'])
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
