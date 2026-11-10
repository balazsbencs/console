#!/usr/bin/env python3
"""Original Classic Distortion numeric methods with synthetic 128-byte state.

No constructors, global initializers, resource scripts or plugin host execute.
Verified mask constants are copied into their normal BSS destinations directly.
"""
import ctypes as C
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


NAMES=['amount','character','low_hz','high_hz','dist_db','pass_db','mix','output_hz','reference_db']


class NativeClassic(NativeBitColor):
    def __init__(self,sample_rate=48000.,**params):
        super().__init__();self.state=C.create_string_buffer(0x80)
        # Static b37e0..b3817 initialization: literal masks and copies only.
        self.image[0x789c90:0x789ca4]=b'\xff'*20
        self.image[0x789cb0:0x789cc0]=self.image[0x5e1060:0x5e1070]
        self.image[0x789cc0:0x789cd0]=self.image[0x5e1070:0x5e1080]
        self.calc_fn=C.CFUNCTYPE(None,C.c_void_p)(self.base+0xa6080)
        self.set_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_float)(self.base+0xa5d80)
        self.prepare_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_float,C.c_int,C.c_bool)(self.base+0xa5d10)
        self.classic_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_bool,C.POINTER(C.c_bool),C.POINTER(C.c_float),C.POINTER(C.c_float))(self.base+0xa5e30)
        self.params=dict(amount=50,character=0,low_hz=20,high_hz=20000,dist_db=0,pass_db=0,mix=100,output_hz=20000,reference_db=0)
        self.params.update(params);self.set(**self.params)
        self.prepare_fn(self.state,sample_rate,64,False);self.calculate()

    def calculate(self):
        self.calc_fn(self.state);self.state[8]=b'\x00'

    def set(self,**params):
        self.params.update(params)
        mapping=dict(amount=0,character=8,low_hz=4,high_hz=3,dist_db=1,pass_db=6,mix=5,output_hz=2,reference_db=7)
        for name,index in mapping.items():self.set_fn(self.state,index,self.params[name])
        # During construction, ResetAudio first supplies the sample rate.
        if struct.unpack_from('<f',self.state,0x10)[0]:self.calculate()

    def reset(self):
        self.prepare_fn(self.state,struct.unpack_from('<f',self.state,0x10)[0],64,False)

    def process(self,left,right):
        assert len(left)==len(right)==64
        l=(C.c_float*64)(*left);r=(C.c_float*64)(*right)
        assert C.addressof(l)%16==C.addressof(r)%16==0
        silent=C.c_bool(False);self.classic_fn(self.state,64,False,C.byref(silent),l,r)
        assert not silent.value
        return list(l),list(r)
