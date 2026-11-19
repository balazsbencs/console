#!/usr/bin/env python3
"""Original sidechain cSimpleComp64 versus C with arbitrary host partitions."""
import argparse
import ctypes as C
import json
import math
import random
import struct
from native_noise_comp_probe import NativeNoiseComp,NAMES,DEFAULTS,ROOT,HASH
class Params(C.Structure):_fields_=[(key,C.c_float) for key in NAMES]
class Comp(C.Structure):_fields_=[('native',C.c_ubyte*96),('p',Params),('active',Params),('rate',C.c_float),('threshold_db',C.c_float),('ratio_coefficient',C.c_float),('gain_offset',C.c_float),('remaining',C.c_uint)]
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--library',default='build-release/libretro_dsp.so');ap.add_argument('--report',default='analysis/re/native-noise-comp-comparison.json');args=ap.parse_args()
 lib=C.CDLL(str((ROOT/args.library).resolve()));lib.rs_noise_comp_set.argtypes=[C.POINTER(Comp),C.POINTER(Params)];lib.rs_noise_comp_prepare.argtypes=[C.POINTER(Comp),C.c_float];lib.rs_noise_comp_reset.argtypes=[C.POINTER(Comp)];lib.rs_noise_comp_process.argtypes=[C.POINTER(Comp)]+[C.POINTER(C.c_float)]*4+[C.c_size_t]
 rates=[1000,22050,44100,44117,48000,48001,96000,192000,1536000]
 scenes=[{},dict(threshold=1),dict(threshold=.7,averaging=1),dict(threshold=.5,ratio=1,attack=1,release=1,gain_comp=1),dict(highpass=0,threshold=1,ratio=0),dict(enable=0),dict(threshold=.1,ratio=.01,attack=.01,release=.01,averaging=.3,gain_comp=.4)]
 samples=cases=statebytes=0
 for rate in rates:
  for scene in scenes:
   for dynamic in [False,True]:
    p=dict(DEFAULTS,**scene);n=NativeNoiseComp(rate,**p);s=Comp();assert lib.rs_noise_comp_set(C.byref(s),C.byref(Params(**p)));assert lib.rs_noise_comp_prepare(C.byref(s),rate);rng=random.Random(rate)
    assert bytes(s.native)==n.state.raw[0x338:0x398],('prepare',rate,scene)
    for block in range(128):
     if dynamic and block in [2,8,16,24,32,40,55,80,96,112]:
      changes={2:dict(threshold=.7),8:dict(attack=.7,release=.8),16:dict(averaging=1),24:dict(highpass=0),32:dict(gain_comp=1,ratio=1),40:dict(enable=0),55:dict(enable=1,highpass=1),80:dict(averaging=0,threshold=1),96:dict(ratio=0),112:dict(threshold=0,gain_comp=0)}[block]
      p.update(changes);n.set(**changes);assert lib.rs_noise_comp_set(C.byref(s),C.byref(Params(**p)))
     if block==64:n.reset();lib.rs_noise_comp_reset(C.byref(s))
     left=[C.c_float(math.sin((block*64+i)*.071)*.7).value for i in range(64)];right=[C.c_float(rng.uniform(-1,1)).value for i in range(64)]
     scale=[0,1e-10,.01,.3,1,4,100][block%7]
     sl=[C.c_float(math.sin((block*64+i)*.017)*scale).value for i in range(64)];sr=[C.c_float(rng.uniform(-scale,scale)).value for i in range(64)]
     if scale==0:sl=sr=[0.]*64
     expected=n.process(left,right,sl,sr);l=(C.c_float*64)(*left);r=(C.c_float*64)(*right);a=(C.c_float*64)(*sl);b=(C.c_float*64)(*sr);off=0
     for chunk in ([1,7,13,3,29,11] if block%2 else [64]):
      ptr=lambda x:C.cast(C.byref(x,off*4),C.POINTER(C.c_float))
      lib.rs_noise_comp_process(C.byref(s),ptr(l),ptr(r),ptr(a) if scale else None,ptr(b) if scale else None,chunk);off+=chunk
     for c,(x,y) in enumerate(zip(expected,[l,r])):
      for i,(u,v) in enumerate(zip(x,y)):assert math.isfinite(v) and struct.pack('<f',u)==struct.pack('<f',v),('audio',rate,scene,dynamic,block,c,i,u,v)
     ns=n.state.raw[0x338:0x398];cs=bytes(s.native)
     if ns!=cs:
      dif=[(hex(0x338+i),struct.unpack_from('<f',ns,i)[0],struct.unpack_from('<f',cs,i)[0]) for i in range(0,96,4) if ns[i:i+4]!=cs[i:i+4]]
      raise AssertionError(('state',rate,scene,dynamic,block,dif))
     samples+=128;statebytes+=96
    cases+=1
 report=dict(reference_sha256=HASH,scenarios=cases,samples=samples,state_bytes=statebytes,max_error=0,state_bit_exact=True,sample_rates=rates,irregular_partitions=[1,7,13,3,29,11],limitations=['Sidechain config only, active main input marker always1','Synthetic original model/cache objects, no constructor or full plugin','Original visualization publication disabled','Linux libm for original and C'])
 (ROOT/args.report).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
