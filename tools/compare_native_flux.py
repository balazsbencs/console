#!/usr/bin/env python3
"""Compare settled Digital Flux adapter against isolated native relations/Drive/audio.

Synthetic two-generator records use explicit seeds, not captured production
relation indices. Notifications are explicitly settled before each Drive call;
this does not recreate the production ModelStore scheduler.
"""
import argparse
import ctypes as C
import importlib.util
import json
import math
from pathlib import Path
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


class Flux(C.Structure):
    _fields_=[('bits',C.c_ubyte*40),('rate',C.c_ubyte*40),
              ('bit_output',C.c_float),('rate_output',C.c_float)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,default=ROOT/'build-release/libretro_dsp.so')
    parser.add_argument('--report',type=Path,default=ROOT/'analysis/re/native-flux-comparison.json')
    args=parser.parse_args()
    spec=importlib.util.spec_from_file_location('comparison',ROOT/'tests/compare_native.py')
    comparison=importlib.util.module_from_spec(spec);spec.loader.exec_module(comparison)
    lib=comparison.configure_library(args.library.resolve())
    lib.rs_digital_flux_reset.argtypes=[C.POINTER(Flux),C.c_int32,C.c_int32]
    lib.rs_digital_flux_tick.argtypes=[C.POINTER(Flux)]+[C.c_float]*4
    lib.rs_digital_flux_targets.argtypes=[C.POINTER(Flux)]+[C.c_float]*3+[C.POINTER(C.c_float)]*2
    lib.retro_digital_reset.argtypes=[C.POINTER(comparison.Digital)]
    f=lambda v:C.c_float(v).value
    counts={'scenarios':0,'ticks':0,'noise_outputs':0,'noise_state_bytes':0,'targets':0,'audio_samples':0}
    control_error=audio_error=0.0
    def exact(x,y,context):
        nonlocal control_error
        control_error=max(control_error,abs(x-y))
        assert struct.pack('<f',x)==struct.pack('<f',y),(context,x,y)
    for sample_rate in [44100.,48000.,96000.]:
      for seeds in [(0,1),(115,273),(-1,2147483647),(391,18)]:
        image=NativeBitColor(sample_rate=sample_rate,normalized=1,bits=0,rate=0)
        stub=0x578c96
        slot=stub+6+struct.unpack_from('<i',image.image,stub+2)[0]
        struct.pack_into('<Q',image.image,slot,C.cast(image.libm.pow,C.c_void_p).value)
        handler=C.create_string_buffer(0x15030)
        store=C.create_string_buffer(8);vtable=C.create_string_buffer(0x40)
        struct.pack_into('<Q',store,0,C.addressof(vtable))
        struct.pack_into('<Q',handler,0x10,C.addressof(store))
        values={};writes={}
        def get(_,key):return values[key]
        def set_(_,key,value):values[key]=value;writes[key]=value
        getter=C.CFUNCTYPE(C.c_float,C.c_void_p,C.c_int)(get)
        setter=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_float)(set_)
        struct.pack_into('<Q',vtable,0x38,C.cast(getter,C.c_void_p).value)
        struct.pack_into('<Q',vtable,0x30,C.cast(setter,C.c_void_p).value)
        changed=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_int)(image.base+0xf0d70)
        drive=C.CFUNCTYPE(None,C.c_void_p,C.c_float)(image.base+0xd6fd0)
        reset=C.CFUNCTYPE(None,C.c_void_p,C.c_int32)(image.base+0x1a5980)
        for index,seed in enumerate(seeds):
            off=0x20+index*0x70
            struct.pack_into('<6i',handler,off,10+index*10,11+index*10,14+index*10,
                             13+index*10,12+index*10,-1)
            struct.pack_into('<i',handler,off+0x20,seed)
            struct.pack_into('<f',handler,off+0x24,-1.)
            struct.pack_into('<i',handler,off+0x60,0x4f)
            struct.pack_into('<Q',handler,off+0x68,C.addressof(handler)+0x90 if index==0 else 0)
            reset(C.addressof(handler)+off+0x28,seed)
        struct.pack_into('<Q',handler,0x15020,C.addressof(handler)+0x20)
        # Scratch third record lets the native arithmetic compute upstream and targets.
        def relation(kind,a,b=0.,c=0.,params=()):
            off=0x100
            values.update({100:a,101:b,102:c})
            struct.pack_into('<4i',handler,off,100,101,-1,102)
            if params:struct.pack_into('<'+'f'*len(params),handler,off+0x10 if kind=='M' else off+0xc,*params)
            struct.pack_into('<i',handler,off+0x60,ord(kind))
            changed(handler,0,2)
            return writes[-1]
        def native_targets(magnitude,amount,balance):
            off=0x100;values[100]=balance
            struct.pack_into('<5i',handler,off,100,-1,-1,103,104)
            struct.pack_into('<i',handler,off+0x60,ord('X'))
            changed(handler,0,2)
            rate_weight,bit_weight=values[103],values[104]
            scaled=relation('m',magnitude,amount)
            b=relation('b',relation('m',bit_weight,scaled),values[14],.75)
            r=relation('b',relation('m',rate_weight,scaled),values[24],.75)
            return b,r
        flux=Flux();lib.rs_digital_flux_reset(C.byref(flux),*seeds)
        dsp=comparison.Digital();p=lib.retro_digital_defaults();p.normalized=1;p.bits=0;p.rate=0
        assert lib.retro_digital_set(C.byref(dsp),C.byref(p))
        assert lib.retro_digital_prepare(C.byref(dsp),sample_rate)
        rng=random.Random(sample_rate+seeds[0])
        for tick in range(256):
            magnitude,amount,fl,bal=(f(v) for v in [rng.random(),rng.random(),rng.random(),rng.random()])
            if tick%17==0:fl=0.
            if tick%23==0:amount=0.
            if tick%29==0:magnitude=0.
            if tick in [0,1,2]:magnitude=amount=fl=1.;bal=f(tick*.5)
            if tick in [61,173]:
                lib.rs_digital_flux_reset(C.byref(flux),*seeds)
                for i in range(2):handler[0x20+i*0x70+0x1c]=b'\x01'
                image.reset();lib.retro_digital_reset(C.byref(dsp))
            scaled=relation('M',magnitude,fl,amount,(.5,.5,.99))
            smooth=relation('f',scaled,params=(0.,1.,.9484,.85,.5))
            for i,sm,depth in [(0,f(.94847450187182),f(.3)),(1,smooth,f(.2))]:
                values.update({10+i*10:relation('m',scaled,depth),11+i*10:0.,12+i*10:sm,13+i*10:0.})
                # Native objectChanged marks coefficients stale; explicit settling models notifications.
                changed(handler,0,i)
            drive(handler,f(sample_rate*f(.015625)))
            lib.rs_digital_flux_tick(C.byref(flux),sample_rate,magnitude,fl,amount)
            exact(values[14],flux.bit_output,('bit noise',sample_rate,seeds,tick))
            exact(values[24],flux.rate_output,('rate noise',sample_rate,seeds,tick))
            for i,ported in [(0,flux.bits),(1,flux.rate)]:
                original=handler.raw[0x20+i*0x70+0x28:0x20+i*0x70+0x50]
                assert original==bytes(ported),('noise state',sample_rate,seeds,tick,i)
            bits,rate=native_targets(magnitude,amount,bal)
            cb,cr=C.c_float(),C.c_float()
            lib.rs_digital_flux_targets(C.byref(flux),magnitude,amount,bal,C.byref(cb),C.byref(cr))
            exact(bits,cb.value,('bit target',tick));exact(rate,cr.value,('rate target',tick))
            image.set(bits=bits,rate=rate);p.bits=cb.value;p.rate=cr.value
            assert lib.retro_digital_set(C.byref(dsp),C.byref(p))
            left=[f(.5*math.sin((tick*64+j)*.071)) for j in range(64)]
            right=[f(.4*math.cos((tick*64+j)*.113)) for j in range(64)]
            expected=image.process(left,right)
            left_c,right_c=(C.c_float*64)(*left),(C.c_float*64)(*right)
            outl,outr=(C.c_float*64)(),(C.c_float*64)()
            lib.retro_digital_process(C.byref(dsp),left_c,right_c,outl,outr,64)
            for actual,reference in zip(list(outl)+list(outr),expected[0]+expected[1]):
                assert math.isfinite(actual) and math.isfinite(reference)
                audio_error=max(audio_error,abs(actual-reference))
                assert actual==reference,('audio',sample_rate,seeds,tick,actual,reference)
            counts['ticks']+=1;counts['noise_outputs']+=2;counts['noise_state_bytes']+=80
            counts['targets']+=2;counts['audio_samples']+=128
        counts['scenarios']+=1
    result=dict(scope='synthetic settled Digital Flux relations + original Drive + normalized stereo cBitColor',
                reference_sha256=HASH,library=str(args.library.resolve()),counts=counts,
                max_control_error=control_error,max_audio_error=audio_error,bit_exact_noise_state=True,
                limitations=['Explicit synthetic seeds; production serialized seed indices unresolved',
                             'Inputs and notifications settled manually before each 64-frame tick',
                             'No complete original ModelStore scheduler, host or resource-script execution',
                             'System libm bindings for isolated native functions'])
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
