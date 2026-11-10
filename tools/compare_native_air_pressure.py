#!/usr/bin/env python3
"""Compare isolated AirPressure numeric setup/process with synthetic delay rings."""
import argparse
import ctypes as C
import gc
import json
import math
from pathlib import Path
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH
class Air(C.Structure):
    _fields_=[('state',C.c_float*36),('ring',C.POINTER(C.c_float)*2),
        ('mask',C.c_uint32),('position',C.c_uint32*2),('delay',C.c_float),
        ('inverse_delay',C.c_float),('sample_rate',C.c_float)]
def f(x):return C.c_float(x).value
class NativeAir(NativeBitColor):
    def __init__(self,rate,amount):
        super().__init__()
        for stub,name in [(0x578a2c,'exp'),(0x57895a,'atan'),(0x578c96,'pow')]:
            slot=stub+6+struct.unpack_from('<i',self.image,stub+2)[0]
            struct.pack_into('<Q',self.image,slot,C.cast(getattr(self.libm,name),C.c_void_p).value)
        self.state=C.create_string_buffer(0xb8)
        self.setup_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_float,C.c_float)(self.base+0xb1cf0)
        self.process_fn=C.CFUNCTYPE(C.c_float,C.c_void_p,C.c_float,C.c_int)(self.base+0xb22f0)
        capacity=4
        while capacity<int(f(rate*f(2.2675736545352265e-05)))+4:capacity*=2
        self.rings=[(C.c_float*capacity)(),(C.c_float*capacity)()]
        for c in range(2):struct.pack_into('<QII',self.state,0x90+c*16,C.addressof(self.rings[c]),capacity-1,0)
        self.setup_fn(self.state,rate,amount)
    def sample(self,x,c):return self.process_fn(self.state,x,c)
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--library',type=Path,default=ROOT/'build-release/libretro_dsp.so')
    ap.add_argument('--report',type=Path,default=ROOT/'analysis/re/native-air-pressure-comparison.json')
    args=ap.parse_args();lib=C.CDLL(str(args.library.resolve()))
    lib.rs_air_pressure_prepare.argtypes=[C.POINTER(Air),C.c_float,C.c_float]
    lib.rs_air_pressure_setup.argtypes=[C.POINTER(Air),C.c_float,C.c_float]
    lib.rs_air_pressure_process.argtypes=[C.POINTER(Air),C.c_float,C.c_uint]
    lib.rs_air_pressure_process.restype=C.c_float
    lib.rs_air_pressure_reset.argtypes=[C.POINTER(Air)]
    lib.rs_air_pressure_destroy.argtypes=[C.POINTER(Air)]
    samples=states=setups=0
    for rate in [1000.,8000.,44100.,48000.,96000.,192000.,384000.,1536000.]:
      for amount in [0.,.001,.1,.25,.5,.75,1.,None]:
        n=NativeAir(rate,amount or 0);s=Air();assert lib.rs_air_pressure_prepare(C.byref(s),rate,amount or 0)
        rng=random.Random(int(rate))
        try:
          for i in range(2048):
            if amount is None and i%17==0:
                a=f(rng.random());n.setup_fn(n.state,rate,a);lib.rs_air_pressure_setup(C.byref(s),rate,a);setups+=1
            if i==1024:
                # Full study reset: native comparison uses fresh synthetic histories.
                a=amount if amount is not None else a
                del n;gc.collect();n=NativeAir(rate,a);lib.rs_air_pressure_reset(C.byref(s))
            for c in range(2):
                x=f(.7*math.sin(i*(.071+c*.02)))
                if i<12:x=f([0.,-0.,1.,-1.,10.,-10.,100.,-100.,1e-10,-1e-10,.1,-.1][i])
                if 256<=i<384:x=0.
                if i>=1536:x=f(rng.uniform(-2,2))
                expect=n.sample(x,c);actual=lib.rs_air_pressure_process(C.byref(s),x,c)
                assert math.isfinite(actual) and struct.pack('<f',actual)==struct.pack('<f',expect),(rate,amount,i,c,actual,expect)
                assert bytes(s.state)==n.state.raw[:0x90],(rate,amount,i,c,'state')
                samples+=1;states+=1
            for c in range(2):
                assert C.string_at(s.ring[c],(s.mask+1)*4)==bytes(n.rings[c]),(rate,amount,i,c,'ring')
        finally:lib.rs_air_pressure_destroy(C.byref(s))
        del n;gc.collect()
    report=dict(reference_sha256=HASH,scope='isolated airPressure numeric setup/process; synthetic zero histories and delay rings; full study reset compared with fresh native state',scenarios=64,samples=samples,state_comparisons=states,changing_setups=setups,max_error=0,all_float_patterns_equal=True,
        limitations=['No original magicDistort outer split/mix/oversampling or complete plugChainDist','Synthetic capacity chosen to cover interpolation taps; original allocation/constructor not run','Linux libm bound for reference and C'])
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
