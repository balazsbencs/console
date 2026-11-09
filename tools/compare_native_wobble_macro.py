#!/usr/bin/env python3
"""Compare settled Wobble macro/Flux relations, original Drive and stereo kernel.

Runs isolated functions from the hash-pinned x86_64 reference. Synthetic model
records use explicit seeds and manually settled notifications, not the complete
original plugin or production model scheduler.
"""
import argparse
import ctypes as C
import gc
import json
import math
from pathlib import Path
import random
import struct
from compare_native_wobble import Params, NAMES, library
from native_wobble_probe import NativeWobble
from native_bitcolor_probe import ROOT, HASH


def f(value):
    return C.c_float(value).value


class Macro(C.Structure):
    _fields_ = [('wow', C.c_ubyte * 40), ('flutter', C.c_ubyte * 40),
                ('wow_output', C.c_float), ('flutter_output', C.c_float)]


class NativeMacro:
    """Original handler arithmetic and Drive with two synthetic noise records."""
    def __init__(self, image, seeds):
        self.handler = C.create_string_buffer(0x15030)
        self.store = C.create_string_buffer(8)
        self.vtable = C.create_string_buffer(0x40)
        self.values = {}
        values = self.values
        self.getter = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_int)(lambda _, key: values[key])
        self.setter = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_float)(
            lambda _, key, value: values.__setitem__(key, value))
        struct.pack_into('<Q', self.store, 0, C.addressof(self.vtable))
        struct.pack_into('<Q', self.handler, 0x10, C.addressof(self.store))
        for offset, callback in [(0x38, self.getter), (0x30, self.setter)]:
            struct.pack_into('<Q', self.vtable, offset, C.cast(callback, C.c_void_p).value)
        self.changed = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int)(image.base + 0xf0d70)
        self.drive = C.CFUNCTYPE(None, C.c_void_p, C.c_float)(image.base + 0xd6fd0)
        reset = C.CFUNCTYPE(None, C.c_void_p, C.c_int32)(image.base + 0x1a5980)
        for index, seed in enumerate(seeds):
            off = 0x20 + index * 0x70
            struct.pack_into('<6i', self.handler, off, 10 + index * 10, 11 + index * 10,
                             14 + index * 10, 13 + index * 10, 12 + index * 10, -1)
            struct.pack_into('<i', self.handler, off + 0x20, seed)
            struct.pack_into('<f', self.handler, off + 0x24, -1.)
            struct.pack_into('<i', self.handler, off + 0x60, ord('O'))
            struct.pack_into('<Q', self.handler, off + 0x68,
                             C.addressof(self.handler) + 0x90 if index == 0 else 0)
            reset(C.addressof(self.handler) + off + 0x28, seed)
        struct.pack_into('<Q', self.handler, 0x15020, C.addressof(self.handler) + 0x20)

    def relation(self, kind, a, b=0., c=0., params=()):
        off = 0x100
        self.values.update({100: a, 101: b, 102: c})
        struct.pack_into('<4i', self.handler, off, 100, 101, -1, 102)
        if params:
            struct.pack_into('<' + 'f' * len(params), self.handler,
                             off + (0x10 if kind == 'M' else 0xc), *params)
        struct.pack_into('<i', self.handler, off + 0x60, ord(kind))
        self.changed(self.handler, 0, 2)
        return self.values[-1]

    def tick(self, sample_rate, magnitude, flux, amount):
        scaled = self.relation('M', magnitude, flux, amount, (.5, .5, .99))
        for index, gain, chaos, smooth, decay in [
                (0, .18918, .43783780, .90, .9054), (1, .2, .43783, .8324, .986486)]:
            base = 10 + index * 10
            self.values.update({base: self.relation('m', scaled, f(gain)),
                                base + 1: f(chaos), base + 2: f(smooth), base + 3: f(decay)})
            self.changed(self.handler, 0, index)
        self.drive(self.handler, f(sample_rate * f(.015625)))

    def targets(self, magnitude, flux, amount, balance, stereo):
        off = 0x100
        self.values[100] = balance
        struct.pack_into('<5i', self.handler, off, 100, -1, -1, 103, 104)
        struct.pack_into('<i', self.handler, off + 0x60, ord('X'))
        self.changed(self.handler, 0, 2)
        wow_weight, flutter_weight = self.values[103], self.values[104]
        amount_scaled = self.relation('m', magnitude, amount)
        wow = self.relation('m', wow_weight, amount_scaled)
        flutter = self.relation('b', self.relation('m', flutter_weight, amount_scaled), self.values[24], .95)
        noise = self.relation('b', 0., self.values[14], .85)
        scaled = self.relation('M', magnitude, flux, amount, (.5, .5, .99))
        noise_scale = self.relation('m', scaled, wow_weight)
        phase = self.relation('f', stereo, params=(0., 1., 0., .4, .5))
        return dict(wow_depth=wow, flutter_depth=flutter, lfo_noise=noise,
                    noise_scale=noise_scale, stereo_phase=phase)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=ROOT / 'build-release/libretro_dsp.so')
    parser.add_argument('--report', type=Path, default=ROOT / 'analysis/re/native-wobble-macro-comparison.json')
    parser.add_argument('--static-ticks', type=int, default=1024)
    parser.add_argument('--dynamic-ticks', type=int, default=4096)
    args = parser.parse_args()
    assert args.static_ticks > 0 and args.dynamic_ticks > 0
    lib = library(args.library)
    lib.rs_wobble_macro_reset.argtypes = [C.POINTER(Macro), C.c_int32, C.c_int32]
    lib.rs_wobble_macro_tick.argtypes = [C.POINTER(Macro)] + [C.c_float] * 4
    lib.rs_wobble_macro_apply.argtypes = [C.POINTER(Macro)] + [C.c_float] * 5 + [C.POINTER(Params)]
    counts = dict(scenarios=0, ticks=0, noise_outputs=0, noise_state_bytes=0, targets=0, audio_samples=0)
    control_error = audio_error = 0.
    def exact(actual, expected, context):
        nonlocal control_error
        control_error = max(control_error, abs(actual - expected))
        assert struct.pack('<f', actual) == struct.pack('<f', expected), (context, actual, expected)
    # magnitude, flux, amount, wow/flutter balance, stereo toggle
    cases = [
        ('flux_zero', (1, 0, 1, .5, 0)), ('amount_zero', (1, 1, 0, .5, 1)),
        ('magnitude_zero', (0, 1, 1, .5, 1)), ('wow_only', (1, 1, 1, 0, 0)),
        ('flutter_only', (1, 1, 1, 1, 1)), ('full', (1, 1, 1, .5, 1)),
        ('partial', (.6, .8, .7, .2, 1)), ('small_amount', (1, 1, .001, .7, 0)),
        ('changing', None),
    ]
    results = []
    for sample_rate in [44100., 48000., 96000.]:
      for seeds in [(0, 1), (-1, 2147483647)]:
       for label, controls in cases:
        p = lib.retro_wobble_defaults()
        native = NativeWobble(sample_rate=sample_rate, normalized=1,
                              **{v: getattr(p, k) for k, v in NAMES.items()})
        model = NativeMacro(native, seeds)
        macro = Macro(); lib.rs_wobble_macro_reset(C.byref(macro), *seeds)
        core = lib.retro_wobble_create()
        assert core and lib.retro_wobble_set(core, C.byref(p)) and lib.retro_wobble_prepare(core, sample_rate)
        rng = random.Random(int(sample_rate) + seeds[0])
        ranges = [[math.inf, -math.inf], [math.inf, -math.inf]]
        ticks = args.dynamic_ticks if controls is None else args.static_ticks
        try:
          for tick in range(ticks):
            if controls is None:
                # Keep long plateaus as well as abrupt changes to expose generator memory.
                if tick % 31 == 0:
                    current = [f(rng.random()) for _ in range(4)] + [float(rng.randrange(2))]
                    if tick % 93 == 0: current[1] = 0.
                    if tick % 155 == 0: current[2] = 0.
                    if tick % 217 == 0: current[0] = 0.
                    if tick % 341 == 0: current = [1., 1., 1., float((tick // 341) % 2), 1.]
            else:
                current = [f(v) for v in controls]
            magnitude, flux, amount, balance, stereo = current
            model.tick(sample_rate, magnitude, flux, amount)
            lib.rs_wobble_macro_tick(C.byref(macro), sample_rate, magnitude, flux, amount)
            for i, output, state in [(0, macro.wow_output, macro.wow), (1, macro.flutter_output, macro.flutter)]:
                exact(output, model.values[14 + i * 10], (label, sample_rate, seeds, tick, 'noise', i))
                offset = 0x20 + i * 0x70 + 0x28
                assert bytes(state) == model.handler.raw[offset:offset + 40], (label, tick, 'noise state', i)
                ranges[i][0] = min(ranges[i][0], output); ranges[i][1] = max(ranges[i][1], output)
            target = model.targets(magnitude, flux, amount, balance, stereo)
            # Applying macros must leave the direct rate/mix/enable controls intact.
            untouched = (p.wow_rate, p.flutter_rate, p.mix, p.enable)
            lib.rs_wobble_macro_apply(C.byref(macro), magnitude, flux, amount, balance, stereo, C.byref(p))
            assert untouched == (p.wow_rate, p.flutter_rate, p.mix, p.enable)
            for name, value in target.items(): exact(getattr(p, name), value, (label, tick, name))
            native.set(**{NAMES[k]: v for k, v in target.items()})
            assert lib.retro_wobble_set(core, C.byref(p))
            left = [f(.6 * math.sin((tick * 64 + i) * .071)) for i in range(64)]
            right = [f(.4 * math.cos((tick * 64 + i) * .093)) for i in range(64)]
            if tick % 97 == 13: left, right = [1.] + [0.] * 63, [-1.] + [0.] * 63
            expected = native.process(left, right)
            a, b = (C.c_float * 64)(*left), (C.c_float * 64)(*right)
            out_a, out_b = (C.c_float * 64)(), (C.c_float * 64)()
            lib.retro_wobble_process(core, a, b, out_a, out_b, 64)
            for actual, reference in zip(list(out_a) + list(out_b), expected[0] + expected[1]):
                assert math.isfinite(actual) and math.isfinite(reference)
                audio_error = max(audio_error, abs(actual - reference))
                assert actual == reference, (label, sample_rate, seeds, tick, 'audio', actual, reference)
            counts['ticks'] += 1; counts['noise_outputs'] += 2; counts['noise_state_bytes'] += 80
            counts['targets'] += 5; counts['audio_samples'] += 128
        finally:
            lib.retro_wobble_destroy(core)
        results.append(dict(case=label, sample_rate=sample_rate, seeds=seeds, ticks=ticks, noise_ranges=ranges))
        counts['scenarios'] += 1
        del native, model
        gc.collect()
    if args.static_ticks >= 1024:
        for result in results:
            if result['case'] == 'full':
                assert all(high > 0 and low < 0 for low, high in result['noise_ranges']), result
    result = dict(scope='synthetic settled Wobble macro relations + original Drive + normalized stereo cWobblerBlock',
                  reference_sha256=HASH, library=str(args.library.resolve()), counts=counts,
                  max_control_error=control_error, max_audio_error=audio_error, bit_exact_noise_state=True,
                  results=results, limitations=[
                      'Explicit synthetic seeds; production serialized seed indices unresolved',
                      'Notifications and inputs manually settled once per 64-frame tick',
                      'No complete original ModelStore scheduler, host or resource-script execution',
                      'Synthetic prepare/model state and system libm bindings; visualization disabled'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'results'}))


if __name__ == '__main__':
    main()
