#!/usr/bin/env python3
"""Isolated original four-frame Classic band splitter; synthetic numeric state."""
import argparse
import ctypes as C
import json
import math
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


class State(C.Structure):
    _fields_=[('low_coefficient',C.c_float),('middle_coefficient',C.c_float),('history',(C.c_float*2)*2)]
class Bands(C.Structure):
    _fields_=[(n,(C.c_float*4)*2) for n in ['low','middle','high']]


class NativeSplit(NativeBitColor):
    def __init__(self):
        super().__init__();self.state=C.create_string_buffer(0x80)
        ptr=C.POINTER(C.c_float)
        self.stereo=C.CFUNCTYPE(None,C.c_void_p,*([ptr]*8))(self.base+0xa8fc0)
        self.mono=C.CFUNCTYPE(None,C.c_void_p,*([ptr]*4))(self.base+0xa8de0)
    def set(self,s):C.memmove(C.byref(self.state,0x48),C.byref(s),C.sizeof(s))
    def process(self,left,right,mono=False):
        l=(C.c_float*4)(*left);r=(C.c_float*4)(*right)
        out=[(C.c_float*4)() for _ in range(6)]
        assert all(C.addressof(a)%16==0 for a in [l,r]+out)
        if mono:self.mono(self.state,l,*out[:3])
        else:self.stereo(self.state,l,r,*out)
        b=Bands()
        for channel in range(1 if mono else 2):
            for band,name in enumerate(['low','middle','high']):
                getattr(b,name)[channel][:]=out[channel*3+band]
        return b


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--library',default='build-release/libretro_dsp.so');ap.add_argument('--report',default='analysis/re/native-classic-split-comparison.json');args=ap.parse_args()
    lib=C.CDLL(str((ROOT/args.library).resolve()));ptr=C.POINTER(C.c_float)
    lib.rs_classic_split.argtypes=[C.POINTER(State),ptr,ptr,C.POINTER(Bands)]
    lib.rs_classic_split_sample.argtypes=[C.POINTER(State),C.c_float,C.c_float,ptr,ptr,ptr]
    lib.rs_classic_split_mono.argtypes=[C.POINTER(State),ptr,ptr,ptr,ptr]
    n=NativeSplit();rng=random.Random(7329);cases=groups=outputs=states=0
    coefficients=[(0,0),(1,1),(0,1),(1,0),(.001,.999),(.005,.37),(.97,.123),(.00001,.00003)]
    for mono in [False,True]:
     for c1,c2 in coefficients:
      for initialized in [False,True]:
        s=State();s.low_coefficient=c1;s.middle_coefficient=c2
        if initialized:
            for c in range(2):s.history[c][:]=[rng.uniform(-1,1),rng.uniform(-1,1)]
        n.set(s)
        for group in range(4096):
            if group%17==16:
                s.low_coefficient=rng.random();s.middle_coefficient=rng.random()
                # Update coefficients only, preserving independently checked histories.
                C.memmove(C.byref(n.state,0x48),C.byref(s),8)
            left=[C.c_float(math.sin((group*4+i)*.073)*.9).value for i in range(4)]
            right=[C.c_float(rng.uniform(-2,2)).value for _ in range(4)]
            if group%97==3:left=[0.,-0.,1e-10,-1e-10];right=[1.,-1.,100.,-100.]
            if 128<=group%512<256:left=right=[0.]*4
            reference=n.process(left,right,mono);actual=Bands();l=(C.c_float*4)(*left);r=(C.c_float*4)(*right)
            if mono:lib.rs_classic_split_mono(C.byref(s),l,actual.low[0],actual.middle[0],actual.high[0])
            elif group%2:lib.rs_classic_split(C.byref(s),l,r,C.byref(actual))
            else:
                for i in range(4):
                    lo=(C.c_float*2)();mi=(C.c_float*2)();hi=(C.c_float*2)()
                    lib.rs_classic_split_sample(C.byref(s),l[i],r[i],lo,mi,hi)
                    for c in range(2):actual.low[c][i]=lo[c];actual.middle[c][i]=mi[c];actual.high[c][i]=hi[c]
            assert bytes(actual)==bytes(reference),(mono,c1,c2,initialized,group,'bands')
            assert bytes(s)==n.state.raw[0x48:0x60],(mono,c1,c2,initialized,group,'state')
            groups+=1;outputs+=12 if mono else 24;states+=6
        cases+=1
    report=dict(reference_sha256=HASH,scope='Isolated original SplitMono4/SplitStereo4 with synthetic coefficients and histories; no constructor or full distortion process',scenarios=cases,four_frame_groups=groups,input_audio_samples=outputs//3,band_output_values=outputs,state_float_comparisons=states,maximum_absolute_error=0,all_float_patterns_equal=True,dynamic_coefficients=True,mono=True,stereo=True,per_frame_stereo_api=True,initial_coefficients=coefficients,limitations=['No original filter setup, waveshaping, gains, dry/wet or full chain','Only finite stable coefficients0..1 tested'])
    path=ROOT/args.report;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
