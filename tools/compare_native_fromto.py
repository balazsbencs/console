#!/usr/bin/env python3
"""Compare native syncFromTo with the built C port."""
import argparse
import ctypes as C
from pathlib import Path
import json
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, default=ROOT / 'build-release/libretro_dsp.so')
    args = parser.parse_args()
    port = C.CDLL(str(args.library.resolve()))
    port.rs_relation_from_to.argtypes = [C.c_float] * 6
    port.rs_relation_from_to.restype = C.c_float
    image=NativeBitColor()
    handler=C.create_string_buffer(0x100)
    store=C.create_string_buffer(8)
    vtable=C.create_string_buffer(0x40)
    struct.pack_into('<Q',store,0,C.addressof(vtable))
    struct.pack_into('<Q',handler,0x10,C.addressof(store))
    current=[0.0];out=[]
    getter=C.CFUNCTYPE(C.c_float,C.c_void_p,C.c_int)(lambda *_:current[0])
    setter=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_float)(lambda _,key,value:out.append(value))
    struct.pack_into('<Q',vtable,0x38,C.cast(getter,C.c_void_p).value)
    struct.pack_into('<Q',vtable,0x30,C.cast(setter,C.c_void_p).value)
    native=C.CFUNCTYPE(None,C.c_void_p,C.c_int,C.c_int)(image.base+0xf0d70)
    struct.pack_into('<3i',handler,0x20,0,-1,-1)
    struct.pack_into('<i',handler,0x80,ord('f'))
    f=lambda x:C.c_float(x).value
    rng=random.Random(342)
    cases=[(x,0.,1.,.9484,.85,.5) for x in [-1,0,.1,.5,.9,1,2]]
    cases += [(rng.uniform(-.2,1.2),0.,1.,.9484,.85,.5) for _ in range(10000)]
    cases += [(rng.uniform(-5,5),-.5,3.,rng.uniform(-5,5),rng.uniform(-5,5),rng.choice([.1,.5,.7,.99])) for _ in range(10000)]
    for case in cases:
        x,lo,hi,outlo,outhi,k=map(f,case)
        current[0]=x;out.clear()
        struct.pack_into('<5f',handler,0x2c,lo,hi,outlo,outhi,k)
        native(handler,0,0)
        expected=port.rs_relation_from_to(x,lo,hi,outlo,outhi,k)
        assert len(out)==1 and struct.pack('<f',out[0])==struct.pack('<f',expected),(case,out,expected)
    result=dict(scope='isolated native syncFromTo relation',reference_sha256=HASH,
                cases=len(cases),max_error=0,bit_exact=True,
                limitations='built C port; no serializer or dependency scheduling execution')
    (ROOT/'analysis/re/native-fromto-comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
if __name__=='__main__':main()
