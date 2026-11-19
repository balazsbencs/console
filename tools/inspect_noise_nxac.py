#!/usr/bin/env python3
"""Inspect local NXAC v1.1 Noise containers; emits metadata only, never audio."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]

def inspect(path):
    b=path.read_bytes()
    assert len(b)>=0x128 and b.startswith(b'NXAC v1.1\n'),path
    magic,channels,frames,cluster,block,rate,table,table_size,data,data_size=struct.unpack_from('<5If4I',b,0x100)
    assert magic==0x3a5920b3 and channels==2 and cluster==2048 and block==64
    assert math.isfinite(rate) and rate>0 and table==0x128
    assert table_size==16*((frames+cluster-1)//cluster)
    assert data==table+table_size and data+data_size==len(b)
    entries=[struct.unpack_from('<ffII',b,o) for o in range(table,data,16)]
    assert entries and entries[0][2]==0
    formats={};rms=[]
    for i,(entry_rate,entry_rms,offset,packed) in enumerate(entries):
        assert entry_rate==rate and math.isfinite(entry_rms) and entry_rms>=0
        left,right=packed&255,(packed>>8)&255
        assert packed>>16==0 and 0<=left<=9 and 0<=right<=9
        def width(fmt):return 0 if fmt==0 else 16*(2+(fmt-1)%3)+2
        expected=(cluster//block)*(width(left)+width(right))
        end=entries[i+1][2] if i+1<len(entries) else data_size
        assert end-offset==expected,(path,i,left,right,end-offset,expected)
        formats[f'{left}/{right}']=formats.get(f'{left}/{right}',0)+1;rms.append(entry_rms)
    text=b[:0x100].split(b'\x1a',1)[0].decode('ascii').strip()
    source=text.split('Original file: ',1)[1]
    return dict(file=path.name,sha256=hashlib.sha256(b).hexdigest(),bytes=len(b),
                source_name=source.replace('\\','/').rsplit('/',1)[-1],
                channels=channels,frames=frames,sample_rate=rate,duration_seconds=frames/rate,
                cluster_frames=cluster,codec_block_frames=block,cluster_count=len(entries),
                table_offset=table,data_offset=data,data_bytes=data_size,
                format_pair_counts=formats,rms_range=[min(rms),max(rms)])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory',type=Path,default=ROOT/'analysis/unpacked/sound/samples.xpak')
    p.add_argument('--report',type=Path,default=ROOT/'analysis/re/noise-nxac-inventory.json')
    a=p.parse_args();files=sorted(a.directory.glob('*.nxac'))
    results=[inspect(path) for path in files]
    output=dict(scope='local NXAC metadata and structural validation; no decode or audio export',files=results)
    a.report.write_text(json.dumps(output,indent=2)+'\n')
    print(json.dumps(dict(files=len(results),total_bytes=sum(x['bytes'] for x in results),
                         total_frames=sum(x['frames'] for x in results),report=str(a.report))))
