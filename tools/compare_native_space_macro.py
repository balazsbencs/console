#!/usr/bin/env python3
"""Compare settled Space macro/Flux relations, original Drive and stereo kernel.

Synthetic one-record handler with explicit seeds and settled64frame controls;
no original constructor, resource scheduler or production seed identity.
"""
import argparse
import ctypes as C
import json
import math
from pathlib import Path
import random
import struct
from compare_native_space import Params
from native_space_probe import NativeSpace, DEFAULTS, ROOT, HASH


def f(value):
    return C.c_float(value).value


def exact(actual, expected, context):
    assert struct.pack('<f', actual) == struct.pack('<f', expected), (context, actual, expected)


class Macro(C.Structure):
    _fields_ = [('generator', C.c_ubyte * 40), ('output', C.c_float)]


class NativeMacro:
    """One original unipolar Noise record and separate scratch relation record."""
    def __init__(self, image, seed):
        self.image = image  # Keep the reference code and its import bindings alive.
        stub = 0x578c96  # NoiseGen's double-precision pow import.
        slot = stub + 6 + struct.unpack_from('<i', image.image, stub + 2)[0]
        struct.pack_into('<Q', image.image, slot, C.cast(image.libm.pow, C.c_void_p).value)
        self.handler = C.create_string_buffer(0x15030)
        self.store = C.create_string_buffer(8)
        self.vtable = C.create_string_buffer(0x40)
        self.values = {}
        self.getter = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_int)(
            lambda _, key: self.values[key])
        self.setter = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_float)(
            lambda _, key, value: self.values.__setitem__(key, value))
        struct.pack_into('<Q', self.store, 0, C.addressof(self.vtable))
        struct.pack_into('<Q', self.handler, 0x10, C.addressof(self.store))
        for offset, callback in [(0x38, self.getter), (0x30, self.setter)]:
            struct.pack_into('<Q', self.vtable, offset, C.cast(callback, C.c_void_p).value)
        self.changed = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int)(image.base + 0xf0d70)
        self.drive = C.CFUNCTYPE(None, C.c_void_p, C.c_float)(image.base + 0xd6fd0)
        self.reset_fn = C.CFUNCTYPE(None, C.c_void_p, C.c_int32)(image.base + 0x1a5980)
        struct.pack_into('<6i', self.handler, 0x20, 10, 11, 14, 13, 12, -1)
        struct.pack_into('<i', self.handler, 0x80, ord('N'))
        struct.pack_into('<Q', self.handler, 0x15020, C.addressof(self.handler) + 0x20)
        self.reset(seed)

    def reset(self, seed):
        struct.pack_into('<i', self.handler, 0x40, seed)
        struct.pack_into('<f', self.handler, 0x44, -1.)
        self.reset_fn(C.addressof(self.handler) + 0x48, seed)

    def relation(self, kind, a, b=0., c=0., params=()):
        off = 0x90
        self.values.update({100: a, 101: b, 102: c})
        struct.pack_into('<4i', self.handler, off, 100, 101, -1, 102)
        if params:
            struct.pack_into('<' + 'f' * len(params), self.handler,
                             off + (0x10 if kind == 'M' else 0xc), *params)
        struct.pack_into('<i', self.handler, off + 0x60, ord(kind))
        self.changed(self.handler, 0, 1)
        return self.values[-1]

    def tick(self, rate):
        self.values.update({10: .5, 11: .5, 12: f(.42), 13: f(.59)})
        self.changed(self.handler, 0, 0)
        self.drive(self.handler, f(rate * f(.015625)))
        return self.values[14]

    def targets(self, magnitude, amount, flux):
        return dict(level=self.relation('m', magnitude, amount),
                    pre_wobble_amount=self.relation('M', magnitude, flux, amount,
                                                   (.5, .5, .99)),
                    pre_wobble_noise=self.relation('b', 0, self.values[14], .75))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--library', type=Path, default=ROOT / 'build-release/libretro_dsp.so')
    ap.add_argument('--report', type=Path,
                    default=ROOT / 'analysis/re/native-space-macro-comparison.json')
    ap.add_argument('--static-ticks', type=int, default=256)
    ap.add_argument('--dynamic-ticks', type=int, default=1024)
    args = ap.parse_args()
    assert args.static_ticks > 0 and args.dynamic_ticks > 0
    lib = C.CDLL(str(args.library.resolve()))
    lib.retro_space_create.restype = C.c_void_p
    for name, rest in [('set', [C.POINTER(Params)]), ('prepare', [C.c_float]),
                       ('reset', []), ('destroy', []),
                       ('process', [C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_size_t])]:
        getattr(lib, 'retro_space_' + name).argtypes = [C.c_void_p] + rest
    lib.rs_space_macro_reset.argtypes = [C.POINTER(Macro), C.c_int32]
    lib.rs_space_macro_tick.argtypes = [C.POINTER(Macro), C.c_float]
    lib.rs_space_macro_apply.argtypes = [C.POINTER(Macro)] + [C.c_float] * 3 + [C.POINTER(Params)]
    counts = dict(scenarios=0, ticks=0, noise_outputs=0, noise_state_bytes=0,
                  targets=0, preserved_controls=0, audio_samples=0)
    cases = [('flux_zero', (1, 1, 0)), ('amount_zero', (1, 0, 1)),
             ('magnitude_zero', (0, 1, 1)), ('full', (1, 1, 1)),
             ('partial', (.6, .7, .8)), ('small_amount', (1, .001, 1)),
             ('changing', None)]
    for rate in [44100., 48000., 96000.]:
      for initial_seed in [0, -2147483648]:
       for label, controls in cases:
        base = dict(DEFAULTS, level=1., dampen_hz=100., soften_hz=16000.,
                    pre_delay_ms=0., dual_mono=1., stereo_detune=f(.3918), feedbacks=0.)
        native = NativeSpace(rate, **base)
        model = NativeMacro(native, initial_seed)
        macro = Macro()
        lib.rs_space_macro_reset(C.byref(macro), initial_seed)
        core = lib.retro_space_create()
        params = Params(**base)
        assert core and lib.retro_space_set(core, C.byref(params))
        assert lib.retro_space_prepare(core, rate)
        rng = random.Random(int(rate) + initial_seed)
        ticks = args.dynamic_ticks if controls is None else args.static_ticks
        seed = initial_seed
        output_range = [math.inf, -math.inf]
        try:
          for tick in range(ticks):
            if controls is None:
                if tick % 31 == 0:
                    current = [f(rng.random()) for _ in range(3)]
                    if tick % 93 == 0: current[2] = 0.
                    if tick % 155 == 0: current[1] = 0.
                    if tick % 217 == 0: current[0] = 0.
                # Enable does not stop the external free-running generator.
                base['enable'] = float((tick // 19) % 2)
                base['stereo_mode'] = float((tick // 53) % 2)
                base['pre_delay_ms'] = f((tick % 101) * .5)
                if tick and tick % 127 == 0:
                    seed = rng.randint(-2147483648, 2147483647)
                    model.reset(seed)
                    lib.rs_space_macro_reset(C.byref(macro), seed)
            else:
                current = [f(v) for v in controls]
            if tick == ticks // 2:
                native.close()
                native = NativeSpace(rate, **base)
                # The relation fixture retains its own image and allocated callbacks.
                model.reset(seed)
                lib.rs_space_macro_reset(C.byref(macro), seed)
                reset_params = Params(**base)
                assert lib.retro_space_set(core, C.byref(reset_params))
                lib.retro_space_reset(core)
            expected_output = model.tick(rate)
            lib.rs_space_macro_tick(C.byref(macro), rate)
            exact(macro.output, expected_output, (rate, label, tick, 'noise'))
            assert bytes(macro.generator) == model.handler.raw[0x48:0x70], (
                rate, label, tick, 'noise-state')
            output_range[0] = min(output_range[0], macro.output)
            output_range[1] = max(output_range[1], macro.output)
            targets = model.targets(*current)
            params = Params(**base)
            before = bytes(params)
            lib.rs_space_macro_apply(C.byref(macro), *current, C.byref(params))
            for name, expected in targets.items():
                exact(getattr(params, name), expected, (rate, label, tick, name))
            for index, (name, _) in enumerate(Params._fields_):
                if name not in targets:
                    assert bytes(params)[index * 4:index * 4 + 4] == before[index * 4:index * 4 + 4]
                    counts['preserved_controls'] += 1
            native.set(**dict(base, **targets))
            assert lib.retro_space_set(core, C.byref(params))
            left = [f(math.sin((tick * 64 + i) * .071) * .7) for i in range(64)]
            right = [f(rng.uniform(-1, 1)) for _ in range(64)]
            if tick % 17 in [3, 4]: left = right = [0.] * 64
            expected = native.process(left, right)
            l, r = (C.c_float * 64)(*left), (C.c_float * 64)(*right)
            offset = 0
            for chunk in ([1, 7, 13, 3, 29, 11] if tick % 2 else [64]):
                lib.retro_space_process(core,
                    C.cast(C.byref(l, offset * 4), C.POINTER(C.c_float)),
                    C.cast(C.byref(r, offset * 4), C.POINTER(C.c_float)), chunk)
                offset += chunk
            for channel, (a, b) in enumerate(zip(expected, [l, r])):
                for index, (x, y) in enumerate(zip(a, b)):
                    exact(y, x, (rate, label, tick, channel, index))
            counts['ticks'] += 1
            counts['noise_outputs'] += 1
            counts['noise_state_bytes'] += 40
            counts['targets'] += 3
            counts['audio_samples'] += 128
        finally:
            native.close()
            lib.retro_space_destroy(core)
        counts['scenarios'] += 1
        print(rate, initial_seed, label, output_range, flush=True)
    report = dict(reference_sha256=HASH, **counts, control_max_error=0., audio_max_error=0.,
                  sample_rates=[44100, 48000, 96000], static_ticks=args.static_ticks,
                  dynamic_ticks=args.dynamic_ticks, irregular_partitions=[1, 7, 13, 3, 29, 11],
                  limitations=['One synthetic free-running unipolar Noise record with explicit seeds',
                               'Settled parameter batches; original full model scheduler not executed',
                               'Production Space serialized relation index remains unresolved',
                               'Active input marker held at 1 and meter feedback disabled on both sides; Linux libm'])
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
