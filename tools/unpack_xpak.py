#!/usr/bin/env python3
"""Read the local XPak cx/Cx format recovered through REA; execute no content.

Decoder evidence: cXPakXOrInputStream::Read (0x17ad10),
cXPakCompositeInputStream::lazyOpen (0x17ade0), cXPakEntryInfo::Read
(0x17b460), cXPakCRCInputStream::Read (0x17a9e0), RC-20 1.0.5 x86_64.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import zlib


def decode_stream(data):
    end = data.index(0)
    codecs = data[:end]
    result = data[end + 1:]
    for codec in reversed(codecs):
        if codec == ord('x'):
            key = 0x12345678
            out = bytearray(len(result))
            for index, value in enumerate(result):
                key = (key * 0x569CF9F3 + 0xF923C1D1) & 0xFFFFFFFF
                out[index] = value ^ (key >> 24)
            result = bytes(out)
        elif codec in (ord('c'), ord('C')):
            if len(result) < 4 or zlib.crc32(result[:-4]) != struct.unpack('<I', result[-4:])[0]:
                raise ValueError('XPak CRC mismatch')
            result = result[:-4]
        else:
            raise ValueError(f'Unsupported XPak codec: {chr(codec)!r}')
    return result


def catalog(data):
    if data[:4] != struct.pack('<I', 0x192CE3F7):
        raise ValueError('Invalid XPak magic')
    size = struct.unpack('<I', data[-4:])[0]
    if not 0 < size <= len(data) - 8:
        raise ValueError('Invalid catalog size')
    table = decode_stream(data[-4 - size:-4])
    cursor = 0
    entries = []
    while cursor < len(table):
        offset, length, flags, unknown1, unknown2, created, modified, unpacked, name_size = \
            struct.unpack_from('<QQIIIQQQI', table, cursor)
        if not 0 < name_size <= 10000 or cursor + 56 + name_size > len(table):
            raise ValueError('Invalid XPak name size')
        name = table[cursor + 56:cursor + 56 + name_size].rstrip(b'\0').decode('utf-8')
        if offset + length > len(data) - 4 - size:
            raise ValueError('Entry exceeds payload bounds')
        entries.append(dict(name=name, offset=offset, size=length, flags=flags,
                            unknown1=unknown1, unknown2=unknown2, unpacked=unpacked))
        cursor += 56 + ((name_size + 3) // 4) * 4
    if cursor != len(table):
        raise ValueError('Invalid catalog alignment')
    return entries


def unpack(data, target, records, prefix=''):
    for entry in catalog(data):
        name = PurePosixPath(entry['name'].replace('\\', '/'))
        if name.is_absolute() or '..' in name.parts or not name.parts:
            raise ValueError('Unsafe archive path')
        content = data[entry['offset']:entry['offset'] + entry['size']]
        path = target.joinpath(*name.parts)
        logical = prefix + name.as_posix()
        if content[:4] == struct.pack('<I', 0x192CE3F7):
            unpack(content, path, records, logical + '/')
        else:
            payload = decode_stream(content)
            if len(payload) != entry['unpacked']:
                raise ValueError(f'Size mismatch for {logical}')
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
            records.append(dict(path=logical, bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest()))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output must be a new directory')
    data = args.input.read_bytes()
    records = []
    unpack(data, args.output, records)
    manifest = dict(source=str(args.input), sha256=hashlib.sha256(data).hexdigest(), files=records)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / '_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Extracted {len(records)} CRC-verified files into {args.output}')
