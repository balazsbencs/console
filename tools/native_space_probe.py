#!/usr/bin/env python3
"""Isolated original ResoX numerical prepare/process, synthetic state and model.
No constructor, resource script or plugin host executes. Allocation is explicitly
zero-filled to establish deterministic clean histories; Linux math imports bind.
"""
import ctypes as C
import struct
from native_bitcolor_probe import NativeBitColor,ROOT,HASH

NAMES=['enable','decay','dampen_hz','soften_hz','level','stereo_detune','stereo_mode','dual_mono','feedbacks','allpass','reverb_mode','pre_delay_ms','pre_wobble_noise','pre_wobble_amount']
OFFSETS=[0x108,0x140,0x178,0x218,0x2b8,0x2f0,0x328,0x360,0x398,0x3d0,0x408,0x440,0x4e0,0x518]
SCALED={0x178,0x218,0x440}
DEFAULTS=dict(zip(NAMES,[1,.8,0,22050,.15,0,0,0,1,1,1,.01,0,0]))

def buffer_offsets():
    return [0x8e0+c*0x428+i*0x48+0x10 for c in range(2) for i in range(12)]+[0xc48+c*0x428+i*0x30+0x10 for c in range(2) for i in range(4)]+[0x5e0,0x5f0]

class NativeSpace(NativeBitColor):
    def __init__(self,rate=48000.,**params):
        super().__init__();self.state=C.create_string_buffer(0x1160);self.buffers=[]
        self.libc=C.CDLL(None);self.libc.calloc.argtypes=[C.c_size_t,C.c_size_t];self.libc.calloc.restype=C.c_void_p;self.libc.free.argtypes=[C.c_void_p]
        self.guard=C.c_uint64(0x3141592653589793);struct.pack_into('<Q',self.image,0x73a228,C.addressof(self.guard))
        self.allocate=C.CFUNCTYPE(C.c_void_p,C.c_size_t)(lambda size:self.libc.calloc(1,size))
        for stub,name,library in [(0x578a32,'exp2',self.libm),(0x578c12,'memcpy',self.libc),(0x578c1e,'memset',self.libc),(0x578ab6,'free',self.libc),(0x5789e4,'cosf',self.libm),(0x578dda,'sinf',self.libm),(0x578bd6,'log10f',self.libm)]:
            slot=stub+6+struct.unpack_from('<i',self.image,stub+2)[0];struct.pack_into('<Q',self.image,slot,C.cast(getattr(library,name),C.c_void_p).value)
        stub=0x578c00;slot=stub+6+struct.unpack_from('<i',self.image,stub+2)[0];struct.pack_into('<Q',self.image,slot,C.cast(self.allocate,C.c_void_p).value)
        self.delay_vtable=C.create_string_buffer(0x28);self.allpass_vtable=C.create_string_buffer(0x28)
        for table,prepare,reset in [(self.delay_vtable,0x1c1620,0x1c1670),(self.allpass_vtable,0x361460,0x3614b0)]:
            struct.pack_into('<QQQ',table,0x10,self.base+prepare,self.base+0x1ba7d0,self.base+reset)
        for c in range(2):
            for i in range(12):struct.pack_into('<Q',self.state,0x8e0+c*0x428+i*0x48,C.addressof(self.delay_vtable))
            for i in range(4):struct.pack_into('<Q',self.state,0xc48+c*0x428+i*0x30,C.addressof(self.allpass_vtable))
        for dest,source,size in [(0x628,0x5e7a80,16),(0x640,0x5f5c90,16),(0x668,0x5e7ab0,16),(0x678,0x5e7ac0,16),(0x1130,0x5f5ca0,16),(0x1140,0x5e7af0,16)]:self.state[dest:dest+size]=self.image[source:source+size]
        for offset,value in [(0x608,1),(0x60c,1),(0x620,-.5),(0x65c,-1),(0x660,-1),(0x688,1)]:struct.pack_into('<f',self.state,offset,value)
        struct.pack_into('<i',self.state,0x664,-1);struct.pack_into('<i',self.state,0x1150,-1);self.state[0x610]=b'\x01'
        struct.pack_into('<i',self.state,0xf4,2)
        self.params=DEFAULTS.copy();self.params.update(params)
        self.model=C.create_string_buffer(8);self.model_vtable=C.create_string_buffer(0x40)
        self.setter=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_float)(lambda *_:None)
        self.getter=C.CFUNCTYPE(C.c_float,C.c_void_p,C.c_int)(lambda _,key: self.params[NAMES[key]])
        struct.pack_into('<Q',self.model,0,C.addressof(self.model_vtable));struct.pack_into('<Q',self.model_vtable,0x30,C.cast(self.setter,C.c_void_p).value);struct.pack_into('<Q',self.model_vtable,0x38,C.cast(self.getter,C.c_void_p).value)
        struct.pack_into('<Q',self.state,0x5c8,C.addressof(self.model))
        for key,off in enumerate(OFFSETS):
            struct.pack_into('<Q',self.state,off+0x18,C.addressof(self.model));struct.pack_into('<i',self.state,off+0x28,key)
        self._cache_initialized=False
        self.set(**self.params)
        self.prepare_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_float)(self.base+0x35be80)
        self.clear_fn=C.CFUNCTYPE(None,C.c_void_p)(self.base+0x361840)
        self.smoothing_fn=C.CFUNCTYPE(None,C.c_void_p)(self.base+0x35c880)
        self.space_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p)(self.base+0x35c900)
        self.batch=C.create_string_buffer(0x40);self.audio=(C.c_float*128)();self.channels=(C.c_void_p*2)(C.addressof(self.audio),C.addressof(self.audio)+256)
        # cAudioBatchBuffer layout used by existing block fixtures.
        struct.pack_into('<Q',self.batch,8,C.addressof(self.channels));struct.pack_into('<i',self.batch,0x10,2);struct.pack_into('<f',self.batch,0x18,1)
        self.prepare(rate)
    def set(self,**params):
        for name,value in params.items():
            off=OFFSETS[NAMES.index(name)];value=C.c_float(value).value
            changed=not self._cache_initialized or struct.pack('<f',self.params[name])!=struct.pack('<f',value)
            self.params[name]=value
            if not changed:continue
            if off in SCALED:
                self.param(off,0,value);self.state[off+0x94]=b"\x00"
            else:
                self.wrap(off,value);self.state[off+0x2c]=b"\x00"
        self._cache_initialized=True
    def prepare(self,rate):self.prepare_fn(self.state,rate)
    def reset(self):self.clear_fn(self.state);self.smoothing_fn(self.state)
    def process(self,left,right):
        assert len(left)==len(right)==64
        for i,x in enumerate(left+right):self.audio[i]=x
        self.space_fn(self.state,self.batch)
        return list(self.audio[:64]),list(self.audio[64:])
    def close(self):
        if not getattr(self,'state',None):return
        for off in buffer_offsets():
            pointer=struct.unpack_from('<Q',self.state,off)[0]
            if pointer:self.libc.free(pointer);struct.pack_into('<Q',self.state,off,0)
    def __del__(self):self.close()
