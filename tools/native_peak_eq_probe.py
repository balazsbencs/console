#!/usr/bin/env python3
"""Original peak-EQ numerical methods on a synthetic160byte object; no ctor/host."""
import ctypes as C
import struct
from native_bitcolor_probe import NativeBitColor,ROOT,HASH


class NativePeakEq(NativeBitColor):
    def __init__(self,rate=48000.,freq=900.,q=.4,db=0.):
        super().__init__();self.state=C.create_string_buffer(0xa0)
        self.guard=C.c_uint64(0x3141592653589793)
        struct.pack_into('<Q',self.image,0x73a228,C.addressof(self.guard))
        for stub,name in [(0x578dda,'sinf'),(0x5789e4,'cosf')]:
            slot=stub+6+struct.unpack_from('<i',self.image,stub+2)[0]
            struct.pack_into('<Q',self.image,slot,C.cast(getattr(self.libm,name),C.c_void_p).value)
        self.update_fn=C.CFUNCTYPE(None,C.c_void_p)(self.base+0xad290)
        self.prepare_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_float,C.c_int,C.c_bool)(self.base+0xab970)
        self.soft_fn=C.CFUNCTYPE(None,C.c_void_p)(self.base+0xaba50)
        self.eq_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_bool,C.POINTER(C.c_bool),C.POINTER(C.c_float),C.POINTER(C.c_float))(self.base+0xabb30)
        struct.pack_into('<i',self.state,8,4)
        self.controls=(C.c_float(freq).value,C.c_float(q).value,C.c_float(db).value)
        struct.pack_into('<3f',self.state,0x10,*self.controls)
        self.prepare(rate)
    def set(self,freq=None,q=None,db=None):
        old=self.controls
        new=tuple(C.c_float(x if x is not None else old[i]).value for i,x in enumerate([freq,q,db]))
        if new==old:return
        self.controls=new;struct.pack_into('<3f',self.state,0x10,*new)
        # Atomic parameter batch: original UpdatePrivates sees final values once.
        # Dist preset stages have fixed freq/Q, so only db changes in production.
        self.update_fn(self.state)
    def prepare(self,rate):self.prepare_fn(self.state,rate,64,False)
    def reset(self):self.soft_fn(self.state)
    def process(self,left,right):
        assert len(left)==len(right)==64
        l=(C.c_float*64)(*left);r=(C.c_float*64)(*right);silent=C.c_bool(False)
        self.eq_fn(self.state,64,False,C.byref(silent),l,r)
        return list(l),list(r)
