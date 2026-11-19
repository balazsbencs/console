#!/usr/bin/env python3
"""Original cSimpleComp sidechain numerics with synthetic caches and stereo buffers."""
import ctypes as C
import struct
from native_bitcolor_probe import NativeBitColor,ROOT,HASH
NAMES=['enable','threshold','ratio','attack','release','averaging','gain_comp','highpass']
DEFAULTS=dict(zip(NAMES,[1,0,.5,0,.2,0,0,1]))
class NativeNoiseComp(NativeBitColor):
 def __init__(self,rate=48000,**params):
  super().__init__();self.state=C.create_string_buffer(0x3a0)
  for stub,name in [(0x578a2c,'exp'),(0x578bd0,'log10'),(0x578c96,'pow'),(0x578dda,'sinf'),(0x5789e4,'cosf')]:
   slot=stub+6+struct.unpack_from('<i',self.image,stub+2)[0];struct.pack_into('<Q',self.image,slot,C.cast(getattr(self.libm,name),C.c_void_p).value)
  self.source=C.create_string_buffer(32);struct.pack_into('<Q',self.state,0xe0,C.addressof(self.source));self.state[0xfc]=b'\1';struct.pack_into('<i',self.state,0xf4,2)
  self.side=(C.c_float*128)();self.side_channels=(C.c_void_p*2)(C.addressof(self.side),C.addressof(self.side)+256)
  struct.pack_into('<QQii',self.state,0x308,C.addressof(self.side),C.addressof(self.side_channels),2,2)
  self.model=C.create_string_buffer(8);self.vtable=C.create_string_buffer(0x40);self.setter=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_float)(lambda *_:None)
  struct.pack_into('<Q',self.model,0,C.addressof(self.vtable));struct.pack_into('<Q',self.vtable,0x30,C.cast(self.setter,C.c_void_p).value);struct.pack_into('<Q',self.state,0x328,C.addressof(self.model))
  self.params=DEFAULTS.copy();self.set(**params)
  self.prepare_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_float)(self.base+0x3657a0)
  self.process_fn=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p)(self.base+0x3659b0)
  self.audio=(C.c_float*128)();self.channels=(C.c_void_p*2)(C.addressof(self.audio),C.addressof(self.audio)+256);self.batch=C.create_string_buffer(32)
  struct.pack_into('<Q',self.batch,8,C.addressof(self.channels));struct.pack_into('<i',self.batch,0x10,2);struct.pack_into('<f',self.batch,0x18,1)
  self.prepare(rate)
 def set(self,**params):
  self.params.update(params)
  for i,key in enumerate(NAMES):self.wrap(0x108+i*0x38,self.params[key])
  self.wrap(0x2d0,0)
 def prepare(self,rate):self.prepare_fn(self.state,rate)
 def reset(self):self.prepare(struct.unpack_from('<f',self.state,0xf8)[0])
 def process(self,left,right,side_left,side_right):
  assert all(len(x)==64 for x in [left,right,side_left,side_right])
  for i,x in enumerate(left+right):self.audio[i]=x
  for i,x in enumerate(side_left+side_right):self.side[i]=x
  self.process_fn(self.state,self.batch)
  return list(self.audio[:64]),list(self.audio[64:])
