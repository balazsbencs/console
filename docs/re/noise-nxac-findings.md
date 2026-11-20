# Noise sample containers: NXAC v1.1

This port uses the sixteen supplied Noise assets in
`analysis/unpacked/sound/samples.xpak` and the hash-pinned RC-20 1.0.5
x86_64 Mach-O. The source tree and CI contain only code and synthetic tests.
The comparison operates on the local assets in memory and records counts,
digests and error statistics.

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
remaining payload, format IDs, gains, deltas and output pointers to the decoder
at 0x151819. kMaxResiduals is 0 followed by `(1/16, 1/64, 1/256)` repeated
three times.

## PCM reconstruction

Each 64-frame block starts with two index bytes for each active component,
mid first. The payload interleaves mid and side groups of four residual codes.
The 4-, 6- and 8-bit codes are unsigned and packed most significant first.
Subtract 7.5, 31.5 or 127.5 respectively, then multiply by the indexed gain
and format scale.

The gain initializer calculates `1 / float(exp2((index - 30) / 10.0))`,
with double precision in the exponential and float division. Delta index
zero starts at zero. Other indices use
`float(sign * exp2(abs(index - 128) / 7.9375) * 2^-16)`, where the sign is
negative for indices below 128. The exponential and multiplication are double
precision. Both tables match all 512 floats from the isolated native initializer.

| Format | Reconstruction in one component |
| --- | --- |
| 0 | Zero component, no index bytes or residuals |
| 1–3 | Add the residual to the indexed delta |
| 4–6 | Accumulate the residual into position, starting at the indexed delta |
| 7–9 | Accumulate the residual into velocity, then velocity into position. Velocity starts at zero and position at the indexed delta |

Predictors reset every 64 frames. The stereo transform computes left as
mid plus side and right as mid minus side. The C implementation retains the
original float operation order and builds with contraction disabled.

| REA evidence | ID |
| --- | --- |
| Constructor and table initialization | `ev_3747c0f7415534f262d22b2f5cd7d6dae40a3bbd72582b84d1caf7bee4071066` |
| RenderBlocks | `ev_53f7cfd6b242e45bc494da0d83569ed7b8c5a54fd936f05a973b7e65393bf8e4` |
| DecompressStereoBlockA | `ev_812b8924c15947b14a6438e1b3d1d05b3bb42adceaeebaca6e4dbeeae0b006b9` |

## C loading and playback

Call `rs_nxac_open` on immutable encoded bytes. It validates the stereo v1.1
header, table, format IDs and payload spans. `rs_nxac_decode_cluster` writes a
full 2048-frame cluster to caller-owned planar buffers. Use `rs_nxac_decode_pcm`
to decode the exact logical frame count and trim the final cluster padding.
Do file I/O and decoding outside the audio callback.

Pass the decoded buffers, length and rates to `rs_noise_player_prepare`.
`rs_noise_player_process` generates stereo PCM with phase continuous across
calls, direct copies for equal rates and the recovered cubic interpolation
for fractional stepping. Playback starts at frame zero and wraps all
interpolation taps across the end of the clip. The whole-clip wrap is a study
choice. Original stack scheduling and type change behavior still need native
validation. The standalone Noise Ducking CLAP continues to accept texture and
sidechain tracks from the host. The sample player is available through the shared
C library.

## Validation

The Linux x86_64 probe runs the native numerical table initializer and
RenderBlocks over synthetic object state. It supplies the validated compressed
payload from the local files and compares planar PCM in memory. All 5,407
clusters and 22,147,072 float values, including cluster padding, match
bit for bit. The whole-file PCM API independently matches the 22,106,624
logical values. A further 400 synthetic clusters exercise all 100 format pairs,
including absent components, and match 1,638,400 float values bit for bit.

The CI test uses synthetic PCM and compressed bytes. It covers header and
offset rejection, padding trim, predictor reset, integer and fractional
playback wraps and host buffer partitioning. The native comparison covers
the isolated decoder. It leaves host startup, the original stack cursor and
the complete Noise graph unverified.

```sh
python3 tools/inspect_noise_nxac.py
python3 tools/compare_native_nxac.py --library build-release/libretro_dsp.so
```
