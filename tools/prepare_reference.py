#!/usr/bin/env python3
"""Extract an exact x86_64 slice; never modify the supplied universal binary."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / 'RC-20 Retro Color.vst/Contents/MacOS/RC-20 Retro Color'
data = source.read_bytes()
magic, count = struct.unpack_from('>II', data)
if magic != 0xCAFEBABE:
    raise SystemExit('Expected a big-endian FAT Mach-O header')
slices = []
for index in range(count):
    cpu, subtype, offset, size, alignment = struct.unpack_from('>IIIII', data, 8 + 20 * index)
    if offset + size > len(data):
        raise SystemExit('Slice exceeds file bounds')
    slices.append(dict(cpu=cpu, subtype=subtype, offset=offset, size=size, alignment=alignment))
selected, = [s for s in slices if s['cpu'] == 0x01000007]
payload = data[selected['offset']:selected['offset'] + selected['size']]
if payload[:4] != b'\xcf\xfa\xed\xfe':
    raise SystemExit('Expected little-endian 64-bit Mach-O slice')
out = ROOT / 'analysis/reference'
out.mkdir(parents=True, exist_ok=True)
target = out / 'rc20-1.0.5-x86_64.macho'
target.write_bytes(payload)
manifest = dict(source=str(source.relative_to(ROOT)), source_sha256=hashlib.sha256(data).hexdigest(),
                slices=slices, selected_slice=selected, output=str(target.relative_to(ROOT)),
                output_sha256=hashlib.sha256(payload).hexdigest(), bytes=len(payload))
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

# Preserve native symbol names as independent static evidence and REA seeds.
symbols = []
cursor = 32
for _ in range(struct.unpack_from('<I', payload, 16)[0]):
    command, size = struct.unpack_from('<II', payload, cursor)
    if command == 2:  # LC_SYMTAB
        symoff, nsyms, stroff, strsize = struct.unpack_from('<IIII', payload, cursor + 8)
        for index in range(nsyms):
            nameoff, kind, section, desc, address = struct.unpack_from('<IBBHQ', payload, symoff + 16 * index)
            if nameoff >= strsize:
                raise SystemExit('Invalid symbol name offset')
            end = payload.index(b'\0', stroff + nameoff, stroff + strsize)
            name = payload[stroff + nameoff:end].decode('utf-8', errors='replace')
            symbols.append(dict(address=hex(address), raw=name, type=kind, section=section))
    cursor += size
names = '\n'.join(s['raw'][1:] if s['raw'].startswith('_') else s['raw'] for s in symbols) + '\n'
demangled = subprocess.run(['c++filt'], input=names, text=True, check=True, capture_output=True).stdout.splitlines()
for symbol, name in zip(symbols, demangled, strict=True):
    symbol['name'] = name
(out / 'symbols.json').write_text(json.dumps(symbols, indent=2) + '\n')
modules = ['cBitColorBlock', 'cSampleDegraderBlock', 'cWobblerBlock', 'cFlutterBlock',
           'cAESingleBandDistortion', 'cAEMagicDistortion', 'cSimpleReverbBlock',
           'cStereoWidthBlock', 'cNoiseBlock', 'cNoiseFXBlock', 'cGainBlock']
candidates = [s for s in symbols if s['section'] == 1 and any(m + '::' in s['name'] for m in modules)
              and not s['name'].startswith(('non-virtual thunk', 'std::'))]
(out / 'dsp-symbols.json').write_text(json.dumps(candidates, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
print(f'Indexed {len(symbols)} symbols; {len(candidates)} DSP method candidates')
