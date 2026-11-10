#!/usr/bin/env python3
"""Isolated original cAEGainPlug numerical reset/set/process comparison."""
import argparse
import ctypes as C
import json
import math
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


class Gain(C.Structure):
    _fields_ = [(name, C.c_float) for name in ['linear_gain','db','current','target','coefficient','block_coefficient','block_target','group_start','group_delta']] + [(name,C.c_uint) for name in ['remaining','lane','smoothing']]


class NativeGain(NativeBitColor):
    def __init__(self):
        super().__init__();self.state=C.create_string_buffer(0x20)
        # b3808/b380f copy this constant into BSS in normal global startup.
        # Copy only those verified 16 bytes, without running any initializer.
        assert struct.unpack_from('<4f',self.image,0x5e1070)==(0,.25,.5,.75)
        self.image[0x789cc0:0x789cd0]=self.image[0x5e1070:0x5e1080]
        libc=C.CDLL(None)
        slot=0x578c1e+6+struct.unpack_from('<i',self.image,0x578c1e+2)[0]
        struct.pack_into('<Q',self.image,slot,C.cast(libc.memset,C.c_void_p).value)
        self.set_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_float)(self.base+0xa9cc0)
        self.reset_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_float,C.c_int,C.c_bool)(self.base+0xa9c10)
        self.gain_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_bool,C.POINTER(C.c_bool),C.POINTER(C.c_float),C.POINTER(C.c_float))(self.base+0xa9d30)
    def set(self,linear,db):self.set_fn(self.state,0,linear);self.set_fn(self.state,1,db)
    def prepare(self,rate):self.reset_fn(self.state,rate,64,False)
    def process(self,left,right,mono=False):
        l=(C.c_float*64)(*left);r=l if mono else (C.c_float*64)(*right)
        assert C.addressof(l)%16==C.addressof(r)%16==0
        silent=C.c_bool(False);self.gain_fn(self.state,64,False,C.byref(silent),l,r)
        return list(l),list(r)
    def state4(self):return struct.unpack_from('<4f',self.state,0x10)


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--library',default='build-release/libretro_dsp.so');ap.add_argument('--report',default='analysis/re/native-gain-comparison.json');args=ap.parse_args()
    lib=C.CDLL(str((ROOT/args.library).resolve()))
    lib.rs_gain_set.argtypes=[C.POINTER(Gain),C.c_float,C.c_float]
    lib.rs_gain_prepare.argtypes=[C.POINTER(Gain),C.c_float]
    lib.rs_gain_reset.argtypes=[C.POINTER(Gain)]
    lib.rs_gain_process.argtypes=[C.POINTER(Gain),C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t]
    n=NativeGain();samples=cases=0
    rates=[1000,8000,22050,44100,44117,48000,48001,96000,192000,1536000]
    starts=[(1,0),(1,-100),(1,100),(-1,12),(0,0),(1,22),(1,-18),(1,12),(100,100),(-100,-100),(1e-10,0),(1e-9,0)]
    for rate in rates:
      for mono in [False,True]:
       for linear,db in starts:
        s=Gain();n.set(linear,db);n.prepare(rate)
        assert lib.rs_gain_set(C.byref(s),linear,db);assert lib.rs_gain_prepare(C.byref(s),rate)
        assert n.state.raw[0x10:0x20]==struct.pack('<4f',s.current,s.target,s.coefficient,s.block_coefficient),('setup',rate,linear,db,n.state4(),s.current,s.target)
        rng=random.Random(rate)
        for block in range(128):
            if block in [2,17,33,65,88,100]:
                linear,db={2:(1,0),17:(1,22),33:(1,-22),65:(-1,18),88:(0,0),100:(1,-100)}[block]
                n.set(linear,db);lib.rs_gain_set(C.byref(s),linear,db)
            if block==72:
                n.prepare(rate);lib.rs_gain_reset(C.byref(s))
            left=[C.c_float(rng.uniform(-2,2)).value for _ in range(64)]
            right=[C.c_float(math.cos((block*64+i)*.071)).value for i in range(64)]
            if block%13==0:left=right=[0.,-0.,1.,-1.]*16
            el,er=n.process(left,right,mono)
            l=(C.c_float*64)(*left);r=l if mono else (C.c_float*64)(*right)
            off=0
            for count in ([1,7,13,3,29,11] if block%2 else [64]):
                lib.rs_gain_process(C.byref(s),C.cast(C.byref(l,off*4),C.POINTER(C.c_float)),C.cast(C.byref(r,off*4),C.POINTER(C.c_float)),count);off+=count
            for c,(a,b) in enumerate([(el,l),(er,r)]):
                for i,(x,y) in enumerate(zip(a,b)):
                    assert struct.pack('<f',x)==struct.pack('<f',y),(rate,mono,linear,db,block,c,i,x,y)
                    samples+=1
            assert n.state.raw[0x10:0x20]==struct.pack('<4f',s.current,s.target,s.coefficient,s.block_coefficient),('state',rate,block,n.state4(),s.current,s.target)
        cases+=1
    report=dict(reference_sha256=HASH,scope='Isolated cAEGainPlug numerical reset/set/stereo-or-mono Process64; verified SIMD interpolation constant copied, no constructors',scenarios=cases,samples=samples,maximum_absolute_error=0,state_bit_exact=True,sample_rates=rates,initial_gain_pairs=starts,irregular_partitions=[1,7,13,3,29,11],reset=True,dynamic_changes=True,limitations=['Silence shortcut disabled','No full chain or plugin host','Linux libm for reference and C'])
    path=ROOT/args.report;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
