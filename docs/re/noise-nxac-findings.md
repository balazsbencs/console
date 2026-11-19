# Noise sample containers: NXAC v1.1

Scope: the sixteen supplied Noise assets under
`analysis/unpacked/sound/samples.xpak`, the hash-pinned RC-20 1.0.5 x86_64 binary,
and packaged Noise declarations. This pass reads metadata and native assembly.
It does not redistribute or export decoded samples, execute resource scripts,
or claim a working portable NXAC decoder.

`tools/inspect_noise_nxac.py` independently validates every local container and
writes metadata/hashes to `analysis/re/noise-nxac-inventory.json`. All sixteen
files pass structural validation: **14,594,032 encoded bytes**, **11,053,312
stereo frames**, all at **44,100 Hz**. Source names below are embedded encoder
provenance, not proof that the original WAV files are present.

| ID | Embedded source basename | Frames | Seconds | Clusters |
| --- | --- | --- | --- | --- |
| 000 | 0-TubeU47.wav | 800832 | 18.159 | 392 |
| 001 | 1-WhiteJ8.wav | 668288 | 15.154 | 327 |
| 002 | 2-DCNoise.wav | 884928 | 20.066 | 433 |
| 003 | 3-MuffStomp.wav | 717440 | 16.268 | 351 |
| 004 | 04-Vinyl.wav | 589952 | 13.378 | 289 |
| 005 | 05-Tape75ips.wav | 529152 | 11.999 | 259 |
| 006 | 06-Tape15ips.wav | 529216 | 12.000 | 259 |
| 007 | 07-Fairfax noise.wav | 748864 | 16.981 | 366 |
| 008 | 08-VHS.wav | 477120 | 10.819 | 233 |
| 009 | 09-Cassette.wav | 769344 | 17.445 | 376 |
| 010 | 990101_010_00_v700_Noise [ABC].wav | 800832 | 18.159 | 392 |
| 011 | 11-Pink-ish.wav | 788288 | 17.875 | 385 |
| 012 | LE_NOISE_VINYL_X.wav | 744384 | 16.879 | 364 |
| 013 | 13-Radio scan.wav | 668224 | 15.152 | 327 |
| 014 | 14-Office Amb.wav | 668224 | 15.152 | 327 |
| 015 | 15-spaceship.wav | 668224 | 15.152 | 327 |

NoiseTypeSpec in `analysis/unpacked/guispec/Config.lua:51–68` supplies the
user-facing ordering and maps these numeric IDs to Vinyl, tape, Tube, DC, Muff,
Radio, VHS, Cassette, 8-bit, White, Pink-ish, Studio and Apollo labels. Its order
is different from file-ID order. The stack text, e.g.
`analysis/unpacked/sound/990101_000_Noise.stackv3:21`, references its NXAC asset
with HeadLth=800832, JumpTo=0, At=0. Do not assume these fields alone establish
a runtime seamless-loop/crossfade policy.

## Container layout established across all sixteen files

All multibyte values below are little endian. Floats are IEEE float32.

| Offset | Type | Observed value / role |
| --- | --- | --- |
| 0x000 | 256-byte area | ASCII `NXAC v1.1`, encoder options, original source path, 0x1a, zero padding |
| 0x100 | u32 | signature 0x3a5920b3 |
| 0x104 | u32 | channels=2 |
| 0x108 | u32 | logical audio frame count |
| 0x10c | u32 | cluster size=2048 audio frames |
| 0x110 | u32 | codec block size=64 audio frames |
| 0x114 | float | sample rate=44100 |
| 0x118 | u32 | cluster table byte offset=0x128 |
| 0x11c | u32 | table bytes=`ceil(frame_count/2048)*16` |
| 0x120 | u32 | encoded-data byte offset=`0x128+table_bytes` |
| 0x124 | u32 | encoded-data byte size=`file_size-data_offset` |

Each 16-byte cluster table entry contains float sample rate, float approximate
RMS, u32 byte offset relative to encoded-data start, and u32 packed formats.
The low and next bytes are two format IDs; upper sixteen bits are zero in this
corpus. RMS interpretation is supported by the existing symbol
`cAENXACSample::GetApproxRMS(int)` at 0x151070; the exact statistical definition
and any normalization remain unverified. Rates are 44100 in every table entry.

All cluster offsets are monotonic and the first is zero. The last cluster also
contains its complete encoded 2048-frame capacity, so logical frame count must
trim decoded padding. Every observed cluster has thirty-two 64-frame codec
blocks; partial final logical blocks must not read beyond the declared count.

