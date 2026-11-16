#!/usr/bin/env python3
"""Settled Classic resource chain composed from isolated original numerical methods.

No original constructor, graph scheduler or plugin host executes. External
routing is the study adapter's explicit constant-per-64-frame rule.
"""
import argparse
import ctypes as C
import gc
import json
import math
import random
import struct
from native_bitcolor_probe import ROOT,HASH
from native_magic_probe import f32 as f
from native_classic_probe import NativeClassic
from native_peak_eq_probe import NativePeakEq
from compare_native_gain import NativeGain
from compare_native_dist_chain import NativeFlux,Params,Flux

class Targets(C.Structure):
    _fields_=[(n,C.c_float) for n in ['scaled_amount','mix','input_gain','output_gain','pre_db','post_db']]+[(n,C.c_float*3) for n in ['eq_db','engine_amount','engine_db','output_hz','reference_db']]

class NativeChain:
    def __init__(self,rate,p):
        self.rate=rate;self.seed=int(p.seed)
        self.engines=[NativeClassic(rate,character=i) for i in range(3)]
        image=self.engines[0]
        stub=0x578c96
        slot=stub+6+struct.unpack_from('<i',image.image,stub+2)[0]
        struct.pack_into('<Q',image.image,slot,C.cast(image.libm.pow,C.c_void_p).value)
        self.flux=NativeFlux(image,self.seed)
        self.gains=[NativeGain() for _ in range(4)]
        self.eq=[[NativePeakEq(rate=rate,freq=850 if i==2 else 900,q=.2 if i==2 else .4,db=0) for _ in range(2)] for i in range(3)]
        self.update(p)
        for g in self.gains:g.prepare(rate)
        for pair in self.eq:
            for e in pair:e.reset()

    def map(self,a,lo,hi,k=.5):
        record=self.flux.map_record
        struct.pack_into('<ff',record,0xc,min(lo,hi),max(lo,hi))
        struct.pack_into('<ffi',record,0x14,lo,hi,0 if k==.5 else 5)
        struct.pack_into('<f',record,0x20,k)
        return self.flux.map_fn(record,a)

    def targets(self,p):
        shared=self.flux.targets(p);a=shared.scaled_amount
        return Targets(a,shared.mix,shared.input_gain,shared.output_gain,
            self.map(a,-19,0),self.map(a,19,0),
            (C.c_float*3)(self.map(a,10,12),self.map(a,10,12,.15),self.map(a,10,16)),
            (C.c_float*3)(self.map(a,0,100),self.map(a,0,100,.15),self.map(a,0,100)),
            (C.c_float*3)(self.map(a,0,-3),self.map(a,0,-3,.15),self.map(a,0,-1)),
            (C.c_float*3)(self.map(a,20000,5000),self.map(a,20000,7000,.15),self.map(a,10000,3000)),
            (C.c_float*3)(-10,-15,self.map(a,0,-6)))

    def update(self,p):
        t=self.target=self.targets(p)
        for g,db in zip(self.gains,[-12,12,t.pre_db,t.post_db]):g.set(1,db)
        for i,e in enumerate(self.engines):
            e.set(amount=t.engine_amount[i],character=i,low_hz=p.low_hz,high_hz=p.high_hz,mix=100,dist_db=t.engine_db[i],pass_db=0,output_hz=t.output_hz[i],reference_db=t.reference_db[i])
            for j in range(2):self.eq[i][j].set(freq=850 if i==2 else 900,q=.2 if i==2 else .4,db=t.eq_db[i] if j==0 else -t.eq_db[i])

    def tick(self,p):
        if self.seed!=int(p.seed):self.seed=int(p.seed);self.flux=NativeFlux(self.engines[0],self.seed)
        self.flux.tick(self.rate,p);self.update(p)

    def process(self,left,right,p):
        if not p.enable:return left,right
        t=self.target;gain=f(t.input_gain*t.mix);ch=int(p.character)
        l=[f(x*gain) for x in left];r=[f(x*gain) for x in right]
        for g in self.gains[::2]:l,r=g.process(l,r)
        l,r=self.eq[ch][0].process(l,r);l,r=self.engines[ch].process(l,r);l,r=self.eq[ch][1].process(l,r)
        for g in self.gains[3::-2]:l,r=g.process(l,r)
        dry=f(1-t.mix)
        return ([f(f(x*dry)+f(y*t.output_gain)) for x,y in zip(left,l)], [f(f(x*dry)+f(y*t.output_gain)) for x,y in zip(right,r)])

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--library',default='build-release/libretro_dsp.so');ap.add_argument('--report',default='analysis/re/native-classic-chain-comparison.json');ap.add_argument('--ticks',type=int,default=128);args=ap.parse_args()
    lib=C.CDLL(str((ROOT/args.library).resolve()))
    lib.retro_classic_chain_create.restype=C.c_void_p
    for name in ['destroy','reset']:getattr(lib,'retro_classic_chain_'+name).argtypes=[C.c_void_p]
    lib.retro_classic_chain_set.argtypes=[C.c_void_p,C.POINTER(Params)];lib.retro_classic_chain_prepare.argtypes=[C.c_void_p,C.c_float]
    lib.retro_classic_chain_process.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t]
    lib.rs_dist_flux_reset.argtypes=[C.POINTER(Flux),C.c_int32];lib.rs_dist_flux_tick.argtypes=[C.POINTER(Flux)]+[C.c_float]*4
    lib.rs_classic_chain_targets.argtypes=[C.POINTER(Flux),C.POINTER(Params),C.POINTER(Targets)]
    counts=dict(scenarios=0,ticks=0,audio_samples=0,target_values=0,noise_state_bytes=0)
    cases=[('zero',dict(amount=0)),('full',dict(flux=0)),('partial',dict(amount=.37,magnitude=.61,mix=.53,flux=.79)),('changing',None)]
    for rate in [44100.,48000.,96000.]:
     for seed in [0,65535]:
      for character in range(3):
       for label,initial in cases:
        p=Params(1,character,1,1,1,seed,20,20000,1)
        if initial:
            for k,v in initial.items():setattr(p,k,v)
        n=NativeChain(rate,p);s=lib.retro_classic_chain_create();macro=Flux();last_seed=int(p.seed);lib.rs_dist_flux_reset(C.byref(macro),last_seed)
        assert lib.retro_classic_chain_set(s,C.byref(p)) and lib.retro_classic_chain_prepare(s,rate)
        rng=random.Random(int(rate)+seed+character)
        try:
         for tick in range(args.ticks):
            if initial is None:
                if tick%17==0:
                    for k in ['amount','mix','magnitude','flux']:setattr(p,k,rng.random())
                    p.character=(tick//17)%3;p.low_hz=20+(tick%7)*117;p.high_hz=20000-(tick%13)*977
                p.enable=0 if 35<=tick%67<44 else 1
                if tick in [53,101]:p.seed=(int(p.seed)+97)%65536
            if tick==args.ticks//2:
                del n;gc.collect();n=NativeChain(rate,p);assert lib.retro_classic_chain_set(s,C.byref(p));lib.retro_classic_chain_reset(s)
                lib.rs_dist_flux_reset(C.byref(macro),int(p.seed));last_seed=int(p.seed)
            if last_seed!=int(p.seed):lib.rs_dist_flux_reset(C.byref(macro),int(p.seed));last_seed=int(p.seed)
            n.tick(p);lib.rs_dist_flux_tick(C.byref(macro),rate,p.magnitude,p.flux,p.amount)
            assert bytes(macro.generator)==n.flux.handler.raw[0x48:0x70],(label,rate,character,tick,'noise')
            target=Targets();lib.rs_classic_chain_targets(C.byref(macro),C.byref(p),C.byref(target))
            assert bytes(target)==bytes(n.target),(label,rate,character,tick,'targets',list(struct.unpack('<21f',bytes(target))),list(struct.unpack('<21f',bytes(n.target))))
            left=[f(.6*math.sin((tick*64+i)*.071)) for i in range(64)];right=[f(.4*math.cos((tick*64+i)*.093)) for i in range(64)]
            if tick%31==13:left,right=[1.]+[0.]*63,[-1.]+[0.]*63
            if tick%31 in [14,15]:left=right=[0.]*64
            el,er=n.process(left,right,p);l=(C.c_float*64)(*left);r=(C.c_float*64)(*right)
            assert lib.retro_classic_chain_set(s,C.byref(p));off=0
            for chunk in ([1,7,13,3,29,11] if tick%2 else [64]):
                lib.retro_classic_chain_process(s,C.cast(C.byref(l,off*4),C.POINTER(C.c_float)),C.cast(C.byref(r,off*4),C.POINTER(C.c_float)),chunk);off+=chunk
            for c,(a,b) in enumerate([(el,l),(er,r)]):
                for i,(x,y) in enumerate(zip(a,b)):assert math.isfinite(y) and struct.pack('<f',x)==struct.pack('<f',y),(label,rate,character,tick,c,i,x,y)
            counts['ticks']+=1;counts['audio_samples']+=128;counts['target_values']+=21;counts['noise_state_bytes']+=40
        finally:lib.retro_classic_chain_destroy(s)
        del n;gc.collect();counts['scenarios']+=1
    report=dict(reference_sha256=HASH,scope='Original Classic/BiQuad/Gain/Noise/parameter mapping kernels with settled external routing adapter',counts=counts,max_audio_error=0,max_target_error=0,noise_state_bit_exact=True,limitations=['No original constructor/graph scheduler/connection interpolation','Explicit study seeds; production relation indices unknown','Inactive histories freeze; controls latch per64frames','Linux x86 math and SSE; ARM bit identity unverified'])
    (ROOT/args.report).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
