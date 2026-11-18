#!/usr/bin/env python3
"""Compare reconstructed clean Space preparation with isolated original prepare."""
import argparse
import ctypes as C,json,struct
from native_space_probe import NativeSpace,ROOT,HASH,NAMES,DEFAULTS,buffer_offsets

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--library',default='build-release/libretro_dsp.so');ap.add_argument('--report',default='analysis/re/native-space-prepare-comparison.json');args=ap.parse_args()
    lib=C.CDLL(str((ROOT/args.library).resolve()));lib.rs_space_prepare_state.argtypes=[C.c_void_p,C.c_float];lib.rs_space_free_state.argtypes=[C.c_void_p]
    counts=0
    for rate in [1000,8000,44100,48000,96000,192000,384000,768000,1536000]:
     for controls in [{},{'pre_delay_ms':100,'dampen_hz':100,'soften_hz':16000},{'pre_delay_ms':37.5,'dampen_hz':22050,'soften_hz':0}]:
        n=NativeSpace(rate,**controls);s=C.create_string_buffer(0x2000);p=DEFAULTS.copy();p.update(controls)
        struct.pack_into('<14f',s,0x1160,*[p[k] for k in NAMES]);assert lib.rs_space_prepare_state(s,rate)
        # Pointer values and vtables differ; compare their lengths/positions separately.
        offsets=list(range(0x5b0,0x5b8,4))+[0x5c0,0x5d8,0x5dc,0x5e8,0x5ec,0x5f8,0x5fc]+list(range(0x600,0x618,4))+list(range(0x620,0x690,4))+list(range(0x6f0,0x8e0,4))+list(range(0x1130,0x1154,4))
        for o in offsets:assert s.raw[o:o+4]==n.state.raw[o:o+4],(rate,controls,hex(o),struct.unpack_from('<f',s,o)[0],struct.unpack_from('<f',n.state,o)[0])
        for ptr in buffer_offsets()[:-2]:
            o=ptr-16
            assert s.raw[o+8:o+16]==n.state.raw[o+8:o+16],(rate,hex(o),'length')
            for state in [s,n.state]:
                base,end,current=struct.unpack_from('<3Q',state,o+16);length=struct.unpack_from('<I',state,o+12)[0]
                assert end-base==(length-1)*4 and current==base
        lib.rs_space_free_state(s);n.close();counts+=1
    report=dict(reference_sha256=HASH,scenarios=counts,numeric_setup_bit_exact=True,ring_lengths_positions_exact=True,limitations=['Clean zero-filled synthetic allocation','No original constructor or plugin host','Linux libm'])
    (ROOT/args.report).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