## Decoder entry points and block structure

Original Mach-O symbols locate:

| Address | Symbol / purpose |
| --- | --- |
| 0x168440 | cNXACSample constructor: header parsing and constant-table initialization |
| 0x151620 | cNXACSample::LoadCluster(int) |
| 0x1516f0 | cNXACSample::RenderBlocks(int,int,int,float*,float*) |
| 0x151ae0 | DecompressStereoBlockA: specialized packed-residual decoder |
| 0x14f780 | cAENXACSample::ReadStereo(int,int,float*,float*) |
| 0x151070 | cAENXACSample::GetApproxRMS(int) |
| 0x5e8350 | cNXACSample::kMaxResiduals float table |
| 0x8900b0 | cNXACSample::gInitialized |
| 0x8900c0 | cNXACSample::gGainConstants[256] |
| 0x8904c0 | cNXACSample::gDeltaConstants[256] |

Raw RenderBlocks disassembly is retained in
`analysis/re/nxac-render-raw.asm`. It limits work to the cluster remainder
`min(32-first_block,requested_blocks)` at 0x151707–0x151710. Its in-memory
cluster records are 24 bytes, with relative payload offset +0xc and format ints
+0x10/+0x14. These differ from the packed on-disk 16-byte table records.

The readonly width table at 0x5e8660 contains `(2,3,4,2,3,4,2,3,4)` for format
IDs 1 through 9. For a nonzero format f, bytes per 64-frame channel block are
`16*width[f-1]+2`: formats 1/4/7 use 34 bytes, 2/5/8 use 50, 3/6/9 use 66.
Zero format contributes no bytes. These correspond to 4/6/8-bit packed sample
residuals, plus two bytes per active component. All observed cluster sizes
match `32*(channel0_block_bytes+channel1_block_bytes)` exactly.

RenderBlocks reads the active components' two-byte gain/delta indices first
(0x1517c3–0x1517fa). It fetches gain from gGainConstants and multiplies it by
kMaxResiduals[format], then fetches delta from gDeltaConstants. It passes the
remaining payload, format IDs, gains/deltas and output pointers to the decoder
at 0x151819. kMaxResiduals is 0 followed by `(1/16,1/64,1/256)` repeated three
times. The specialized decoder consumes packed nibble/six-bit/byte values;
its format-pair jump tables cover IDs 0..9. Different groups of formats
require reconstruction arithmetic in addition to bit unpacking. In one
inspected branch the second reconstructed component is added into the first
output, suggesting component/stereo decorrelation; the complete transform and
predictor interpretation are not yet established and must not be guessed.

## Feasibility and practical loading path

A portable decoder appears feasible: headers and bounded block addressing are
regular, residual packing is directly visible, and decoder tables/functions
are present in the supplied native implementation. The inner inspected path
operates on residual integers and floats; no decryption step was observed in
that bounded path. This does not establish that every format branch or related
container feature is covered. The large native body specializes combinations
of component formats, so a much smaller generic implementation may be possible
once the reconstruction equations are understood.

Required next evidence is constructor-derived gain/delta initialization plus
one representative decode path per format family and stereo combination. Then
compare a portable scalar decoder against the isolated original for all
clusters, preserving native float operation order and padding trims. No
portable decode or native full-sample render was run in this pass. Parent REA
queries for constructor/RenderBlocks/decompressor are separate pending evidence;
this note makes no claim based on results not yet delivered.

The immediate independent path is **user-provided WAV audio**: load and validate
PCM/float data off the audio thread, retain original sample rate and frame
count, convert to owned stereo float buffers, and provide those buffers to the
Noise playback/resampling API. Keep file I/O, allocation and decoding outside
process(). Preserve asset identity in state and explicitly handle missing
files. Mono duplication and any resampling/gain normalization should be
explicit adapter choices. User-owned WAV input makes the Noise DSP and CLAP
reviewable without waiting for NXAC decoding; it cannot by itself establish
sample-for-sample equivalence with the bundled RC-20 Noise sounds.

Reproduce container inspection only:

```sh
python3 tools/inspect_noise_nxac.py
```

The report contains metadata and digests, not audio payloads. This inspection
is intentionally bounded to the sixteen supplied v1.1 Noise containers; other
NXAC versions, mono encodings, corrupt files and streaming concurrency remain
unassessed.
