#!/usr/bin/env python3
"""Compare C Magnetic with the isolated hash-pinned original cFlutter kernel."""
import argparse
import ctypes as C
import gc
import json
import math
from pathlib import Path
from native_magnetic_probe import NativeMagnetic
from native_bitcolor_probe import ROOT, HASH


class Params(C.Structure):
    _fields_ = [(name, C.c_float) for name in [
        'depth', 'depth_steer', 'rate', 'stereo_phase', 'wear_gain', 'wear_pan',
        'wear_stereo', 'wear_depth', 'dropout_steer', 'dropout_amount',
        'dropout_min', 'dropout_max', 'enable']]


def library(path):
    lib = C.CDLL(str(path.resolve()))
    lib.retro_magnetic_defaults.restype = Params
    lib.retro_magnetic_create.restype = C.c_void_p
    lib.retro_magnetic_destroy.argtypes = [C.c_void_p]
    lib.retro_magnetic_set.argtypes = [C.c_void_p, C.POINTER(Params)]
    lib.retro_magnetic_prepare.argtypes = [C.c_void_p, C.c_float]
    lib.retro_magnetic_reset.argtypes = [C.c_void_p]
    lib.retro_magnetic_reset_smoothing.argtypes = [C.c_void_p]
    lib.retro_magnetic_process.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.POINTER(C.c_float), C.c_size_t]
    return lib


def values(params):
    return {name: getattr(params, name) for name, _ in Params._fields_}


