# Architecture

This page maps the source tree and explains the few design decisions that
shape everything else. Read it before changing decoder or encoder code.

## Layout

```
src/                 decoder library (libbink2) and shared codec code
src/encoder/         encoder library (libbink2enc)
src/tools/           bink2_build, bink2_dump, bink2_sdl_player
src/tools/player/    renderer players (OpenGL, Metal, Vulkan, Direct3D)
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
| `bink2_dsp.c/.h` | IDCTs, block put/add, motion compensation, YUV to RGB rows; scalar `*_c` references and the dispatch to SIMD. |
| `bink2_dsp_neon.c`, `bink2_dsp_sse2.c` | NEON and SSE2 versions of the DSP routines, compiled in when the target has them. |
| `bink2_filter.c/.h` | RAD's post-reconstruction stages: which revisions each stage covers, and the deblocking edge primitive. The frame-level passes are not wired in yet. |
| `bink2_tables.c/.h` | Codebooks, scan orders, quantiser matrices. |
| `bink2_audio.c/.h` | Bink Audio 2. |
| `bink2_thread.c/.h` | The slice thread pool (POSIX or Win32 threads), CPU count, `BINK2_THREADS`. |

A frame call flows like this:

1. `bink2.c` reads the frame packet using the index.
2. The packet is split into one audio chunk per track, then the video
   payload.
3. `bink2_video_decode()` reads the frame flags and slice offsets, then runs
   the old or new slice decoder for every slice, on the handle's thread pool
   when the frame has more than one slice. It then checks that each slice
   ended before the next one starts, as a serial walk would.
4. Each enabled audio track decodes its chunk.

Decoded planes are padded to 32-sample macroblock boundaries; the public
getters report the stride and callers crop to the visible size. RGB is
produced lazily on the first `bink2_get_video()` call for a frame, in row
bands on the slice threads when the handle has them.

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
| `bink2_enc_audio.c` | Bink Audio 2 track: scheduling, forward DCT, block writer. |
| `bink2_enc_wav.c` | PCM WAV loading for `bink2_build --audio`. |
| `bink2_enc_image.c`, `bink2_enc_stb.c` | Image loading for `bink2_build`, RGB to YUV. |

## Design decisions

### The pinned FFmpeg tree is the correctness target

RAD has not published a Bink 2 specification. The decoder is a port of the
unmerged FFmpeg Bink 2 patches at one pinned commit, and the golden checksums
in `tests/reference_checksums.h` are generated from that same build. "Correct"
means bit-identical video to the pinned oracle, not to RAD's decoder. The one
known gap between the two is the in-loop deblocking filter, which neither the
oracle nor libbink2 implements.

The `KB2f` float IDCT depends on how multiply-adds are rounded. The oracle's
build fused three of them, so `idct_1d_old` writes those three with `fmaf()`
and the libraries build with `-ffp-contract=off` (MSVC's `/fp:precise` does
not contract either). `KB2f` output is then identical on every platform and
compiler instead of depending on whether the compiler happens to fuse. See [bink2.md](bink2.md) and
[tests/README.md](../tests/README.md).

### The encoder reconstructs with decoder code

The encoder links the decoder and calls the same dequantise, IDCT, put/add,
and motion-compensation functions to build its reference frames. The encoder's
reconstruction is therefore byte-identical to what the decoder produces, and
the tests assert that for every frame. It also predicts quantisers and motion
vectors with the decoder's `bink2_new_predict_q()` and
`bink2_new_predict_mv()`, so the deltas it writes are against the same
predictions the decoder makes. A change to shared codec code affects
both libraries at once; a private reimplementation on the encoder side would
break this guarantee.

Audio follows the same idea with a weaker guarantee. The audio encoder takes
its frame length, bands, and quantiser table from the decoder's
`bink2_audio_describe()`, and its tests decode every block with the decoder.
The transform is lossy, so audio is checked by SNR and PCM length, never byte
for byte. See [audio-encoder-design.md](audio-encoder-design.md).

### Two syntax generations, one frame layer

`KB2f` and `KB2g`+ share the container, slice framing, motion compensation,
and plane handling in `bink2_video.c`, and differ in the slice body. Keep
revision-specific syntax in `bink2_video_old.c` or `bink2_video_new.c`.

### Slices are independent

A frame's slices share no decoding state. Everything a slice predicts from
(DC values, motion vectors, quantisers, the current macroblock position) lives
in its own `Bink2Slice` in `Bink2Video.slice[]` and is reset at the slice's
first row. A slice writes only its own macroblock rows of the current picture
and the motion arrays, and reads the reference picture, which is complete
before the frame starts. The encoder, which codes one slice at a time, uses
`slice[0]`.

That is what lets slices run in parallel: `decode_impl()` validates every
slice offset, hands each slice its own bit reader on a `Bink2Pool`, and after
joining applies the same between-slice and end-of-packet checks the serial
walk did, so a damaged frame fails identically either way. The pool is
created lazily, so a `Bink2Video` that never decodes (the encoder's) never
starts threads. `test_threads` compares threaded and serial output frame by
frame.

### SIMD is compile-time and bit-exact

`bink2_dsp.h` defines `BINK2_SIMD_NEON` on arm64 and `BINK2_SIMD_SSE2` on
x86-64 (unless `BINK2_NO_SIMD`, from `-DBINK2_SIMD=OFF`), and each public DSP
routine calls the matching version. There is no runtime CPU detection: both
are baseline for their architecture. Every SIMD routine must equal its scalar
`*_c` reference bit for bit, including saturation and truncation;
`test_dsp_simd` compares them on random and edge-case inputs. The `KB2f` float
IDCT stays scalar because reordering float operations changes its output; its
float-to-pixel block writes are vectorised on SSE2 and AArch64, where the
conversion rounds half to even like `lrintf`.

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
