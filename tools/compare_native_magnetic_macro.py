#!/usr/bin/env python3
"""Compare the settled Magnetic macro graph, original Drive and cFlutter audio.
Synthetic records and explicit seeds; no full plugin or original scheduler.
"""
import argparse
import ctypes as C
import gc
import json
import math
from pathlib import Path
import random
import struct
from compare_native_magnetic import Params, library, values
from native_magnetic_probe import NativeMagnetic
from native_bitcolor_probe import ROOT, HASH

def f(x): return C.c_float(x).value
class Macro(C.Structure):
    _fields_=[(n,C.c_ubyte*40) for n in ['depth','dropout','wear','pan']]+[(n+'_output',C.c_float) for n in ['depth','dropout','wear','pan']]

class NativeMacro:
    """Original handler arithmetic and Drive with four synthetic noise records."""
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
            struct.pack_into('<i', self.handler, off + 0x60, ord('N') if index == 1 else ord('O'))
            struct.pack_into('<Q', self.handler, off + 0x68,
                             C.addressof(self.handler) + off + 0x70 if index < 3 else 0)
            reset(C.addressof(self.handler) + off + 0x28, seed)
        struct.pack_into('<Q', self.handler, 0x15020, C.addressof(self.handler) + 0x20)

    def relation(self, kind, a, b=0., c=0., params=()):
        off = 0x1e0
        self.values.update({100: a, 101: b, 102: c})
        struct.pack_into('<4i', self.handler, off, 100, 101, -1, 102)
        if params:
            if kind == 'F':
                lo, mid, hi, kl, kh = params
                struct.pack_into('<5f',self.handler,off+0xc,lo,hi,mid,kl,kh)
            else:
                struct.pack_into('<'+'f'*len(params), self.handler,
                                 off+(0x10 if kind=='M' else 0xc),*params)
        struct.pack_into('<i', self.handler, off + 0x60, ord(kind))
        self.changed(self.handler, 0, 4)
        return self.values[-1]

    def dropout_amount(self,magnitude,amount,dropouts):
        a=self.relation('m',magnitude,amount)
        d=self.relation('f',a,params=(0.,1.,0.,1.,.98))
        return self.relation('m',dropouts,self.relation('m',d,magnitude))

    def tick(self,sample_rate,magnitude,flux,amount,dropouts):
        group=self.relation('M',magnitude,flux,amount,(.5,.5,.99))
        d=self.dropout_amount(magnitude,amount,dropouts)
        gain=self.relation('F',group,params=(.14,.17,.5,.5,.5))
        ds=self.relation('f',group,params=(0.,1.,.83,.5,.5))
        ws=self.relation('f',group,params=(0.,1.,.91,.4,.5))
        dos=self.relation('f',d,params=(0.,1.,.3,.05,.15))
        for index,gain,chaos,smooth,decay in [
            (0,gain,0,ds,.5),(1,.87837785,.424324,dos,.656756758),
            (2,.44,0,ws,.5),(3,.40,0,.837,.5)]:
            base=10+index*10
            self.values.update({base:f(gain),base+1:f(chaos),base+2:f(smooth),base+3:f(decay)})
            self.changed(self.handler,0,index)
        self.drive(self.handler,f(sample_rate*f(.015625)))

    def targets(self,magnitude,amount,balance,dropouts):
        off=0x1e0; self.values[100]=balance
        struct.pack_into('<5i',self.handler,off,100,-1,-1,103,104)
        struct.pack_into('<i',self.handler,off+0x60,ord('X'))
        self.changed(self.handler,0,4)
        wear,flutter=self.values[103],self.values[104]
        a=self.relation('m',magnitude,amount)
        depth=self.relation('m',flutter,a); wd=self.relation('m',wear,a)
        ds=self.relation('b',.5,self.values[14],.85)
        dos=self.relation('b',0,self.values[24],.75)
        wg=self.relation('b',0,self.values[34],.85)
        wp=self.relation('b',0,self.values[44],.85)
        d=self.dropout_amount(magnitude,amount,dropouts)
        lo=self.relation('f',d,params=(0.,1.,.1,.68,.5))
        hi=self.relation('f',d,params=(0.,1.,.25,.88,.5))
        return dict(depth=depth,wear_depth=wd,depth_steer=ds,dropout_steer=dos,
                    wear_gain=wg,wear_pan=wp,dropout_amount=d,dropout_min=lo,dropout_max=hi)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--library',type=Path,default=ROOT/'build-release/libretro_dsp.so')
    ap.add_argument('--report',type=Path,default=ROOT/'analysis/re/native-magnetic-macro-comparison.json')
    ap.add_argument('--static-ticks',type=int,default=1024)
    ap.add_argument('--dynamic-ticks',type=int,default=4096)
    args=ap.parse_args(); assert args.static_ticks>0 and args.dynamic_ticks>0
    lib=library(args.library)
    lib.rs_magnetic_macro_reset.argtypes=[C.POINTER(Macro)]+[C.c_int32]*4
    lib.rs_magnetic_macro_tick.argtypes=[C.POINTER(Macro)]+[C.c_float]*5
    lib.rs_magnetic_macro_apply.argtypes=[C.POINTER(Macro)]+[C.c_float]*4+[C.POINTER(Params)]
    counts=dict(scenarios=0,ticks=0,noise_outputs=0,noise_state_bytes=0,targets=0,audio_samples=0)
    def exact(a,b,ctx):
        assert struct.pack('<f',a)==struct.pack('<f',b),(ctx,a,b)
    # magnitude, flux, amount, balance, dropouts; wear stereo is varied separately.
    cases=[('flux_zero',(1,0,1,.5,1)),('amount_zero',(1,1,0,.5,1)),
           ('magnitude_zero',(0,1,1,.5,1)),('wear_only',(1,1,1,0,0)),
           ('flutter_only',(1,1,1,1,0)),('full',(1,1,1,.5,1)),
           ('partial',(.6,.8,.7,.2,.63)),('small_amount',(1,1,.001,.7,1)),
           ('dropouts_zero',(1,.3,.8,.8,0)),('changing',None)]
    results=[]
    for rate in [44100.,48000.,96000.]:
      for seeds in [(0,1,2,3),(-1,2147483647,123456,-2147483648)]:
       for label,controls in cases:
        p=lib.retro_magnetic_defaults(); p.stereo_phase=.37; p.wear_stereo=1
        native=NativeMagnetic(sample_rate=rate,**values(p))
        model=NativeMacro(native,seeds); macro=Macro()
        lib.rs_magnetic_macro_reset(C.byref(macro),*seeds)
        core=lib.retro_magnetic_create()
        assert core and lib.retro_magnetic_set(core,C.byref(p)) and lib.retro_magnetic_prepare(core,rate)
        rng=random.Random(int(rate)+seeds[0]);ranges=[[math.inf,-math.inf] for _ in range(4)]
        ticks=args.dynamic_ticks if controls is None else args.static_ticks
        try:
          for tick in range(ticks):
            if controls is None:
                if tick%31==0:
                    current=[f(rng.random()) for _ in range(5)]
                    if tick%93==0: current[1]=0.
                    if tick%155==0: current[2]=0.
                    if tick%217==0: current[0]=0.
                    if tick%341==0: current=[1.,1.,1.,float((tick//341)%2),1.]
                p.enable=0 if 100<=tick%701<200 else 1
                p.wear_stereo=float((tick//113)%2)
                p.rate=f((tick//127)%11/10)
                if tick in [513,2049]:
                    # Both original and C generators restart with explicit seeds.
                    model=NativeMacro(native,seeds)
                    lib.rs_magnetic_macro_reset(C.byref(macro),*seeds)
            else: current=[f(x) for x in controls]
            magnitude,flux,amount,balance,dropouts=current
            model.tick(rate,magnitude,flux,amount,dropouts)
            lib.rs_magnetic_macro_tick(C.byref(macro),rate,magnitude,flux,amount,dropouts)
            for i,name in enumerate(['depth','dropout','wear','pan']):
                out=getattr(macro,name+'_output'); exact(out,model.values[14+i*10],(label,tick,name))
                off=0x20+i*0x70+0x28
                assert bytes(getattr(macro,name))==model.handler.raw[off:off+40],(label,tick,'state',name)
                ranges[i][0]=min(ranges[i][0],out); ranges[i][1]=max(ranges[i][1],out)
            target=model.targets(magnitude,amount,balance,dropouts)
            untouched=(p.rate,p.stereo_phase,p.wear_stereo,p.enable)
            lib.rs_magnetic_macro_apply(C.byref(macro),magnitude,amount,balance,dropouts,C.byref(p))
            assert untouched==(p.rate,p.stereo_phase,p.wear_stereo,p.enable)
            for name,v in target.items(): exact(getattr(p,name),v,(label,tick,name))
            native.set(**values(p));assert lib.retro_magnetic_set(core,C.byref(p))
            if controls is None and tick==777:
                native.reset_smoothing();lib.retro_magnetic_reset_smoothing(core)
            left=[f(.6*math.sin((tick*64+i)*.071)) for i in range(64)]
            right=[f(.4*math.cos((tick*64+i)*.093)) for i in range(64)]
            if tick%97==13:left,right=[1.]+[0.]*63,[-1.]+[0.]*63
            if tick%97 in [14,15]:left,right=[0.]*64,[0.]*64
            expected=native.process(left,right)
            a,b=(C.c_float*64)(*left),(C.c_float*64)(*right)
            lib.retro_magnetic_process(core,a,b,64)
            for actual,ref in zip(list(a)+list(b),expected[0]+expected[1]):
                assert math.isfinite(actual) and actual==ref,(label,rate,seeds,tick,'audio',actual,ref)
            counts['ticks']+=1;counts['noise_outputs']+=4;counts['noise_state_bytes']+=160
            counts['targets']+=len(target);counts['audio_samples']+=128
        finally:lib.retro_magnetic_destroy(core)
        results.append(dict(case=label,sample_rate=rate,seeds=seeds,ticks=ticks,noise_ranges=ranges))
        counts['scenarios']+=1;del native,model;gc.collect()
    result=dict(scope='synthetic settled Magnetic resource relations + original Drive + stereo cFlutter kernel',
                reference_sha256=HASH,library=str(args.library.resolve()),counts=counts,
                max_control_error=0,max_audio_error=0,bit_exact_noise_state=True,results=results,
                limitations=['Explicit seeds; original production relation indices unresolved',
                    'Manually settled notifications before each64frame Drive/render; no full ModelStore scheduler',
                    'Synthetic constructor/prepare/cached model state, Linux libm, inactive visualization',
                    'Full original startup/reset and cross-platform reference equivalence unverified'])
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='results'}))
if __name__=='__main__':main()
