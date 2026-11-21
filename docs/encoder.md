# libbink2 encoder

`libbink2` encodes `KB2j` video: 8-bit YUV 4:2:0, no alpha, with an optional
single Bink Audio 2 track. The encoder is a separate static library
(`libbink2enc.a`, CMake target `bink2_encoder`) and is also exposed by the
`bink2_build` command.

## Containers and frames

The writer produces the `KB2j` container with the same 48-byte fixed header
as the decoder consumes, followed by zero or one audio track descriptor and
then the `(frame_count + 1)` word index. The index key bit and the frame flags
are set together:

- keyframe (intra) frames use video flags `0x76018` and write the row-CBP
  block;
- inter frames use `0x6e018` and omit that block.

The container video-flags low bits select the slice count: `0x10` = 2,
`0x11` = 3, `0x12` = 4, `0x13` = 8. Slice boundaries are computed exactly as
the decoder does for `KB2h+`:

```
aligned_h = (height + 31) & ~31
end = aligned_h + 32 * slices - 1
start += ((end - start) / (slices - i)) & ~31
end -= 32
```

Each slice starts on a 32-bit boundary; the header stores the byte offsets of
slices 1..n-1.

## Macroblocks

A frame is a sequence of 32x32 luma macroblocks. Intra frames write the
quantiser delta, luma CBP/DC/AC, chroma V, then chroma U. Inter frames write
the LRU block type (skip / motion / residual; intra-in-inter is not emitted
yet), then:

- **skip** copies the macroblock from the reference and writes no MV;
- **motion** writes one or four vectors and motion-compensates without a
  residual;
- **residual** writes the vectors, the quantiser delta, then the
  motion-compensated residual CBP/DC/AC and the chroma-present bit.

The DC predictor is the decoder's 2-D predictor. The encoder inverts it
element by element: for each DC in turn it predicts with the already-chosen
values, then writes the difference. This matters because the predictor chains
`dc[i] = dc[i-1] + tdc[i]`; replacing it with a single zero-delta prediction
does not reconstruct the block.

The forward transform is the exact inverse of the integer IDCT's 8x8 matrix,
not a textbook cosine DCT (the decoder butterfly's basis is
`[1.1875, 1, 0.6875, 0.25, ...]`). Quantisation and dequantisation use the
decoder's matrices and rounding, so the reconstruction written by the encoder
and by the decoder are byte-identical.

## Options

| Option | Meaning |
| --- | --- |
| `slices` | 2, 3, 4 or 8 |
| `keyframe_interval` | frames between keyframes; `0` = all intra |
| `motion_range` | luma search radius, 0..64; `0` forces skip copies |
| `four_mv` | one or four vectors per macroblock |
| `quality` | 0..100 in quality mode |
| `target_bitrate` | video bits per second, excluding audio; `0` selects quality mode |
| `audio_quality` | 0..100 for the audio track; ignored without audio |
| `verify` | decode the result after writing |
| `recon_path` | tight raw YUV 4:2:0 reconstruction dump |
| `verbose` | progress on stderr |

`bink2_build` maps the same options to command-line flags and reads `.ppm`,
`.bmp`, `.png`, `.jpg`, and `.jpeg` frames from a directory (natural sort) or
a single image.

## Audio

One optional audio track is written as Bink Audio 2 with the DCT transform.
Set `source.audio.channels` to 1 or 2, `source.audio.rate` to 1..65535 Hz, and
`source.audio.sample_count` to the number of samples per channel, then supply
`read_audio`. There is no resampling: the container stores the source rate.

Audio runs for the video duration. Samples past the end are ignored, and a
short track is padded with silence. `options.audio_quality` is 0..100 and
scales the per-band quantiser; it does not target a bitrate. A coefficient that
would exceed the block syntax's 15-bit width is clamped, and `--verbose`
reports the clipped block.

