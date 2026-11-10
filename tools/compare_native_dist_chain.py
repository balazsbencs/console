#!/usr/bin/env python3
"""Settled Dist resource targets + original NoiseGen Drive/Gain/Magic kernels.

External dry/wet and character routing is an explicit constant-per-batch adapter;
this does not execute or establish the complete original graph/scheduler.
"""
import argparse
import ctypes as C
import gc
import json
import math
import random
import struct
from native_bitcolor_probe import ROOT, HASH
from native_magic_probe import NativeMagic, f32 as f
from compare_native_gain import NativeGain
from compare_native_magnetic_macro import NativeMacro


NAMES=['amount','character','mix','magnitude','flux','seed','low_hz','high_hz','enable']
class Params(C.Structure): _fields_=[(n,C.c_float) for n in NAMES]
class Flux(C.Structure): _fields_=[('generator',C.c_ubyte*40),('output',C.c_float)]
class Targets(C.Structure):
    _fields_=[(n,C.c_float) for n in ['scaled_amount','mix','input_gain','output_gain','tube_db','iron_db']]+[('engine_amount',C.c_float*3)]


class NativeFlux(NativeMacro):
    def __init__(self,image,seed):
        self.image=image
        self.guard=C.c_uint64(0x3141592653589793)
        struct.pack_into('<Q',image.image,0x73a228,C.addressof(self.guard))
        self.handler=C.create_string_buffer(0x15030);self.store=C.create_string_buffer(8);self.vtable=C.create_string_buffer(0x40)
        self.values={};values=self.values
        self.getter=C.CFUNCTYPE(C.c_float,C.c_void_p,C.c_int)(lambda _,key:values[key])
        self.setter=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_float)(lambda _,key,value:values.__setitem__(key,value))
        struct.pack_into('<Q',self.store,0,C.addressof(self.vtable));struct.pack_into('<Q',self.handler,0x10,C.addressof(self.store))
        for off,cb in [(0x38,self.getter),(0x30,self.setter)]:struct.pack_into('<Q',self.vtable,off,C.cast(cb,C.c_void_p).value)
        self.changed=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_int)(image.base+0xf0d70)
        self.drive=C.CFUNCTYPE(None,C.c_void_p,C.c_float)(image.base+0xd6fd0)
        self.reset_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_int32)(image.base+0x1a5980)
        struct.pack_into('<6i',self.handler,0x20,10,11,14,13,12,-1)
        struct.pack_into('<if',self.handler,0x40,seed,-1.)
        struct.pack_into('<i',self.handler,0x80,ord('O'))
        struct.pack_into('<Q',self.handler,0x88,0)
        struct.pack_into('<Q',self.handler,0x15020,C.addressof(self.handler)+0x20)
        self.reset_fn(C.addressof(self.handler)+0x48,seed)
        self.values[14]=0.
        self.map_fn=C.CFUNCTYPE(C.c_float,C.c_void_p,C.c_float)(image.base+0x9f090)
        self.map_record=C.create_string_buffer(0x40)
        struct.pack_into('<i',self.map_record,8,1)

    def tick(self,rate,p):
        group=self.relation('M',p.magnitude,p.flux,p.amount,(.5,.5,.99))
        gain=self.relation('F',group,params=(0.,.16,.36,.5,.5))
        smooth=self.relation('F',group,params=(.913,.913,.1,.5,.5))
        self.values.update({10:f(gain),11:0.,12:f(smooth),13:f(.405)})
        self.changed(self.handler,0,0);self.drive(self.handler,f(rate*f(.015625)))

    def map(self,x,high,curve=.5):
        struct.pack_into('<ff',self.map_record,0xc,0.,high)
        struct.pack_into('<ffi',self.map_record,0x14,0.,high,0 if curve==.5 else 5)
        struct.pack_into('<f',self.map_record,0x20,curve)
        return self.map_fn(self.map_record,x)

    def targets(self,p):
        a=self.relation('m',p.magnitude,p.amount)
        mix=self.relation('m',p.mix,self.relation('f',a,params=(0.,1.,0.,1.,.99)))
        feed=self.relation('b',.5,self.values[14],.75)
        pre=self.relation('K',feed,params=(20.,-3.,.5,.5))
        post=self.relation('K',feed,params=(-20.,3.,.5,.5))
        return Targets(a,mix,pre,post,self.map(a,22),self.map(a,18,.4),
                       (C.c_float*3)(self.map(a,100),self.map(a,75),self.map(a,100,.4)))


