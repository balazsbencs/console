#!/usr/bin/env python3
"""Synthetic isolated RC-20 Wobble kernel probe (Linux x86_64).

Maps a hash-pinned original image; manually reconstructs prepare state. Never
executes plugin constructors, prepare allocations, resource scripts or host
entry points. Cached parameter wrappers and dummy visualization stores stand
in for the original model. This is a kernel experiment, not plugin hosting.
"""
import ctypes as C
import json
import math
import struct
from native_bitcolor_probe import NativeBitColor, HASH


class NativeWobble(NativeBitColor):
    def __init__(self,sample_rate=48000.,depth=.2,depth2=.05,rate=.372093,
                 rate2=7.999029,mix=1.,stereo_phase=0.,noise=0.,noise_scaler=0.,enabled=1.,normalized=0.):
        super().__init__()  # image loader only; inherited synthetic BitColor object is replaced
        self.state=C.create_string_buffer(0x800)
        self.callbacks=[]
        self.buffers=[]
        self.sample_rate=self.float32(sample_rate)
        self.normalized=bool(normalized)
        self.guard=C.c_uint64(0x3141592653589793)
        struct.pack_into('<Q',self.image,0x73a228,C.addressof(self.guard))
        self.libc=C.CDLL(None)
        for stub,name,library in [(0x578c12,'memcpy',self.libc),(0x578c96,'pow',self.libm)]:
            slot=stub+6+struct.unpack_from('<i',self.image,stub+2)[0]
            struct.pack_into('<Q',self.image,slot,C.cast(getattr(library,name),C.c_void_p).value)
        self.vtable=C.create_string_buffer(0x88)
        struct.pack_into('<Q',self.vtable,0x80,self.base+0x3525c0)
        struct.pack_into('<Q',self.state,0,C.addressof(self.vtable))
        struct.pack_into('<i',self.state,0xf4,2)
        self.f32(0xf8,self.sample_rate);self.f32(0x4b0,1./self.sample_rate)
        self.f32(0x4b4,self.float32(self.sample_rate*.02))
        requested=2*int(self.float32(self.sample_rate*.02))+1
        self.capacity=1 << (requested-1).bit_length()
        struct.pack_into('<i',self.state,0x4b8,requested)
        for pointer,mask in [(0x4e0,0x4e8),(0x4f0,0x4f8)]:
            buffer=(C.c_float*self.capacity)();self.buffers.append(buffer)
            struct.pack_into('<Q',self.state,pointer,C.addressof(buffer))
            struct.pack_into('<I',self.state,mask,self.capacity-1)
        for off in [0x500,0x520,0x540,0x560]:
            struct.pack_into('<Q',self.state,off,self.base+0x135330)
            self.f32(off+8,-.5);self.f32(off+0x10,.75)
            self.f32(off+0x1c,1./self.sample_rate)
        coeff=self.exp32(self.float32(self.float32(10./self.sample_rate)*self.float32(-6.2831855)))
        for off,value in [(0x590,10./self.sample_rate),(0x594,1),(0x598,1-coeff),(0x59c,self.float32(1+coeff)*.5),(0x5a0,coeff)]:self.f32(off,value)
        def constant(address):return struct.unpack_from('<f',self.image,address)[0]
        cutoff=self.float32(self.sample_rate*constant(0x5f5868))
        cutoff=self.float32(cutoff*constant(0x5e1228))
        cutoff=self.float32(cutoff*constant(0x5ea9b0))
        exponent=self.float32(constant(0x5f0dcc)*cutoff)
        exponent=self.float32(exponent/constant(0x5e21e4))
        feed=self.float32(1-self.exp32(exponent))
        for off in [0x5a4,0x5b8,0x5cc,0x5e0,0x5f4,0x608]:
            struct.pack_into('<I',self.state,off,0x3c8637bd)
            self.f32(off+8,feed);self.f32(off+12,cutoff)
            self.state[off+16]=b'\x01'
        self.state[0x7cc:0x7dc]=self.image[0x5f5ab0:0x5f5ac0]
        struct.pack_into('<i',self.state,0x7c4,int(self.float32(self.float32(self.sample_rate*.5)*.015625)))
        struct.pack_into('<i',self.state,0x7e0,10)
        for off in [0x620,0x640]:
            data=(C.c_float*64)();pointers=(C.c_void_p*1)(C.addressof(data))
            self.buffers.extend([data,pointers])
            struct.pack_into('<Q',self.state,off,C.addressof(data))
            struct.pack_into('<Q',self.state,off+8,C.addressof(pointers))
            struct.pack_into('<ii',self.state,off+16,1,1)
        self.model=C.create_string_buffer(8);self.model_vtable=C.create_string_buffer(0x40)
        noop=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_float)(lambda *_:None)
        self.callbacks.append(noop)
        struct.pack_into('<Q',self.model_vtable,0x30,C.cast(noop,C.c_void_p).value)
        struct.pack_into('<Q',self.model,0,C.addressof(self.model_vtable))
        for off in [0x660,0x710]:
            struct.pack_into('<Q',self.state,off+0x18,C.addressof(self.model))
            self.state[off+0xa8]=b'\x01' # disable visualization accumulation, preserve counter callback
            struct.pack_into('<i',self.state,off+0x88,0x7fffffff)
        self.set(depth=depth,depth2=depth2,rate=rate,rate2=rate2,mix=mix,
                 stereo_phase=stereo_phase,noise=noise,noise_scaler=noise_scaler,enabled=enabled)
        self.process_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p)(self.base+0x350c20)
        self.phase_fn=C.CFUNCTYPE(None,C.c_void_p)(self.base+0x3525c0)
        self.smoothing_fn=C.CFUNCTYPE(None,C.c_void_p)(self.base+0x350be0)
        self.phase_fn(self.state)

    def set(self,**values):
        parameters={'depth':(0x140,0,.8,.4),'depth2':(0x1e0,0,.2,.1),
                    'rate':(0x280,.1,4,1),'rate2':(0x320,6,20,10)}
        wrappers={'enabled':0x108,'stereo_phase':0x3c0,'noise':0x3f8,'noise_scaler':0x430,'mix':0x468}
        for name,value in values.items():
            if name in wrappers:
                self.wrap(wrappers[name],value)
                if name == 'enabled':
                    struct.pack_into('<i',self.state,0x7dc,10 if value < .5 else 0)
            elif name in parameters:
                off,lo,hi,mid=parameters[name]
                physical=self.scale(value,lo,hi,mid) if self.normalized else value
                self.param(off,value if self.normalized else .5,physical)
                self.metadata(off,lo,hi,mid)
                if name in ('rate','rate2'):self.state[off+0x94]=b'\x00'
            else:raise ValueError(name)

    def reset(self):
        """Observed phase reset + smoothing invalidation; does not clear delay memory."""
        self.phase_fn(self.state);self.smoothing_fn(self.state)


if __name__=='__main__':
    native=NativeWobble()
    out=native.process([.5*math.sin(i*.1) for i in range(64)],[.4*math.cos(i*.13) for i in range(64)])
    assert all(math.isfinite(x) for channel in out for x in channel)
    print(json.dumps(dict(scope='isolated Wobble with synthetic prepare/model state, visualization disabled',
                          reference_sha256=HASH,capacity=native.capacity,left=out[0][:8],right=out[1][:8])))
