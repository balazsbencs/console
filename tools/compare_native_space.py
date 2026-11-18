#!/usr/bin/env python3
"""Original ResoX prepare/process against C, including arbitrary host partitions."""
import argparse
import ctypes as C
import json
import math
import random
import struct
from native_space_probe import NativeSpace,NAMES,DEFAULTS,buffer_offsets,ROOT,HASH
class Params(C.Structure):_fields_=[(name,C.c_float) for name in NAMES]
def bits(x):return struct.pack('<f',x)
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--library',default='build-release/libretro_dsp.so');ap.add_argument('--report',default='analysis/re/native-space-comparison.json');args=ap.parse_args()
 lib=C.CDLL(str((ROOT/args.library).resolve()));lib.retro_space_create.restype=C.c_void_p
 for name in ['prepare','set','process','reset','reset_smoothing','destroy']:getattr(lib,'retro_space_'+name).argtypes=[C.c_void_p]+({'prepare':[C.c_float],'set':[C.POINTER(Params)],'process':[C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_size_t]}.get(name,[]))
 rates=[1000,22050,44100,48000,48001,44117,96000,192000,384000,768000,1536000]
 scenes=[{},dict(level=1),dict(stereo_mode=1,dual_mono=1,stereo_detune=.3918),dict(dampen_hz=2000,soften_hz=4000,allpass=0),dict(decay=1,pre_delay_ms=100,reverb_mode=0),dict(pre_wobble_amount=1,pre_wobble_noise=.4,stereo_mode=1,dual_mono=1),dict(enable=0),dict(level=0),dict(decay=0,soften_hz=0,dampen_hz=22050,feedbacks=0),dict(stereo_detune=1,stereo_mode=1,pre_wobble_amount=.01,pre_wobble_noise=-1)]
 samples=cases=statebytes=0
 for rate in rates:
  for scene in scenes:
   p=DEFAULTS.copy();p.update(scene);n=NativeSpace(rate,**p);s=lib.retro_space_create();assert lib.retro_space_set(s,C.byref(Params(**p)));assert lib.retro_space_prepare(s,rate);rng=random.Random(rate)
   for block in range((1024 if rate==1536000 else 512) if rate in [384000,1536000] and scene=={} else 128):
    if block==64:n.close();n=NativeSpace(rate,**p);lib.retro_space_reset(s)
    if block in [24,40,55,70,85,100,112]:
     changes={24:dict(level=.8,enable=1),40:dict(decay=.4,dampen_hz=900,soften_hz=12000),55:dict(stereo_detune=.7,stereo_mode=1,dual_mono=1),70:dict(pre_delay_ms=30,pre_wobble_amount=.3,pre_wobble_noise=.7),85:dict(allpass=0,reverb_mode=0),100:dict(level=0),112:dict(level=.5,allpass=1,reverb_mode=1)}[block]
     p.update(changes);n.set(**changes);assert lib.retro_space_set(s,C.byref(Params(**p)))
    if block==88:
     p['pre_delay_ms']=75;n.set(pre_delay_ms=75);assert lib.retro_space_set(s,C.byref(Params(**p)));n.smoothing_fn(n.state);lib.retro_space_reset_smoothing(s)
    left=[C.c_float(math.sin((block*64+i)*.071)*.7).value for i in range(64)];right=[C.c_float(rng.uniform(-1,1)).value for i in range(64)]
    if block%17 in [3,4]:left=right=[0.]*64
    expected=n.process(left,right);l=(C.c_float*64)(*left);r=(C.c_float*64)(*right);off=0
    for chunk in ([1,7,13,3,29,11] if block%2 else [64]):
     lib.retro_space_process(s,C.cast(C.byref(l,off*4),C.POINTER(C.c_float)),C.cast(C.byref(r,off*4),C.POINTER(C.c_float)),chunk);off+=chunk
    for c,(a,b) in enumerate(zip(expected,[l,r])):
     for i,(x,y) in enumerate(zip(a,b)):assert math.isfinite(y) and bits(x)==bits(y),('audio',rate,scene,block,c,i,x,y)
    ns=n.state.raw;cs=C.string_at(s,0x1160)
    for start,end in [(0x600,0x60c),(0x628,0x648),(0x654,0x660),(0x668,0x690),(0x760,0x8d0),(0x1130,0x1150)]:
     for pos in range(start,end,4):assert ns[pos:pos+4]==cs[pos:pos+4],('state',rate,scene,block,hex(pos),struct.unpack_from('<f',ns,pos),struct.unpack_from('<f',cs,pos))
     statebytes+=end-start
    for pos in buffer_offsets():
     pn=struct.unpack_from('<Q',ns,pos)[0];pc=struct.unpack_from('<Q',cs,pos)[0]
     count=struct.unpack_from('<I',ns,pos-4)[0] if pos not in [0x5e0,0x5f0] else struct.unpack_from('<I',ns,pos+8)[0]+1
     if pos not in [0x5e0,0x5f0]:
      assert ns[pos-8:pos]==cs[pos-8:pos],('length',rate,scene,block,hex(pos))
      for shift in [8,16]:assert struct.unpack_from('<Q',ns,pos+shift)[0]-pn==struct.unpack_from('<Q',cs,pos+shift)[0]-pc,('position',rate,scene,block,hex(pos),shift)
     else:assert ns[pos+8:pos+16]==cs[pos+8:pos+16],('ring-position',rate,scene,block,hex(pos))
     aa=C.string_at(pn,count*4);bb=C.string_at(pc,count*4)
     assert aa==bb,('delay',rate,scene,block,hex(pos),next((i//4 for i in range(len(aa)) if aa[i]!=bb[i]),None))
     statebytes+=count*4
    samples+=128
   n.close();lib.retro_space_destroy(s);cases+=1
 report=dict(reference_sha256=HASH,scenarios=cases,samples=samples,state_bytes=statebytes,max_error=0,sample_rates=rates,irregular_partitions=[1,7,13,3,29,11],hard_reset_against_fresh_native=True,reset_smoothing_after_pending_control=True,long_run_384000_frames=32768,long_run_1536000_frames=65536,limitations=['Synthetic zero-filled allocation, no constructor or original plugin host','Visualization-only meters omitted and excluded from state comparison','Original active input marker always1; no native silence-tail optimization','Linux libm for original and C'])
 path=ROOT/args.report;path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