def run(lib, rate, overrides, blocks, updates=None, smoothing_at=None, reset_at=None, partition=False):
    p = lib.retro_magnetic_defaults()
    for name, value in overrides.items(): setattr(p, name, value)
    native = NativeMagnetic(sample_rate=rate, **values(p))
    core = lib.retro_magnetic_create()
    assert core and lib.retro_magnetic_set(core, C.byref(p)) and lib.retro_magnetic_prepare(core, rate)
    worst = 0.; unequal = 0; samples = 0; examples = []
    try:
      for block in range(blocks):
        if updates and block in updates:
            for name, value in updates[block].items(): setattr(p, name, value)
            native.set(**{name: getattr(p, name) for name in updates[block]})
            assert lib.retro_magnetic_set(core, C.byref(p))
        if block == smoothing_at:
            native.reset_smoothing(); lib.retro_magnetic_reset_smoothing(core)
        if block == reset_at:
            # Full study reset is compared against freshly reconstructed native
            # constructor/prepare state; original resetSmoothing is only partial.
            del native; gc.collect()
            native = NativeMagnetic(sample_rate=rate, **values(p))
            lib.retro_magnetic_reset(core)
        left = [C.c_float(.6 * math.sin((block * 64 + i) * .071)).value for i in range(64)]
        right = [C.c_float(.4 * math.cos((block * 64 + i) * .093)).value for i in range(64)]
        if block % 97 == 13: left, right = [1.] + [0.] * 63, [-1.] + [0.] * 63
        if block % 97 in [14, 15]: left, right = [0.] * 64, [0.] * 64
        expected = native.process(left, right)
        a, b = (C.c_float * 64)(*left), (C.c_float * 64)(*right)
        if partition:
            offset = 0
            for length in [1, 7, 13, 3, 29, 11]:
                pa = C.cast(C.byref(a, offset * 4), C.POINTER(C.c_float))
                pb = C.cast(C.byref(b, offset * 4), C.POINTER(C.c_float))
                lib.retro_magnetic_process(core, pa, pb, length)
                offset += length
            assert offset == 64
        else:
            lib.retro_magnetic_process(core, a, b, 64)
        for actual, reference in zip(list(a) + list(b), expected[0] + expected[1]):
            assert math.isfinite(actual) and math.isfinite(reference), (rate, overrides, block, actual, reference)
            worst = max(worst, abs(actual - reference))
            if actual != reference:
                unequal += 1
                if len(examples) < 2: examples.append(dict(block=block, sample=samples, actual=actual, reference=reference))
            samples += 1
    finally:
        lib.retro_magnetic_destroy(core)
    return dict(sample_rate=rate, parameters=overrides, blocks=blocks, updates=updates,
                smoothing_at=smoothing_at, reset_at=reset_at, partition=partition,
                samples=samples, max_error=worst, unequal_samples=unequal, examples=examples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=ROOT / 'build-release/libretro_dsp.so')
    parser.add_argument('--report', type=Path, default=ROOT / 'analysis/re/native-magnetic-comparison.json')
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args(); lib = library(args.library)
    cases = [
        {}, dict(depth=0), dict(wear_depth=0), dict(depth=0, wear_depth=0),
        dict(enable=0), dict(depth=1, depth_steer=-1), dict(depth_steer=0), dict(depth_steer=1),
        dict(rate=0), dict(rate=1), dict(stereo_phase=1), dict(stereo_phase=.37),
        dict(wear_gain=1), dict(wear_gain=-1), dict(wear_gain=.4, wear_depth=.3),
        dict(wear_gain=.8, wear_pan=-1, wear_stereo=1),
        dict(wear_gain=.8, wear_pan=1, wear_stereo=1),
        dict(wear_gain=-.8, wear_pan=.23, wear_stereo=1),
        dict(dropout_amount=1, dropout_steer=0),
        dict(dropout_amount=1, dropout_steer=.17),
        dict(dropout_amount=1, dropout_steer=1),
        dict(dropout_amount=.3, dropout_steer=.5, dropout_min=.3, dropout_max=.8),
        dict(dropout_amount=1, dropout_steer=.7, dropout_min=.68, dropout_max=.88,
             wear_stereo=1, wear_pan=-.4),
    ]
    results = []
    for rate in ([48000] if args.quick else [44100, 48000, 96000]):
        for case in cases:
            results.append(run(lib, rate, case, 8 if args.quick else 512))
            gc.collect()
    if not args.quick:
        updates = {
            3: dict(depth=.17, depth_steer=-.3, wear_gain=.4),
            9: dict(rate=1, stereo_phase=1, wear_stereo=1, wear_pan=-.7),
            33: dict(dropout_amount=1, dropout_steer=.6, dropout_min=.4, dropout_max=.9),
            83: dict(enable=0), 104: dict(enable=1),
            174: dict(depth=0, wear_depth=0), 211: dict(depth=1, wear_depth=.8, depth_steer=0),
            249: dict(dropout_steer=.2, stereo_phase=.2, wear_gain=-.8, wear_pan=.6),
            399: dict(dropout_amount=0, rate=0, wear_stereo=0),
            517: dict(depth_steer=1, wear_depth=.2, wear_gain=.9),
            769: dict(depth_steer=-1, dropout_amount=1, dropout_min=.1, dropout_max=.25),
            1100: dict(wear_stereo=1, wear_pan=-.3), 1300: dict(wear_stereo=0),
            1500: dict(wear_stereo=1, wear_pan=.9),
        }
        for rate in [44100, 48000, 96000]:
            results.append(run(lib, rate, {}, 2048, updates, smoothing_at=300, reset_at=1024))
            results.append(run(lib, rate, {}, 2048, updates, smoothing_at=300, reset_at=1024, partition=True))
        results.append(run(lib, 48000, dict(rate=0, stereo_phase=1, wear_stereo=1,
                           wear_pan=-.8, wear_gain=.9, dropout_amount=1, dropout_steer=.7), 4096))
    result = dict(reference_sha256=HASH, library=str(args.library.resolve()),
                  scope='isolated stereo Magnetic kernel with synthetic constructor/prepare/model state; no macro graph or original scheduler',
                  cases=len(results), samples=sum(r['samples'] for r in results),
                  max_error=max(r['max_error'] for r in results), unequal_samples=sum(r['unequal_samples'] for r in results),
                  results=results, limitations=[
                      'System libm bindings and reconstructed cached wrappers; visualization disabled',
                      'Native resetSmoothing compared separately from full study reset',
                      'Full reset compares C with freshly reconstructed original prepare state',
                      '64-frame native batches; C partition tests subdivide those batches without control changes'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'results'}))
    for r in results:
        if r['unequal_samples']: print(json.dumps(r))
    raise SystemExit(result['max_error'] > 2e-6)


if __name__ == '__main__':
    main()
