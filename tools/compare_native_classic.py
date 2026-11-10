#!/usr/bin/env python3
"""Full numerical Classic Distortion comparison against isolated original methods."""
import argparse
import ctypes as C
import gc
import json
import math
import random
import struct
from native_classic_probe import NativeClassic,NAMES,ROOT,HASH


class Params(C.Structure):_fields_=[(n,C.c_float) for n in NAMES]


class Private(C.Structure):
    # Read-only fixture view matching the inspected private struct in classic.c.
    _fields_=[('p',Params),('rate',C.c_float),('split',C.c_float*6),
              ('gains',C.c_float*4),('wave',C.c_float*4),
              ('output_coefficient',C.c_float),('output',C.c_float*2),('remaining',C.c_uint)]


def exact_state(pointer,native,context):
    s=C.cast(pointer,C.POINTER(Private)).contents
    pieces=[(bytes(s.gains),native.state.raw[0x38:0x48],'gains'),
            (bytes(s.split),native.state.raw[0x48:0x60],'split'),
            (bytes(s.wave),native.state.raw[0x60:0x70],'wave'),
            (struct.pack('<f',s.output_coefficient),native.state.raw[0x74:0x78],'output coefficient'),
            (bytes(s.output),native.state.raw[0x78:0x80],'output state')]
    for a,b,label in pieces:
        assert a==b,(context,label,struct.unpack('<'+'f'*(len(a)//4),a),struct.unpack('<'+'f'*(len(b)//4),b))
    return sum(len(a)//4 for a,_,_ in pieces)



def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--library',default='build-release/libretro_dsp.so');ap.add_argument('--report',default='analysis/re/native-classic-comparison.json');args=ap.parse_args()
    lib=C.CDLL(str((ROOT/args.library).resolve()))
    lib.retro_classic_create.restype=C.c_void_p;lib.retro_classic_destroy.argtypes=[C.c_void_p]
    lib.retro_classic_set.argtypes=[C.c_void_p,C.POINTER(Params)];lib.retro_classic_prepare.argtypes=[C.c_void_p,C.c_float];lib.retro_classic_reset.argtypes=[C.c_void_p]
    lib.retro_classic_process.argtypes=[C.c_void_p,C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t]
    rates=[1000,8000,22050,44100,44117,48000,48001,96000,192000,1536000]
    cases=[dict(),dict(amount=0,mix=0),dict(amount=100,mix=100,reference_db=-18),
           dict(amount=13,low_hz=8000,high_hz=1500,mix=30,dist_db=-15,pass_db=4,output_hz=600,reference_db=12),
           dict(amount=75,low_hz=200,high_hz=5000,mix=72,dist_db=100,pass_db=-100,output_hz=20,reference_db=22),
           dict(amount=1,low_hz=20000,high_hz=20,mix=99,dist_db=-100,pass_db=100,output_hz=20000,reference_db=-22),
           dict(amount=100,reference_db=100),dict(amount=0,reference_db=-100)]
    scenarios=samples=state_values=0
    for rate in rates:
     for character in range(3):
      for variation,values in enumerate(cases):
        p=dict(amount=50,character=character,low_hz=20,high_hz=20000,dist_db=0,pass_db=0,mix=100,output_hz=20000,reference_db=0);p.update(values)
        n=NativeClassic(rate,**p);s=lib.retro_classic_create()
        assert lib.retro_classic_set(s,C.byref(Params(**p))) and lib.retro_classic_prepare(s,rate)
        state_values+=exact_state(s,n,(rate,character,variation,'setup'))
        rng=random.Random(rate+character+variation)
        try:
         for block in range(64):
            if block in [8,16,24,40]:
                if block==8:p.update(amount=31,mix=61,dist_db=-2.5,pass_db=2,reference_db=-7)
                elif block==16:p.update(low_hz=122,high_hz=7222,output_hz=933)
                elif block==24:p.update(character=(character+1)%3,amount=82,reference_db=9)
                else:p.update(character=character,amount=5,reference_db=0)
                n.set(**p);assert lib.retro_classic_set(s,C.byref(Params(**p)))
            if block==32:
                n.reset();lib.retro_classic_reset(s)
            left=[C.c_float(math.sin((block*64+i)*.071)*.7).value for i in range(64)]
            right=[C.c_float(math.cos((block*64+i)*.103)*.53).value for i in range(64)]
            if block%7==0:
                left=[C.c_float(rng.uniform(-1.2,1.2)).value for _ in range(64)]
                right=[C.c_float(rng.uniform(-.9,.9)).value for _ in range(64)]
            if block in [14,15,60,61,62,63]:left=right=[0.]*64
            el,er=n.process(left,right);l=(C.c_float*64)(*left);r=(C.c_float*64)(*right)
            off=0
            for count in ([1,7,13,3,29,11] if block%2 else [64]):
                lib.retro_classic_process(s,C.cast(C.byref(l,off*4),C.POINTER(C.c_float)),C.cast(C.byref(r,off*4),C.POINTER(C.c_float)),count);off+=count
            for c,(a,b) in enumerate([(el,l),(er,r)]):
                for i,(x,y) in enumerate(zip(a,b)):
                    assert math.isfinite(y) and struct.pack('<f',x)==struct.pack('<f',y),(rate,character,variation,block,c,i,x,y)
                    samples+=1
            state_values+=exact_state(s,n,(rate,character,variation,block))
        finally:lib.retro_classic_destroy(s)
        del n;gc.collect();scenarios+=1
    report=dict(reference_sha256=HASH,scope='Full numerical Classic stereo CalcInternals/Process64 with synthetic128byte object and verifiedmask copies',scenarios=scenarios,samples=samples,state_float_comparisons=state_values,state_bit_exact=True,maximum_absolute_error=0,all_float_patterns_equal=True,sample_rates=rates,initial_parameter_variations=cases,dynamic_controls=True,character_changes=True,reset=True,irregular_partitions=[1,7,13,3,29,11],limitations=['No original constructor/globalinitializer/fullpluginhost','Native silence shortcut disabled','Linux libm and x86 SSE rounding model','No full Dist chain or oversampling'])
    path=ROOT/args.report;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
