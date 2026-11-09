#!/usr/bin/env python3
"""Compare C Wobble against the isolated hash-pinned original kernel."""
import argparse
import ctypes as C
import json
import math
from pathlib import Path
from native_wobble_probe import NativeWobble
from native_bitcolor_probe import ROOT, HASH

class Params(C.Structure):
    _fields_ = [(n,C.c_float) for n in ['wow_depth','flutter_depth','wow_rate','flutter_rate',
                                    'stereo_phase','mix','lfo_noise','noise_scale','enable']]

def library(path):
    lib=C.CDLL(str(path.resolve()))
    lib.retro_wobble_defaults.restype=Params
    lib.retro_wobble_create.restype=C.c_void_p
    lib.retro_wobble_destroy.argtypes=[C.c_void_p]
    lib.retro_wobble_set.argtypes=[C.c_void_p,C.POINTER(Params)]
    lib.retro_wobble_prepare.argtypes=[C.c_void_p,C.c_float]
    lib.retro_wobble_reset_phase.argtypes=[C.c_void_p]
    lib.retro_wobble_reset_smoothing.argtypes=[C.c_void_p]
    lib.retro_wobble_process.argtypes=[C.c_void_p]+[C.POINTER(C.c_float)]*4+[C.c_size_t]
    return lib

NAMES=dict(wow_depth='depth',flutter_depth='depth2',wow_rate='rate',flutter_rate='rate2',
           stereo_phase='stereo_phase',mix='mix',lfo_noise='noise',noise_scale='noise_scaler',enable='enabled')
def run(lib,rate,overrides,blocks,updates=None,reset_at=None):
    p=lib.retro_wobble_defaults()
    for k,v in overrides.items():setattr(p,k,v)
    native=NativeWobble(sample_rate=rate,normalized=1,**{v:getattr(p,k) for k,v in NAMES.items()})
    dsp=lib.retro_wobble_create()
    assert dsp and lib.retro_wobble_set(dsp,C.byref(p)) and lib.retro_wobble_prepare(dsp,rate)
    worst=0.; unequal=0; samples=0
    try:
        for block in range(blocks):
            if updates and block in updates:
                for k,v in updates[block].items():setattr(p,k,v)
                native.set(**{NAMES[k]:v for k,v in updates[block].items()})
                assert lib.retro_wobble_set(dsp,C.byref(p))
            if block==reset_at:
                native.reset();lib.retro_wobble_reset_phase(dsp);lib.retro_wobble_reset_smoothing(dsp)
            left=[.6*math.sin((block*64+i)*.071) for i in range(64)]
            right=[.4*math.cos((block*64+i)*.093) for i in range(64)]
            if block%64==13:left=[1.]+[0.]*63;right=[-1.]+[0.]*63
            if block%64 in [14,15]:left=[0.]*64;right=[0.]*64
            expected=native.process(left,right)
            a,b=(C.c_float*64)(*left),(C.c_float*64)(*right)
            outa,outb=(C.c_float*64)(),(C.c_float*64)()
            lib.retro_wobble_process(dsp,a,b,outa,outb,64)
            for actual,reference in zip(list(outa)+list(outb),expected[0]+expected[1]):
                assert math.isfinite(actual) and math.isfinite(reference)
                error=abs(actual-reference);worst=max(worst,error)
                if actual!=reference:
                    unequal+=1
                    if unequal<3: print('difference',rate,overrides,block,samples,actual,reference,error)
                samples+=1
    finally:lib.retro_wobble_destroy(dsp)
    return dict(sample_rate=rate,parameters=overrides,blocks=blocks,updates=updates,
                reset_at=reset_at,samples=samples,max_error=worst,unequal_samples=unequal)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,default=ROOT/'build-release/libretro_dsp.so')
    parser.add_argument('--report',type=Path,default=ROOT/'analysis/re/native-wobble-comparison.json')
    parser.add_argument('--quick',action='store_true')
    args=parser.parse_args();lib=library(args.library)
    cases=[{},dict(wow_depth=0),dict(flutter_depth=0),dict(wow_depth=0,flutter_depth=0),
           dict(stereo_phase=.4),dict(mix=0),dict(mix=.3),dict(mix=.5),
           dict(wow_rate=0,flutter_rate=0),dict(wow_rate=1,flutter_rate=1),
           dict(wow_depth=1,flutter_depth=1),dict(noise_scale=1,lfo_noise=.7),
           dict(noise_scale=1,lfo_noise=-.7,stereo_phase=.4),dict(enable=0)]
    results=[run(lib,rate,case,8 if args.quick else 512) for rate in ([48000] if args.quick else [44100,48000,96000]) for case in cases]
    if not args.quick:
        updates={3:dict(wow_depth=.1,flutter_depth=1),9:dict(wow_rate=1,flutter_rate=0,stereo_phase=.4),
                 33:dict(mix=.2,noise_scale=.9,lfo_noise=-.7),83:dict(enable=0),104:dict(enable=1),
                 174:dict(wow_depth=0,flutter_depth=0),211:dict(wow_depth=1,mix=1),
                 249:dict(lfo_noise=.6,stereo_phase=0)}
        results += [run(lib,rate,{},1024,updates,reset_at=300) for rate in [44100,48000,96000]]
        results += [run(lib,48000,dict(wow_rate=0,flutter_rate=0,stereo_phase=.4),4096)]
    result=dict(reference_sha256=HASH,scope='stereo normalized Wobble with synthetic prepare/cached model state; visualization disabled; no macro Flux generator',results=results)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(len(results),'cases',sum(x['samples'] for x in results),'samples; max error',max(x['max_error'] for x in results),'unequal',sum(x['unequal_samples'] for x in results))
    raise SystemExit(any(x['max_error']>2e-6 for x in results))
if __name__=='__main__':main()