class NativeChain:
    def __init__(self,rate,p):
        self.rate=rate;self.seed=int(p.seed)
        self.engines=[NativeMagic(rate,character=i) for i in range(3)]
        self.gains=[NativeGain() for _ in range(6)]
        self.flux=NativeFlux(self.engines[0],self.seed)
        self.update(self.flux.targets(p),p)
        for g in self.gains:g.prepare(rate)

    def update(self,t,p):
        self.target=t
        for g,db in zip(self.gains,[-12,12,t.tube_db,-t.tube_db,t.iron_db,-t.iron_db]):g.set(1,db)
        for i,e in enumerate(self.engines):e.set(amount=t.engine_amount[i],character=i,low_hz=p.low_hz,high_hz=p.high_hz,mix=100,dist_db=0,pass_db=0,output_hz=20000)

    def tick(self,p):
        if self.seed!=int(p.seed):self.seed=int(p.seed);self.flux=NativeFlux(self.engines[0],self.seed)
        self.flux.tick(self.rate,p);self.update(self.flux.targets(p),p)

    def process(self,left,right,p):
        if not p.enable:return left,right
        t=self.target;gain=f(t.input_gain*t.mix)
        l=[f(x*gain) for x in left];r=[f(x*gain) for x in right]
        l,r=self.gains[0].process(l,r)
        character=int(p.character)
        if character==0:l,r=self.gains[2].process(l,r)
        if character==2:l,r=self.gains[4].process(l,r)
        l,r=self.engines[character].process(l,r)
        if character==0:l,r=self.gains[3].process(l,r)
        if character==2:l,r=self.gains[5].process(l,r)
        l,r=self.gains[1].process(l,r)
        dry=f(1-t.mix)
        return ([f(f(x*dry)+f(y*t.output_gain)) for x,y in zip(left,l)],
                [f(f(x*dry)+f(y*t.output_gain)) for x,y in zip(right,r)])


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--library',default='build-release/libretro_dsp.so');ap.add_argument('--report',default='analysis/re/native-dist-chain-comparison.json');ap.add_argument('--static-ticks',type=int,default=256);ap.add_argument('--dynamic-ticks',type=int,default=1024);args=ap.parse_args()
    lib=C.CDLL(str((ROOT/args.library).resolve()))
    lib.retro_dist_chain_create.restype=C.c_void_p;lib.retro_dist_chain_destroy.argtypes=[C.c_void_p]
    lib.retro_dist_chain_set.argtypes=[C.c_void_p,C.POINTER(Params)];lib.retro_dist_chain_prepare.argtypes=[C.c_void_p,C.c_float];lib.retro_dist_chain_reset.argtypes=[C.c_void_p]
    lib.retro_dist_chain_process.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t]
    lib.rs_dist_flux_reset.argtypes=[C.POINTER(Flux),C.c_int32]
    lib.rs_dist_flux_tick.argtypes=[C.POINTER(Flux)]+[C.c_float]*4
    lib.rs_dist_flux_targets.argtypes=[C.POINTER(Flux),C.POINTER(Params),C.POINTER(Targets)]
    counts=dict(scenarios=0,ticks=0,audio_samples=0,noise_state_bytes=0,target_values=0);results=[]
    cases=[('flux_zero',dict(flux=0)),('amount_zero',dict(amount=0)),('magnitude_zero',dict(magnitude=0)),('full',dict()),('partial',dict(amount=.37,magnitude=.61,mix=.53,flux=.79)),('changing',None)]
    for rate in [44100.,48000.,96000.]:
     for seed in [0,65535]:
      for character in range(3):
       for label,initial in cases:
        p=Params(1,character,1,1,1,seed,20,20000,1)
        if initial:
            for k,v in initial.items():setattr(p,k,v)
        n=NativeChain(rate,p);s=lib.retro_dist_chain_create();macro=Flux();lib.rs_dist_flux_reset(C.byref(macro),seed)
        assert lib.retro_dist_chain_set(s,C.byref(p)) and lib.retro_dist_chain_prepare(s,rate)
        rng=random.Random(int(rate)+seed+character);ticks=args.dynamic_ticks if initial is None else args.static_ticks
        low=math.inf;high=-math.inf;last_seed=seed
        try:
         for tick in range(ticks):
            if initial is None:
                if tick%31==0:
                    for k in ['amount','mix','magnitude','flux']:setattr(p,k,rng.random())
                    p.character=(tick//31)%3;p.low_hz=20+(tick%7)*117;p.high_hz=20000-(tick%13)*977
                p.enable=0 if 100<=tick%211<135 else 1
                if tick in [173,511]:p.seed=(int(p.seed)+97)%65536
            if tick==ticks//2:
                del n;gc.collect();n=NativeChain(rate,p);lib.retro_dist_chain_set(s,C.byref(p));lib.retro_dist_chain_reset(s)
                lib.rs_dist_flux_reset(C.byref(macro),int(p.seed));last_seed=int(p.seed)
            if last_seed!=int(p.seed):lib.rs_dist_flux_reset(C.byref(macro),int(p.seed));last_seed=int(p.seed)
            n.tick(p);lib.rs_dist_flux_tick(C.byref(macro),rate,p.magnitude,p.flux,p.amount)
            assert bytes(macro.generator)==n.flux.handler.raw[0x48:0x70],(label,rate,seed,character,tick,'noise state')
            assert struct.pack('<f',macro.output)==struct.pack('<f',n.flux.values[14]),(label,tick,'noise output')
            t=Targets();lib.rs_dist_flux_targets(C.byref(macro),C.byref(p),C.byref(t))
            assert bytes(t)==bytes(n.target),(label,rate,seed,character,tick,'targets',list(struct.unpack('<9f',bytes(t))),list(struct.unpack('<9f',bytes(n.target))))
            low=min(low,macro.output);high=max(high,macro.output)
            left=[f(.6*math.sin((tick*64+i)*.071)) for i in range(64)];right=[f(.4*math.cos((tick*64+i)*.093)) for i in range(64)]
            if tick%97==13:left,right=[1.]+[0.]*63,[-1.]+[0.]*63
            if tick%97 in [14,15]:left=right=[0.]*64
            el,er=n.process(left,right,p);l=(C.c_float*64)(*left);r=(C.c_float*64)(*right)
            assert lib.retro_dist_chain_set(s,C.byref(p));off=0
            for chunk in ([1,7,13,3,29,11] if tick%2 else [64]):
                lib.retro_dist_chain_process(s,C.cast(C.byref(l,off*4),C.POINTER(C.c_float)),C.cast(C.byref(r,off*4),C.POINTER(C.c_float)),chunk);off+=chunk
            for c,(a,b) in enumerate([(el,l),(er,r)]):
                for i,(x,y) in enumerate(zip(a,b)):assert math.isfinite(y) and struct.pack('<f',x)==struct.pack('<f',y),(label,rate,seed,character,tick,c,i,x,y)
            counts['ticks']+=1;counts['audio_samples']+=128;counts['noise_state_bytes']+=40;counts['target_values']+=9
        finally:lib.retro_dist_chain_destroy(s)
        results.append(dict(case=label,rate=rate,seed=seed,initial_character=character,ticks=ticks,flux_range=[low,high]));counts['scenarios']+=1
        del n;gc.collect()
    report=dict(reference_sha256=HASH,scope='Settled resource targets + original one-generator Drive + Gain/Magic kernels; explicit external constant-per-batch dry/wet/character routing adapter',counts=counts,max_audio_error=0,max_target_error=0,noise_state_bit_exact=True,results=results,
                limitations=['Explicit seeds; production relation indices unresolved','No full original graph/scheduler or gain-transition scheduler','Three engine histories freeze when inactive; whole audio freezes while disabled, Flux advances','No oversampling, original constructor/startup or plugin host','Linux libm for reference and C'])
    path=ROOT/args.report;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='results'}))
if __name__=='__main__':main()
