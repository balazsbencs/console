#!/usr/bin/env python3
"""Compare every local NXAC cluster against isolated original RenderBlocks.

No audio is exported. Report includes source hashes and decoded-buffer hashes.
"""
import argparse
import ctypes as C
import hashlib
import json
import random
from pathlib import Path
import struct
from native_nxac_probe import NativeNXAC,ROOT,HASH


class Sample(C.Structure):
    _fields_ = [('bytes', C.c_void_p), ('size', C.c_size_t), ('frames', C.c_uint32),
                ('cluster_count', C.c_uint32), ('data_offset', C.c_uint32),
                ('sample_rate', C.c_float)]


def identical(actual, expected, context):
    if actual != expected:
        index = next(i//4 for i in range(len(actual)) if actual[i] != expected[i])
        raise AssertionError((context, index, struct.unpack_from('<f', actual, index*4)[0],
                              struct.unpack_from('<f', expected, index*4)[0],
                              actual[index*4:index*4+4].hex(), expected[index*4:index*4+4].hex()))


def synthetic(mid, side, pattern):
    rng = random.Random(10000*pattern+100*mid+side)
    payload = bytearray()
    widths = [0 if fmt == 0 else 2+(fmt-1)%3 for fmt in [mid, side]]
    for block in range(32):
        for channel, fmt in enumerate([mid, side]):
            if fmt:
                payload.extend([(block*17+pattern*43+fmt*13+channel)&255,
                                (block*29+pattern*61+fmt*7+channel)&255])
        for group in range(16):
            for width in widths:
                for i in range(width):
                    value = [0, 255, 0x55 if (i+group)%2 else 0xaa,
                             rng.randrange(256)][pattern]
                    payload.append(value)
    encoded = bytearray(0x138)
    encoded[:10] = b'NXAC v1.1\n'
    struct.pack_into('<5If4I', encoded, 0x100,
                     0x3a5920b3, 2, 2048, 2048, 64, 48000., 0x128, 16, 0x138, len(payload))
    struct.pack_into('<ffII', encoded, 0x128, 48000., 0., 0, mid | (side<<8))
    return bytes(encoded+payload)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--library', type=Path, default=ROOT/'build-release/libretro_dsp.so')
    ap.add_argument('--directory', type=Path, default=ROOT/'analysis/unpacked/sound/samples.xpak')
    ap.add_argument('--report', type=Path, default=ROOT/'analysis/re/native-nxac-comparison.json')
    args = ap.parse_args()
    if not args.library.is_file():
        ap.error(f'Library does not exist or is not a file: {args.library}')
    if not args.directory.is_dir():
        ap.error(f'Asset directory does not exist: {args.directory}')
    paths = sorted(args.directory.glob('*.nxac'))
    if not paths:
        ap.error(f'No NXAC assets found: {args.directory}')
    if not args.report.parent.is_dir() or args.report.is_dir():
        ap.error(f'Report parent must exist and target must be a file: {args.report}')
    lib = C.CDLL(str(args.library.resolve()))
    lib.rs_nxac_open.argtypes = [C.POINTER(Sample), C.c_void_p, C.c_size_t]
    lib.rs_nxac_open.restype = C.c_bool
    lib.rs_nxac_decode_cluster.argtypes = [C.POINTER(Sample), C.c_uint32,
                                          C.POINTER(C.c_float), C.POINTER(C.c_float)]
    lib.rs_nxac_decode_cluster.restype = C.c_bool
    lib.rs_nxac_decode_pcm.argtypes = [C.POINTER(Sample), C.POINTER(C.c_float),
                                      C.POINTER(C.c_float), C.c_size_t]
    lib.rs_nxac_decode_pcm.restype = C.c_bool
    for name in ['gain', 'delta']:
        fn = getattr(lib, 'rs_nxac_'+name)
        fn.argtypes = [C.c_uint8]
        fn.restype = C.c_float
    native = NativeNXAC()
    identical(struct.pack('<256f', *[lib.rs_nxac_gain(i) for i in range(256)]),
              native.gain_bits, 'gain-table')
    identical(struct.pack('<256f', *[lib.rs_nxac_delta(i) for i in range(256)]),
              native.delta_bits, 'delta-table')
    left, right = (C.c_float*2048)(), (C.c_float*2048)()
    counts = dict(files=0, clusters=0, samples=0, pcm_samples=0,
                  native_subrange_calls=0, table_coefficients=512)
    results = []
    pairs = set()
    for path in paths:
        info = native.open(path)
        sample = Sample()
        assert lib.rs_nxac_open(C.byref(sample), native.encoded_buffer, len(native.encoded)), path
        assert sample.frames == info['frames'] and sample.cluster_count == info['cluster_count']
        pcm_left, pcm_right = (C.c_float*sample.frames)(), (C.c_float*sample.frames)()
        assert lib.rs_nxac_decode_pcm(C.byref(sample), pcm_left, pcm_right, sample.frames), path.name
        digest = hashlib.sha256()
        checked = set()
        for cluster in range(sample.cluster_count):
            expected = native.render(cluster)
            assert lib.rs_nxac_decode_cluster(C.byref(sample), cluster, left, right), (path.name, cluster)
            actual = [bytes(left), bytes(right)]
            for channel in range(2):
                identical(actual[channel], expected[channel], (path.name, cluster, channel))
                digest.update(actual[channel])
                frames = min(2048, sample.frames-cluster*2048)
                pcm = pcm_left if channel == 0 else pcm_right
                trimmed = C.string_at(C.addressof(pcm)+cluster*2048*4, frames*4)
                identical(trimmed, expected[channel][:frames*4],
                          ('pcm', path.name, cluster, channel))
                counts['pcm_samples'] += frames
            packed = struct.unpack_from('<I', native.encoded, 0x128+cluster*16+12)[0]
            pair = (packed & 255, (packed>>8) & 255)
            pairs.add(pair)
            if pair not in checked:
                checked.add(pair)
                for start, blocks in [(1, 1), (7, 13), (31, 64)]:
                    sub = native.render(cluster, start, blocks)
                    count = min(blocks, 32-start)*64*4
                    for channel in range(2):
                        identical(actual[channel][start*256:start*256+count], sub[channel],
                                  (path.name, cluster, channel, start, blocks))
                    counts['native_subrange_calls'] += 1
            counts['clusters'] += 1
            counts['samples'] += 4096
        counts['files'] += 1
        results.append(dict(file=path.name, source_sha256=info['sha256'], frames=sample.frames,
                            clusters=sample.cluster_count, decoded_cluster_sha256=digest.hexdigest()))
        print(path.name, sample.cluster_count, 'clusters bit exact', flush=True)
    assert counts['files'] > 0
    synthetic_cases = synthetic_samples = 0
    synthetic_hash = hashlib.sha256()
    for mid in range(10):
        for side in range(10):
            for pattern in range(4):
                native.open_bytes(synthetic(mid, side, pattern))
                sample = Sample()
                assert lib.rs_nxac_open(C.byref(sample), native.encoded_buffer,
                                        len(native.encoded)), (mid, side, pattern)
                expected = native.render(0)
                assert lib.rs_nxac_decode_cluster(C.byref(sample), 0, left, right)
                for channel, actual in enumerate([bytes(left), bytes(right)]):
                    identical(actual, expected[channel], ('synthetic', mid, side, pattern, channel))
                    synthetic_hash.update(actual)
                synthetic_cases += 1
                synthetic_samples += 4096
    print('Synthetic 100 format pairs/four patterns bit exact', flush=True)
    report = dict(reference_sha256=HASH, **counts, format_pairs=sorted(pairs), max_error=0,
                  scope='Original numeric table loop and RenderBlocks versus C, every local asset cluster including padding',
                  files_detail=results, synthetic_cases=synthetic_cases, synthetic_samples=synthetic_samples,
                  synthetic_format_pairs=100, synthetic_decoded_sha256=synthetic_hash.hexdigest(), limitations=['Synthetic sample/cluster objects, no constructor or resource streams',
                                                    'Local NXAC v1.1 stereo corpus; no asset audio exported',
                                                    'Shared Linux libm exp2 for numeric table initialization'])
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'files_detail'}))


if __name__ == '__main__':
    main()
