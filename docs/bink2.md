# Bink 2 format notes

This document describes the Bink 2 container and bitstream as implemented by
`libbink2`. It is a porting reference, not a formal specification. RAD Game
Tools has not published a specification; the description follows the
unmerged FFmpeg Bink 2 patch (`bink2.c`, `bink2g.c`, `bink2f.c`,
`binkaudio.c`) at commit `f4904840e820bfd0fb8fbc22179b39dfdbd3a9e0` and the
public Reverse-engineering notes on MultimediaWiki and
<https://codecs.multimedia.cx>. Where they differ, libbink2 follows the
pinned FFmpeg tree because the golden checksums come from it. The
post-reconstruction filters are the exception; see
[Post-reconstruction filters](#post-reconstruction-filters).

## Attribution

The decoder is derived from:

- `libavcodec/bink2.c`, `bink2g.c`, `bink2f.c`, copyright (c) 2014
  Konstantin Shishkov and (c) 2019 Paul B Mahol.
- `libavcodec/binkaudio.c`, copyright (c) 2007-2011 Peter Ross and (c) 2009
  Daniel Verkamp.

Those files are LGPL-2.1-or-later. libbink2 keeps that license.

## Container

The container is the Bink container with a `KB2x` signature, where `x` is
`f` through `n`. All integers are little-endian.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Signature |
| 4 | 4 | File size minus 8 |
| 8 | 4 | Frame count |
| 12 | 4 | Largest frame packet |
| 16 | 4 | Reserved |
| 20 | 4 | Width |
| 24 | 4 | Height |
| 28 | 4 | FPS numerator |
| 32 | 4 | FPS denominator |
| 36 | 4 | Video flags |
| 40 | 4 | Audio track count |

From `KB2i` onward one 32-bit field follows the track count. Then come the
per-track maximum decoded sizes (4 bytes each), the per-track descriptors
(24-bit sample rate and an 8-bit flag byte: `0x10` DCT, `0x20` stereo,
`0x40` 16-bit), and the per-track IDs (4 bytes each). The frame index is
`frame_count + 1` 32-bit entries; bit 0 is the keyframe flag and the rest is
the byte offset. The last entry is the end offset. Frame packets contain one
audio chunk per track (`u32 size` + payload) followed by the video payload.

The audio payload begins with a 32-bit reported decoded size; the audio
bitstream follows.

## Frame and slices

A video frame begins with a 32-bit frame-flags word and one 32-bit offset per
remaining slice. Flag `0x10000` adds row and column CBP bit arrays before the
slices; flags `0x8000` and `0x4000` suppress the row and column arrays
respectively.

Slice counts depend on the revision:

- `KB2f`: two slices.
- `KB2g`: one slice when the 32-aligned height is below 128, otherwise two.
- `KB2h` and later: the container video flags' low two bits select 2, 3, 4, or
  8 slices.

Each slice is a sequence of 32x32 luma macroblocks (16x16 chroma) from the
slice's start row to its end row. A slice starts at the next 32-bit word
after the previous one.

## Video bitstream

There are two generations of syntax.

`KB2f` uses a floating-point IDCT, per-component quantisers, old CBP and
DC/AC coding, and the old value/skip codebooks. `KB2g` and later use an
integer IDCT, one quantiser per macroblock, LRU block-type coding, unary DC
coding, and the new AC skip codebooks.

Macroblock types are intra, skip, motion, and motion plus residual. Motion
uses half-pel luma and quarter-pel chroma. The luma and chroma interpolation
filters are:

```
luma:  (A - 4*B + 19*C - 4*D + E + 1) >> 5
chroma 1/2: (A + B + 1) >> 1
chroma 1/4: (6*A + 2*B + 4) >> 3
```

DC and motion vectors are predicted from the top, left, and top-left
neighbours with median-style predictors. Prediction state resets at each
slice edge. Intra macroblocks in inter frames can average neighbouring
blocks' DC values when those neighbours are not intra.

The integer IDCT writes transposed coefficients: `bink2_put_block` reads
`block[j * 8 + i]` for output row `i`.

## Audio

Bink 2 uses Bink Audio 2: the same transform layout as Bink Audio 1, but with
7-bit quantiser indices and a different coefficient loop. Each chunk can hold
several transform blocks; libbink2 decodes blocks until the chunk ends. The
first block's overlap tail is used by the next block; seeking clears it.

## Post-reconstruction filters

RAD's decoder runs three stages that the pinned FFmpeg tree and NihAV lack.
libbink2 implements them, derived from study of RAD's decoder. All three are
in-loop: the next frame predicts from the processed picture, not the raw
reconstruction. They are on by default and turned off with
`bink2_enable_filters(b, 0)`, which takes effect at the next keyframe.

- **Residual edge filter (R).** It applies to inter macroblocks that carry
  both motion and a coded residual, in new-syntax frames. It smooths the
  internal 8x8 edges of the residual after the IDCT and before the residual
  is added to the prediction. Frame flag `0x40000` disables it, `0x4000`
  drops the vertical edges and `0x8000` the horizontal ones.
- **DC reconstruction.** An intra block with no AC coefficients is replaced
  by a smooth surface interpolated from the DC values of usable neighbouring
  blocks. No frame flag disables it. Frames with flag `0x2000` use an integer
  form and `KB2f` frames use a floating-point form.
- **Deblocking (DB).** A 4-tap filter across the 8x8 block grid on Y, U and
  V. Frame flag `0x4000` disables vertical edges and `0x8000` horizontal
  edges, and the per-row and per-column arrays of flag `0x10000` disable
  single edges. A frame whose arrays run past the end of the packet is
  rejected.

DC reconstruction and deblocking run as one whole-frame pass after every
slice has been decoded. Residual filtering happens inside macroblock
decoding.

Coverage by revision:

| Revisions | R | DC | DB |
| --- | --- | --- | --- |
| `KB2f` | none (syntax has no such macroblocks) | yes | yes |
| `KB2g`, `KB2h`, `KB2i`, `KB2j`, `KB2k`, `KB2m` | yes | yes | yes |
| `KB2l`, `KB2n` | no | no | no |

### Remaining differences from RAD

- `KB2l` and `KB2n` are not covered; the RAD decoder that was studied
  rejects them, so they decode without any filter stage.
- RAD chooses the old or new slice syntax per frame from flag `0x2000`;
  libbink2 chooses it once from the revision.
- RAD uses a single slice for `KB2h` and later videos under 128 pixels high.

The FFmpeg-derived golden checksums in `tests/reference_checksums.h` cover
only the filters-off path. `tests/filtered_checksums.h` pins libbink2's own
filtered output; it is a regression guard, not a correctness reference.
