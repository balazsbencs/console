#!/usr/bin/env python3
"""Peak EQ native setup/interpolation/bypass/process with arbitrary C partitions."""
import argparse
import ctypes as C
import json
import math
import random
import struct
from native_peak_eq_probe import NativePeakEq,ROOT,HASH


class Eq(C.Structure):
    _fields_=[('native',C.c_uint64*20),('batch_left',C.c_uint),('lane',C.c_uint),('batch_filter',C.c_uint),('batch_fade',C.c_uint),('fade_start',C.c_float),('fade_end',C.c_float),('fade_weight',C.c_float)]


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--library',default='build-release/libretro_dsp.so');ap.add_argument('--report',default='analysis/re/native-peak-eq-comparison.json');args=ap.parse_args()
    lib=C.CDLL(str((ROOT/args.library).resolve()));lib.rs_peak_eq_set.argtypes=[C.POINTER(Eq),C.c_float,C.c_float,C.c_float];lib.rs_peak_eq_prepare.argtypes=[C.POINTER(Eq),C.c_float];lib.rs_peak_eq_reset.argtypes=[C.POINTER(Eq)];lib.rs_peak_eq_process.argtypes=[C.POINTER(Eq),C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t]
    rates=[1000,8000,44100,48000,48001,96000,192000,1536000]
    controls=[(900,.4,12),(900,.4,-12),(850,.2,16),(850,.2,-16),(20,.1,0),(20000,10,24),(900,10000,100),(850,.01,-100)]
    cases=samples=state_bytes=0
    n=NativePeakEq()
    for rate in rates:
     for freq,q,db in controls:
      for dynamic in [False,True]:
        n.state=C.create_string_buffer(160);struct.pack_into('<i',n.state,8,4);n.controls=tuple(C.c_float(v).value for v in [freq,q,db]);struct.pack_into('<3f',n.state,0x10,freq,q,db);n.prepare(rate)
        s=Eq();assert lib.rs_peak_eq_set(C.byref(s),freq,q,db);assert lib.rs_peak_eq_prepare(C.byref(s),rate)
        assert bytes(s.native)==n.state.raw,('prepare',rate,freq,q,db)
        rng=random.Random(rate);p=[freq,q,db]
        for block in range(256):
            if dynamic and block in [2,6,20,32,64,96,112,160,191]:
                if block==2:p[2]=0
                elif block==6:p[2]=-12
                elif block==20:p[2]=16
                elif block==32:p[0]=20;p[1]=.2
                elif block==64:p[0]=19000;p[1]=3
                elif block==96:p[2]=0
                elif block==112:p[2]=.00001
                elif block==160:p=[850,.2,16]
                else:p=[900,.4,-12]
                n.set(*p);assert lib.rs_peak_eq_set(C.byref(s),*p)
                assert bytes(s.native)==n.state.raw,('set',rate,freq,q,db,block)
            if block==128:n.reset();lib.rs_peak_eq_reset(C.byref(s))
            left=[C.c_float(math.sin((block*64+i)*.071)*.7).value for i in range(64)];right=[C.c_float(rng.uniform(-1,1)).value for i in range(64)]
            if block%17 in [3,4]:left=right=[0.]*64
            expected=n.process(left,right);l=(C.c_float*64)(*left);r=(C.c_float*64)(*right);off=0
            for chunk in ([1,7,13,3,29,11] if block%2 else [64]):
                lib.rs_peak_eq_process(C.byref(s),C.cast(C.byref(l,off*4),C.POINTER(C.c_float)),C.cast(C.byref(r,off*4),C.POINTER(C.c_float)),chunk);off+=chunk
            for c,(a,b) in enumerate(zip(expected,[l,r])):
                for i,(x,y) in enumerate(zip(a,b)):assert math.isfinite(y) and struct.pack('<f',x)==struct.pack('<f',y),('audio',rate,freq,q,db,dynamic,block,c,i,x,y)
            if bytes(s.native)!=n.state.raw:
                dif=[(hex(i),bytes(s.native)[i],n.state.raw[i]) for i in range(160) if bytes(s.native)[i]!=n.state.raw[i]]
                raise AssertionError(('state',rate,freq,q,db,dynamic,block,dif))
            samples+=128;state_bytes+=160
        cases+=1
    report=dict(reference_sha256=HASH,scope='Original peak type4 UpdatePrivates/ResetAudio/SoftReset/Process64, synthetic160byteobject, atomic physical control batches',scenarios=cases,samples=samples,state_bytes=state_bytes,max_error=0,state_bit_exact=True,sample_rates=rates,controls=controls,irregular_partitions=[1,7,13,3,29,11],limitations=['Original silence shortcut disabled','No full plugin or original constructor','Atomic changed control batch; separate parameter notifications not simulated','Linux libm for original and C'])
    path=ROOT/args.report;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