The audio output is lossy and is not compared byte for byte. `verify` decodes
every audio chunk and requires its total PCM size to match the encoder's, and
the tests compare the decoded signal with the source at a measured SNR.
The block syntax and quantiser layout are documented in
[audio-encoder-design.md](audio-encoder-design.md).

## Rate control

Quality mode maps quality to a base quantiser (`q = 8 + (100-quality)/5`,
clamped 1..36). Target-bitrate mode starts at `q = 12` and adjusts it by 1..3
per frame toward `target_bitrate * fps_den / (fps_num * 8)` bytes of video
payload. Audio chunks are not counted, so audio adds its own bitrate on top
of the target. A frame
whose mean luma SAD against the previous source frame exceeds 24 is marked a
scene cut and coded as a keyframe, with the index bit and frame flags kept in
sync.

## Reconstruction guarantee

For every encoded frame the encoder reconstructs with the same dequantise,
integer IDCT, put/add, and motion-compensation functions the decoder uses,
and then runs the decoder's own filter stages (`bink2_video_apply_filters`:
DC reconstruction, then deblocking) on the result before it writes the recon
and copies the picture into the reference. The filters see the decoder's
inputs because the encoder sets them up with decoder code:

- each frame starts with `bink2_video_begin_frame`, which stores the frame
  flags and zeroes the motion vectors, so skip macroblocks have zero
  vectors as they do in the decoder;
- the edge flags come from `bink2_video_read_frame_edges` reading the frame
  header the encoder has just written;
- the per-block info words are stored with the decoder's helpers.

`recon_path` therefore matches the decoder's visible planes exactly with the
filters on (the default); the tests compare every frame byte for byte.
Motion search, mode decisions, and rate control may be lossy, but they never
make the decoder disagree with the reconstruction.

## Known divergence

The reconstruction is the filtered picture. DC reconstruction has no
frame-flag opt-out, so a decoder that skips the filter stages, either
libbink2 with `bink2_enable_filters(b, 0)` or the pinned FFmpeg reference,
shows pixels that differ from `recon` in DC-only blocks, and the difference
carries into later inter frames through the reference. This drift is
accepted. With the frame flags and edge arrays the encoder writes,
deblocking and the residual edge filter change no pixels in its `KB2j`
output; only DC reconstruction does.

`test_enc_inter` checks both halves. `test_state_matches_decoder` encodes a
short clip with the internal API, decodes each payload, and requires the
encoder's frame flags, edge flags, motion vectors, info words and picture
to equal the decoder's. On each frame it also requires that the residual
edge filter selects no direction, and that a filters-off decode followed by
DC reconstruction alone gives the filtered picture, so deblocking changed
nothing. The end of `main` decodes the encoded file with the filters off
and requires its keyframe, which has DC-only blocks in a flat band, to
differ from `recon`.

## Validation

`tools/validate.sh file.bk2` runs the pinned FFmpeg Bink 2 oracle with
`-v error` and fails on any diagnostic. The pinned oracle has a
`slice_height[4]` array bug in its 8-slice path, so it rejects valid 8-slice
files; the script exits 77 (the project's skip code) for those and the
library's own decoder round trip covers them. Set `FFMPEG_BINK2` to point
at a different build. Set
`BINK2ENC_BIN` and `BINK2_VALIDATE_YUV` (plus optional
`BINK2_VALIDATE_WIDTH/HEIGHT/FPS`) to also encode the same raw clip with the
independent `bink2enc` and print both file sizes.

`options.verify` decodes the just-written file with `libbink2` and compares
every visible plane to the in-memory reconstruction, returning
`BINK2_ENC_ERR_VERIFY` on a mismatch. `bink2_build --verify` uses it. With
audio, it also requires the decoded PCM byte count to equal the sum of the
per-frame audio sizes.

`test_enc_audio_oracle` compares the encoder's audio with the pinned FFmpeg
Bink 2 decoder when `BINK2_ORACLE` points at that binary; otherwise it skips
with exit code 77. It does not modify the golden checksums.
