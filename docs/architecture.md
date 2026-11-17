# Architecture

This page maps the source tree and explains the few design decisions that
shape everything else. Read it before changing decoder or encoder code.

## Layout

```
src/                 decoder library (libbink2) and shared codec code
src/encoder/         encoder library (libbink2enc)
src/tools/           bink2_build, bink2_dump, bink2_sdl_player
tests/               CTest programs, golden checksums, fixtures
tests/media/         small committed fixtures
third_party/stb/     stb_image.h, used only by the encoder's image loader
tools/validate.sh    oracle check for encoded files
docs/                format notes, API and tool reference
```

## Decoder

| File | Responsibility |
| --- | --- |
| `bink2.h` | The only installed decoder header. |
| `bink2.c` | Public API, container header and frame index parsing, frame packet loading, truncation policy. |
| `bink2_internal.h` | `struct bink2_t` and header limits. |
| `bink2_bits.c/.h` | LSB-first bit reader, plus the bit writer the encoder uses. |
| `bink2_video.c/.h` | Plane allocation, slice layout, motion compensation, DC prediction, RGB conversion, motion-vector storage. |
| `bink2_video_old.c` | `KB2f` slice syntax (float IDCT, per-component quantisers). |
| `bink2_video_new.c/.h` | `KB2g`+ slice syntax (integer IDCT, LRU block types). |
| `bink2_dsp.c/.h` | IDCTs and block put/add. |
| `bink2_tables.c/.h` | Codebooks, scan orders, quantiser matrices. |
| `bink2_audio.c/.h` | Bink Audio 2. |

A frame call flows like this:

1. `bink2.c` reads the frame packet using the index.
2. The packet is split into one audio chunk per track, then the video
   payload.
3. `bink2_video_decode()` reads the frame flags and slice offsets, then calls
   the old or new slice decoder per slice depending on the revision.
4. Each enabled audio track decodes its chunk.

Decoded planes are padded to 32-sample macroblock boundaries; the public
getters report the stride and callers crop to the visible size. RGB is
produced lazily on the first `bink2_get_video()` call for a frame.

## Encoder

| File | Responsibility |
| --- | --- |
| `bink2_encoder.h` | The only installed encoder header. |
| `bink2_encoder.c` | Option validation, the frame loop, keyframe and scene-cut decisions, rate control, `verify`. |
| `bink2_enc_container.c` | `KB2j` header and frame index. |
| `bink2_enc_video.c` | Per-frame and per-macroblock mode decision and coding. |
| `bink2_enc_motion.c` | Motion search. |
| `bink2_enc_dct.c` | Forward transform and quantisation. |
| `bink2_enc_syntax.c` | Bitstream writers that mirror the decoder's readers. |
| `bink2_enc_bits.c` | Bit counting for mode decisions. |
| `bink2_enc_image.c`, `bink2_enc_stb.c` | Image loading for `bink2_build`, RGB to YUV. |

## Design decisions

### The pinned FFmpeg tree is the correctness target

RAD has not published a Bink 2 specification. The decoder is a port of the
unmerged FFmpeg Bink 2 patches at one pinned commit, and the golden checksums
in `tests/reference_checksums.h` are generated from that same build. "Correct"
means bit-identical video to the pinned oracle, not to RAD's decoder. The one
known gap between the two is the in-loop deblocking filter, which neither the
oracle nor libbink2 implements. See [bink2.md](bink2.md) and
[tests/README.md](../tests/README.md).

### The encoder reconstructs with decoder code

The encoder links the decoder and calls the same dequantise, IDCT, put/add,
and motion-compensation functions to build its reference frames. The encoder's
reconstruction is therefore byte-identical to what the decoder produces, and
the tests assert that for every frame. A change to shared codec code affects
both libraries at once; a private reimplementation on the encoder side would
break this guarantee.

### Two syntax generations, one frame layer

`KB2f` and `KB2g`+ share the container, slice framing, motion compensation,
and plane handling in `bink2_video.c`, and differ in the slice body. Keep
revision-specific syntax in `bink2_video_old.c` or `bink2_video_new.c`.

### Untrusted input

Any `.bk2` file may be hostile. Header fields are bounded at open
(`bink2_internal.h`), every frame packet is checked against the real file
size, and bit readers are bounds-checked. `tests/test_malformed.c` feeds
truncated and corrupted streams through the decoder; new parsing code needs
a case there.

### libbink compatibility

The decoder API mirrors [libbink](https://github.com/raynorpat/libbink): an
opaque handle, `char` status returns, `unsigned long` sizes, and a `mode`
argument that is accepted but unused. These are kept deliberately so libbink
callers can port with a rename.

## Design history

`docs/superpowers/` holds the original design spec and implementation plans.
They record intent at the time of writing and are not kept in sync with the
code. Where they disagree with this page or the source, the source wins.
