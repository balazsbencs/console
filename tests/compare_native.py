#!/usr/bin/env python3
"""Compare independent C DSP against original hash-pinned native DSP bytes."""
import argparse
import ctypes as C
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from native_bitcolor_probe import NativeBitColor, HASH


class Params(C.Structure):
    _fields_ = [(name, C.c_float) for name in
                ['bits', 'rate', 'smooth', 'mix', 'feedback', 'low', 'high', 'round_error', 'stereo', 'aa2', 'aa3',
                 'focus', 'mod_rate', 'mod_depth', 'normalized']]


class Channel(C.Structure):
    _fields_ = [(name, C.c_float) for name in ['hp_input', 'hp_output', 'lp_output',
                  'hp2_input', 'hp2_output', 'lp2_output', 'phase']]
    _fields_ += [('ring', C.c_float * 32)]
    _fields_ += [(name, C.c_float) for name in ['slew', 'filtered']]
    _fields_ += [('position', C.c_uint)]


class Digital(C.Structure):
    _fields_ = [('params', Params)]
    _fields_ += [(name, C.c_float) for name in ['sample_rate', 'bits_state', 'mix_state',
        'bits_pole', 'bits_feed', 'mix_pole', 'mix_feed', 'hp_pole', 'hp_feed', 'lp_pole', 'lp_feed',
        'advance', 'step', 'levels', 'quant_gain', 'rounding', 'blend_scale', 'slew_limit',
        'output_feed', 'feedback_gain', 'wet_target', 'rate_state', 'gain_state', 'gain_target', 'mod_phase']]
    _fields_ += [('control_remaining', C.c_uint), ('channels', Channel * 2)]


def configure_library(path):
    lib = C.CDLL(str(path))
    lib.retro_digital_defaults.restype = Params
    lib.retro_digital_prepare.argtypes = [C.POINTER(Digital), C.c_float]
    lib.retro_digital_set.argtypes = [C.POINTER(Digital), C.POINTER(Params)]
    lib.retro_digital_process.argtypes = [C.POINTER(Digital)] + [C.POINTER(C.c_float)] * 4 + [C.c_size_t]
    return lib


def run(lib, sample_rate, overrides, updates=None, reset_at=None):
    native = NativeBitColor(sample_rate=sample_rate, **overrides)
    dsp = Digital(); p = lib.retro_digital_defaults()
    for key, value in overrides.items(): setattr(p, key, value)
    assert lib.retro_digital_set(C.byref(dsp), C.byref(p))
    assert lib.retro_digital_prepare(C.byref(dsp), sample_rate)
    worst, squared, mismatch, samples = 0.0, 0.0, 0, 0
    for block in range(32):
        if updates and block in updates:
            native.set(**updates[block])
            for key, value in updates[block].items(): setattr(p, key, value)
            assert lib.retro_digital_set(C.byref(dsp), C.byref(p))
        if block == reset_at:
            native.reset(); lib.retro_digital_reset(C.byref(dsp))
        # Include initial transients, positive/negative input, unrelated stereo,
        # silence, impulse and a signal above nominal full scale.
        start = block * 64
        left = [0.6 * math.sin((start + i) * 0.103) for i in range(64)]
        right = [0.4 * math.cos((start + i) * 0.079) for i in range(64)]
        if block == 12: left = [1.0] + [0.0] * 63; right = [-1.0] + [0.0] * 63
        if block in [13, 14]: left = [0.0] * 64; right = [0.0] * 64
        if block == 20: left = [v * 4 for v in left]; right = [v * 5 for v in right]
        expected = native.process(left, right)
        l, r = (C.c_float * 64)(*left), (C.c_float * 64)(*right)
        ol, or_ = (C.c_float * 64)(), (C.c_float * 64)()
        lib.retro_digital_process(C.byref(dsp), l, r, ol, or_, 64)
        for actual, reference in zip(list(ol) + list(or_), expected[0] + expected[1]):
            assert math.isfinite(actual)
            error = abs(actual - reference)
            worst = max(worst, error); squared += error * error
            mismatch += actual != reference; samples += 1
    return dict(sample_rate=sample_rate, parameters=overrides, updates=updates, reset_at=reset_at, samples=samples,
                max_error=worst, rms_error=math.sqrt(squared / samples), nonidentical_samples=mismatch)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=ROOT / 'build/libretro_dsp.so')
    parser.add_argument('--report', type=Path, default=ROOT / 'analysis/re/native-comparison.json')
    args = parser.parse_args()
    lib = configure_library(args.library.resolve())
    lib.retro_digital_reset.argtypes = [C.POINTER(Digital)]
    scenarios = [{}, {'bits': 1}, {'bits': 12}, {'bits': 23.5, 'rate': 44100},
        {'rate': 44.1}, {'rate': 200}, {'smooth': 1}, {'smooth': 0.4, 'bits': 2},
        {'feedback': 0.7, 'smooth': 0.5}, {'mix': 0.3}, {'stereo': 0},
        {'low': 500, 'high': 3000, 'round_error': 0.6},
        {'focus': 0}, {'focus': 0, 'low': 500, 'high': 3000},
        {'mod_rate': 0.5, 'mod_depth': 0.8}, {'mod_rate': 1, 'mod_depth': 1},
        *[dict(normalized=1, bits=b, rate=r, focus=f, low=500, high=3000,
               mod_rate=0.61, mod_depth=0.4) for b, r in [(0, 0), (0.1, 0.2), (0.5, 0.5), (0.8, 0.1), (1, 1)] for f in [0, 1]]]
    results = [run(lib, rate, case) for rate in [44100, 48000, 96000] for case in scenarios]
    physical_updates = {3: {'bits': 1, 'rate': 44.1}, 7: {'bits': 23.5, 'rate': 44100},
        9: {'smooth': 1, 'feedback': 0.8}, 15: {'low': 700, 'high': 2300, 'mix': 0.3},
        19: {'mod_rate': 0.8, 'mod_depth': 0.7}, 26: {'bits': 8, 'rate': 200}}
    normalized_updates = {3: {'bits': 1, 'rate': 1}, 7: {'bits': 0.1, 'rate': 0.05},
        9: {'smooth': 1, 'feedback': 0.8}, 15: {'low': 700, 'high': 2300, 'mix': 0.3},
        19: {'mod_rate': 0.8, 'mod_depth': 0.7}, 26: {'bits': 0.3, 'rate': 0.7}}
    for sample_rate in [44100, 48000, 96000]:
        for focus in [0, 1]:
            results.append(run(lib, sample_rate, dict(focus=focus), physical_updates, reset_at=23))
            results.append(run(lib, sample_rate, dict(normalized=1, bits=0.5, rate=0.5, focus=focus),
                               normalized_updates, reset_at=23))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(dict(reference_sha256=HASH,
        scope='stereo cBitColor with Focus/Cut, LFO, original normalized curves, batch-aligned updates/reset; synthetic state, system libm; external Flux absent',
        results=results), indent=2) + '\n')
    failures = [r for r in results if r['max_error'] > 2e-6]
    for result in failures: print(json.dumps(result))
    print(f"{len(results)} cases; {sum(r['samples'] for r in results)} samples; "
          f"worst error {max(r['max_error'] for r in results):.9g}; {len(failures)} failures")
    raise SystemExit(bool(failures))
