#!/usr/bin/env python3
"""Isolated native relation handler comparison with synthetic ModelStore callbacks."""
import argparse
import ctypes as C
import itertools
import json
from pathlib import Path
import random
import struct
from native_bitcolor_probe import NativeBitColor, ROOT, HASH


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path,
                        default=ROOT / 'build-release/libretro_dsp.so')
    args = parser.parse_args()
    image = NativeBitColor()
    handler = C.create_string_buffer(0x100)
    store = C.create_string_buffer(8)
    vtable = C.create_string_buffer(0x40)
    struct.pack_into('<Q', store, 0, C.addressof(vtable))
    struct.pack_into('<Q', handler, 0x10, C.addressof(store))
    values, outputs = {}, {}
    get_type = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_int)
    set_type = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_float)
    get = get_type(lambda context, key: values[key])
    set_ = set_type(lambda context, key, value: outputs.__setitem__(key, value))
    struct.pack_into('<Q', vtable, 0x38, C.cast(get, C.c_void_p).value)
    struct.pack_into('<Q', vtable, 0x30, C.cast(set_, C.c_void_p).value)
    native = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int)(image.base + 0xf0d70)
    port = C.CDLL(str(args.library.resolve()))
    port.rs_relation_mul_triple.argtypes = [C.c_float] * 6
    port.rs_relation_mul_triple.restype = C.c_float
    port.rs_relation_modulate.argtypes = [C.c_float] * 3
    port.rs_relation_modulate.restype = C.c_float
    port.rs_relation_triangle.argtypes = [C.c_float, C.POINTER(C.c_float), C.POINTER(C.c_float)]
    def setup(kind, a, b=0, c=0, curves=(.5,.5,.5)):
        values.clear(); outputs.clear()
        values.update({0:a, 1:b, 2:c})
        # -1 destination suppresses the native handler's optional tracing path.
        struct.pack_into('<4i', handler, 0x20, 0, 1, -1, 2)
        struct.pack_into('<3f', handler, 0x30, *curves)
        struct.pack_into('<i', handler, 0x80, ord(kind))
    def exact(a,b,context):
        assert struct.pack('<f',a)==struct.pack('<f',b), (context,a,b)
    counts={'triangle':0,'mul_triple':0,'modulate':0}
    rng=random.Random(2020)
    for x in [-.1,0,.1,.49999997,.5,.50000006,.9,1,1.1]+[rng.random() for _ in range(1000)]:
        setup('X',x)
        struct.pack_into('<2i',handler,0x2c,3,4)
        native(handler,0,0)
        left,right=C.c_float(),C.c_float()
        port.rs_relation_triangle(x,C.byref(left),C.byref(right))
        exact(outputs[3],left.value,('left',x));exact(outputs[4],right.value,('right',x))
        counts['triangle']+=1
    tests=list(itertools.product([0,.01,.5,1],repeat=3))
    tests += [tuple(rng.random() for _ in range(3)) for _ in range(1000)]
    curves=[(.5,.5,.99),(.5,.5,.5),(.1,.3,.9),(.9,.5,.2)]
    for abc,ks in itertools.product(tests,curves):
        setup('M',*abc,curves=ks);native(handler,0,0)
        exact(outputs[-1],port.rs_relation_mul_triple(*abc,*ks),('M',abc,ks))
        counts['mul_triple']+=1
    styles=[0,.25,.25000003,.5,.50000006,.6,.6000001,.75,.75000006,.85,.8500001,.9,.90000004,.95,.9500001,1]
    pairs=list(itertools.product([0,.1,.5,.99,1],[-1,-.5,0,.5,1]))
    pairs += [(rng.random(),rng.uniform(-1,1)) for _ in range(1000)]
    for (inp,mod),style in itertools.product(pairs,styles):
        setup('b',inp,mod,style);native(handler,0,0)
        exact(outputs[-1],port.rs_relation_modulate(inp,mod,style),('b',inp,mod,style))
        counts['modulate']+=1
    result=dict(scope='isolated native relation handler with synthetic model callbacks',
                reference_sha256=HASH,cases=counts,max_error=0,bit_exact=True,
                limitations='finite normalized controls; no relation dependency scheduling')
    (ROOT/'analysis/re/native-relations-comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
if __name__=='__main__': main()
