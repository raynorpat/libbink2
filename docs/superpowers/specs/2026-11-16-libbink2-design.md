# libbink2 design

Date: 2026-11-16
Status: draft for approval

## Intent

`libbink2` is a standalone C99 library for Bink 2 (`.bk2`, `KB2*`) files, built
in the same vein as `libbink`: an opaque handle, a simple open/info/decode/seek
API, a separate encoder library, and small command-line tools. It decodes Bink 2
video and Bink Audio 2, and encodes Bink 2 video.

The library lives in `/Users/raynorpat/Projects/libbink2` and does not modify or
depend on `libbink`.

## Goals

- Decode `KB2f` through `KB2n` video: YUV 4:2:0 plus optional alpha,
  32x32 macroblocks, intra/skip/motion/motion+residual blocks, 1 or 4 motion
  vectors per macroblock, 2/3/4/8 slices.
- Decode Bink Audio 2 (DCT and RDFT tracks), up to 7 tracks, and publish
  host-endian signed 16-bit PCM like `libbink`.
- Read the Bink container: header, audio descriptors, frame index, keyframe
  flags, frame packets, 2 GB ceiling, seek by keyframe.
- Encode `KB2j` video from packed RGB: keyframes and delta frames, the four
  macroblock types, one or four motion vectors, rate control, configurable
  keyframe interval and slices.
- Keep the decoder bit-exact with the reference decoder it is ported from.
  Verification is gated by the media matrix below; paths without golden media
  are implemented and smoke-tested, not claimed bit-exact.
- Keep the encoder's internal reconstruction bit-exact with our own decoder, so
  encode/decode round-trips exactly.
- License: LGPL-2.1-or-later, matching `libbink`.
- Mirror `libbink`'s API feel and tooling: `bink2_dump`, `bink2_build`, and an
  SDL player.

## Non-goals

- `KB2a` (no public decoder understands it).
- Full-resolution Bink 2 (YUV 4:4:4 or RGB) and any block size other than
  8x8 residuals in 4:2:0.
- The official in-loop deblocking filter. Neither reference implementation has
  it; omitting it is documented as a known, bounded divergence.
- Bink 1 (`BIK*`) decoding or encoding.
- Encoding revisions other than `KB2j`.
- Encoder alpha, encoder audio, and the renderer zoo (OpenGL/Metal/Vulkan/D3D)
  in the first release. These can follow without changing the architecture.
- Copying AGPL code from NihAV or `bink2enc`.

## Constraints

- C99, CMake 3.16 or newer, no mandatory third-party dependencies.
- Optional SDL3/SDL2 for the player; vendored `stb_image` for image input.
- 32-bit container offsets and `fseek`, so the same 2 GB file ceiling as
  `libbink`.
- Width/height up to 7680x4800, at most 1,000,000 frames, at most 7 audio
  tracks.
- Decoder ported only from `mrtnmtth/FFmpeg-bink2` at commit
  `f4904840e820bfd0fb8fbc22179b39dfdbd3a9e0` (based on FFmpeg
  `1f9b5fa5815c`, 2022-05-07), files `bink2.c`, `bink2g.c`, `bink2f.c`, and
  `binkaudio.c`. All four carry LGPL-2.1-or-later headers. The port includes
  that tree's fixes: the out-of-bounds motion-compensation reads, the
  column-CBP read for 32-aligned widths, and the Bink Audio 2 packet reset.
- Encoder written originally in C, using the VLC tables and inverse transforms
  that already live in the LGPL decoder port. `bink2enc` is used only as an
  external behavioral oracle (run it, compare output), never as source.

## Platform and determinism

- Primary target: 64-bit macOS with clang (arm64 and x86_64). 0.1.0 also
  builds on Linux and cross-compiles for Windows with mingw-w64, mirroring
  `libbink`; MSVC is best-effort. The SDL player builds wherever SDL3 or SDL2
  is found.
- I/O uses `fseek`/`long` like `libbink`, so the same 2 GB file ceiling
  applies on every platform.
- Floating-point paths (the `KB2f` IDCT and Bink Audio 2) are compiled at the
  same optimization level and with the same FP flags as the pinned oracle
  (`-O3 -fno-math-errno -fno-signed-zeros`, default contraction on
  clang/GCC; `/fp:precise` on MSVC) and without fast-math. The project
  defaults to `Release`; at `-O0` the float IDCT rounds differently and the
  golden frames diverge by ±1. The `KB2g+` integer path is exact on every
  platform.
- Golden checksums record the toolchain that produced them. Video and audio
  must match exactly on the primary macOS/clang build; on other 64-bit
  IEEE-754 builds the float paths are compared with a 1-LSB tolerance until
  that platform is added to the exact-match set.

## Approaches considered

1. **Standalone `libbink2` (chosen).** New repo, own `bink2_*` API, scaffolding
   adapted from `libbink`, Bink 2 decoder and encoder added. No coupling to
   `libbink`, no risk to a working Bink 1 library, room to grow. Costs a few
   thousand lines of duplicated container/tool scaffolding.
2. **Extend `libbink` with `KB2*` detection.** One library and one tool set, but
   turns a working single-codec library into a two-codec one, needs a codec
   vtable and a larger API/encoder surface, and discards the separate repo.
3. **Shared `libbink` core plus a Bink 2 module.** Best long-term maintenance,
   but requires splitting `libbink` into core and codec layers first, plus
   cross-repo versioning. Most work before the first frame decodes.

Encoder provenance was decided separately: an independent C encoder keeps the
project LGPL. Porting `bink2enc` would make the whole library AGPL-3.0.

## Format facts the implementation relies on

- The container is the Bink container with a `KB2x` signature. Header fields
  keep their offsets. From `KB2i` onward there is one additional 32-bit field
  after the audio track count.
- Bink 2 audio descriptors are a 24-bit sample rate followed by an 8-bit flag
  byte (`0x10` DCT, `0x20` stereo, `0x40` 16-bit), then the per-track ID table.
  Reading the rate as 16 bits and the flags as 16 bits also works on known
  files because the flag byte lands in the high half.
- Frame index entries use bit 0 as the keyframe flag. The last entry ends at the
  file size. Frame packets hold each audio chunk (`u32 size` + payload) then the
  video payload.
- A video frame begins with a 32-bit frame-flags word and one 32-bit slice
  offset per remaining slice. Frame flag `0x10000` adds row and column CBP flag
  arrays before the slices.
- Slice counts: `KB2f` uses 2; `KB2g` uses 1 when the padded height is under
  128, otherwise 2; `KB2h` and later use `flags & 3` to select 2, 3, 4, or 8.
- Macroblock types are intra, skip, motion, and motion+residual. Motion is
  half-pel for luma and quarter-pel for chroma, with the filters from the
  reference decoder. Predictors are median-style DC/MV prediction with
  top/left/top-left edge state.
- `KB2f` and earlier use floating-point IDCT, per-component quantisers, and the
  older CBP/DC/AC coding. `KB2g` and later use integer IDCT, one quantiser per
  macroblock, LRU block-type coding, and unary DC coding. The port keeps the two
  paths separate (`bink2_video_old.c`, `bink2_video_new.c`).
- Bink Audio 2 differs from Bink Audio 1 in the 7-bit quantiser index and in
  coefficient coding. The port follows the reference decoder's `version_2`
  path and publishes 16-bit PCM.

## Architecture

```
CMakeLists.txt
src/
  bink2.h                  public decoder API
  bink2_internal.h         container and track state
  bink2.c                  open/info/enable/frame/seek
  bink2_bits.[ch]          LSB-first bit reader
  bink2_tables.[ch]        VLC codebooks, scans, quantiser matrices
  bink2_dsp.[ch]           IDCT, motion compensation, block helpers
  bink2_video.[ch]         common video state, dispatch, planes, RGB
  bink2_video_old.c        KB2f path (from bink2f.c)
  bink2_video_new.c        KB2g+ path (from bink2g.c)
  bink2_audio.[ch]         Bink Audio 2 DCT/RDFT
  encoder/
    bink2_encoder.[ch]     public encoder API and file writer
    bink2_enc_video.c      mode decision, motion search, slices
    bink2_enc_dct.c        forward DCT and quantisation
    bink2_enc_bits.[ch]    bit counting helpers over the shared bit writer
    bink2_enc_image.c      stb_image loader
    bink2_enc_wav.c        WAV loader
  tools/
    bink2_dump.c           headless PPM/YUV dump and checksums
    bink2_build.c          image sequence (+ WAV later) to .bk2
    bink2_sdl_player.c     SDL3/SDL2 playback with audio mixing
tests/
docs/
third_party/stb/
```

The scaffolding (container parsing, bit reader style, plane layout, RGB
conversion, WAV loader, SDL player core, CMake options, test conventions) is
copied from `libbink` and adapted, with attribution in the headers. The public
types and functions are prefixed `bink2_` / `BINK2_` so both libraries can be
linked into one program without collisions.

## Public API

Decoder, mirroring `libbink`:

```c
typedef struct bink2_t *bink2;

bink2 bink2_open_file(const char *filename, unsigned char mode);
bink2 bink2_open_filepointer(FILE *file, unsigned char mode);
bink2 bink2_open_memory(const unsigned char *buffer, unsigned long size);
void  bink2_close(bink2 object);

char bink2_info_all(const bink2 object, unsigned long *frame,
                    unsigned long *frame_count, double *usf);
char bink2_info_video(const bink2 object, unsigned long *w, unsigned long *h,
                      unsigned long *flags);
char bink2_info_audio(const bink2 object, unsigned char *track_mask,
                      unsigned char channels[7], unsigned char bitdepth[7],
                      unsigned long audio_rate[7]);

char bink2_enable_video(bink2 object, unsigned char enable);
char bink2_enable_audio(bink2 object, unsigned char track, unsigned char enable);

char bink2_first(bink2 object);
char bink2_next(bink2 object);
char bink2_seek_keyframe(bink2 object, unsigned long frame);

const unsigned char *bink2_get_video(const bink2 object);
const unsigned char *bink2_get_plane(const bink2 object, int plane);
int bink2_get_stride(const bink2 object, int plane);
const unsigned char *bink2_get_audio(const bink2 object, unsigned char track);
unsigned long bink2_get_audio_size(const bink2 object, unsigned char track);
int bink2_get_motion(const bink2 object, unsigned long *mb_w,
                     unsigned long *mb_h, const int16_t **mv_x,
                     const int16_t **mv_y, const unsigned char **mv_count);
```

`bink2_get_motion` reports motion vectors per 32x32 luma macroblock. The four
arrays hold `mb_w * mb_h * 4` entries, indexed
`(by * mb_w + bx) * 4 + k`. `mv_count[by * mb_w + bx]` is 1 or 4 for a
motion-compensated block and 0 otherwise. Components are in half-pel luma
samples, matching the decoder's internal units; chroma offsets are the same
vectors divided by four. `int16_t` covers the full search range. This is wider
than `libbink`'s per-8x8 signed-char API because Bink 2 has up to four vectors
per macroblock and half-pel units.

Encoder, mirroring `bink_encoder.h`:

```c
typedef struct {
    int width, height;
    unsigned long frame_count;
    unsigned long fps_num, fps_den;
    int (*read_frame)(void *user, unsigned long index, unsigned char *rgb);
    int (*read_frame_yuv)(void *user, unsigned long index, unsigned char *y,
                          unsigned char *u, unsigned char *v);
    void *user;
} bink2_enc_source;

typedef struct {
    int keyframe_interval;   /* default 150; 0 = only frame 0 */
    int motion_range;        /* search radius, 0 disables motion */
    int slices;              /* 2, 3, 4, or 8; default 2 */
    int four_mv;             /* allow four vectors per macroblock */
    int quality;             /* 0..100, maps to quantiser bounds */
    int target_bitrate;      /* KiB/s, 0 = quality mode */
    int verify;              /* decode and compare after writing */
    const char *recon_path;  /* optional raw YUV420 reconstruction dump */
    int verbose;
} bink2_enc_options;

void bink2_enc_options_default(bink2_enc_options *options);
int  bink2_enc_encode(const bink2_enc_source *source,
                      const bink2_enc_options *options,
                      const char *output, char *err, size_t err_size);
```

The source supplies packed RGB24 for each frame, converted with the same
BT.601 matrix `libbink` uses. An additional `read_frame_yuv` callback is
accepted for callers that already have tight 4:2:0 planes; when set it takes
precedence and skips the conversion.

## Encoder behavior

- Signature `KB2j`, 4:2:0, no alpha, no audio in the first release.
- Frame 0 is always intra. Every `keyframe_interval` frames and on detected
  scene cuts, an intra frame is written and flagged in the frame index.
- Every macroblock is tried as: skip, motion (1 or 4 vectors), motion+residual,
  and intra. Candidate selection is rate-distortion: distortion is SAD/SSE on
  the reconstructed block; bits are counted with the encoder's own bit writer.
- Motion search is a predictor- and temporal-guided diamond search over
  half-pel luma positions, with quarter-pel chroma derived from the luma vector.
  All vectors are clamped to the reference window used by the decoder.
- Residuals use the decoder's integer forward/inverse pair, a dead-zone
  quantiser, and the decoder's AC codebooks and scan orders. The encoder
  simulates DC and MV prediction exactly, including slice edges.
- The quantiser is chosen per macroblock from a small candidate set, with
  optional rate feedback to hit `target_bitrate`.
- Slices are written sequentially; the header contains their byte offsets.
  Slice-parallel encoding is a later optimization, not part of the API.
- `verify` decodes the produced file with `libbink2` and compares each visible
  plane against the internal reconstruction byte for byte.

## Decoder behavior and known divergence

- Video output is decoded YUV 4:2:0 planes with decoder strides, plus alpha when
  the container flag is set. `bink2_get_video` lazily converts to packed RGB24.
- Audio is decoded to host-endian signed 16-bit PCM, mixed across enabled tracks
  by the player.
- Seeking walks back to the nearest frame flagged as a keyframe.
- The deblocking filter is not simulated. Output therefore differs from RAD's
  decoder by a few levels around block edges. It matches the FFmpeg reference
  decoder bit for bit, which is the correctness target and the source of the
  golden checksums.
- Malformed input never reads out of bounds. Frame and slice sizes, CBP arrays,
  motion vectors, and audio writes are all validated.
- **Truncated files.** A frame whose start offset is at or past EOF ends the
  stream: `bink2_next` returns `BINK2_LAST` if no bytes of it exist, and
  `BINK2_ERROR` if the frame starts inside the file but its payload is cut
  short. `bink2_seek_keyframe` clamps to the last frame that starts inside the
  file. Index entries past EOF are ignored rather than treated as corrupt.
  `bink2_dump --frames N` stops cleanly at the truncation point.
- **Determinism.** Platform and FP rules are in "Platform and determinism".

## Testing

- **Unit tests, no media:** bit reader and writer, VLC table construction,
  IDCT against the reference constants, motion-compensation filters, DC/MV
  predictors, plane geometry, RGB conversion, container header parsing, and
  malformed-input rejection.
- **Decoder goldens:** `tests/capture_reference.sh` builds or accepts a
  `ffmpeg-bink2` binary, decodes the sample files named by
  `BINK2_TEST_MEDIA`, and writes per-frame FNV-1a checksums for every plane and
  per-track PCM lengths/checksums into `tests/reference_checksums.h`. `ctest`
  then compares the full decode sequence. Tests skip when the media variable is
  unset, exactly like `libbink`.
- **Samples in use:**
  - `ge_video_86l.bk2` — `KB2g`, 1024x576, 490 frames, one stereo DCT track.
    Fully present; golden-gated.
  - `sc13_01_partial.bk2` — `KB2f`, 1280x736, nominal 499 frames, six DCT
    tracks. Confirmed genuinely partial: the header declares 21,171,184 bytes
    but only 10,240,000 are present. Frames 0–235 are complete, frame 236
    starts inside the file but runs past EOF, frames 237+ are absent.
    Golden-gated through frame 235, with the truncation policy above asserted.
  - Both are outside the repo; `BINK2_TEST_MEDIA=$HOME/Downloads`.
- **Coverage matrix.** Claims are limited to what has golden media:

  | Path | Status |
  | --- | --- |
  | `KB2f`, 2 slices, DCT audio | golden-gated (partial sample) |
  | `KB2g`, 2 slices, stereo DCT audio | golden-gated |
  | `KB2h`–`KB2n`, 3/8 slices, alpha, RDFT | best-effort; unit and decode-smoke tests only until media exists |

  The h–n, alpha, and RDFT code is still implemented from the reference and
  exercised by synthetic/unit tests, but the design does not claim bit-exact
  verification for those paths. No further `.bk2` media is available as of
  this design. Adding a sample moves a row into the golden-gated set without a
  design change.
- **Optional cross-check:** decode the same files with NihAV and compare plane
  checksums where its output is complete. NihAV is a test oracle only and is
  never linked or copied.
- **Encoder tests:** synthetic solid/pattern/gradient frames, odd sizes, scene
  cuts, 1- and 4-vector motion, and every block type. Each test encodes, decodes
  with `libbink2`, and asserts byte-exact planes against the encoder's
  reconstruction. A separate script runs the FFmpeg oracle on produced files
  and asserts it accepts every frame.
- **Sanitizers:** the test suite runs clean under ASan and UBSan, as `libbink`'s
  does.

## Tools

- `bink2_dump [--frames N] video.bk2 [outdir]`: PPM frames plus per-frame
  checksums, no SDL.
- `bink2_build <images> [-o out.bk2] [options]`: PPM/BMP/PNG/JPEG frames to
  `KB2j`, with `--verify`, `--fps`, `--keyframe-interval`, `--motion-range`,
  `--slices`, `--quality`, `--bitrate`.
- `bink2_sdl_player video.bk2`: SDL3 first, SDL2 fallback, one renderer, audio
  mixing for every enabled track, Esc/Q quit, Space pause, Left/Right seek.
  The GL/Metal/Vulkan/D3D renderers are a later addition.

## Phases and acceptance

1. **Scaffold and container.** CMake project, license, README skeleton, copied
   bit reader/plane tools, container parsing, frame index, `bink2_dump`.
   Accept: open both samples, report metadata and audio descriptors, dump raw
   packets.
2. **KB2f video.** Port the old path, DC/MV prediction, motion compensation,
   audio chunk skipping. Accept: frames 0–235 of `sc13` decode without warnings
   and match the oracle; the truncation policy is asserted; the decoder stops
   cleanly at frame 236.
3. **KB2g+ video.** Port the new path (`bink2g.c`), slices, row/column CBP
   flags, alpha, RGB conversion, motion reporting. Accept: all 490 frames of
   `ge_video` match the oracle; a `KB2j` file produced by `bink2enc` decodes
   correctly. Alpha and h–n decode paths are smoke/unit-tested only until a
   sample is available.
4. **Bink Audio 2.** Port DCT and RDFT, 7-bit quantiser indices, PCM output,
   mixing. Accept: track PCM checksums match the oracle; the SDL player runs
   both samples.
5. **Intra encoder and build tool.** Bit writer, forward DCT, CBP/DC/AC
   writing, intra macroblocks, keyframes, slices, container writer,
   `bink2_build`. Accept: produced `KB2j` files decode in `libbink2` and in the
   FFmpeg oracle; reconstruction is byte-exact.
6. **Inter encoder.** Motion search, 1/4 vectors, skip/motion/residual modes,
   rate control, scene cuts. Accept: quality and bitrate within a documented
   margin of `bink2enc` on a short clip at the same settings; round-trip
   byte-exact; no oracle warnings.
7. **Release polish.** Docs (`docs/usage.md`, `docs/encoder.md`,
   `docs/bink2.md`), malformed-input suite, ASan/UBSan clean run, install
   rules, `0.1.0`. Optional follow-ons: SDL renderer zoo, encoder audio, alpha,
   slice threading.

## What is left to the builder

- Exact quantiser candidate lists, dead-zone width, lambda weighting, scene-cut
  threshold, and motion-search heuristics. These are encoder tuning choices;
  the format and the API do not depend on them.
- Whether the RGB-to-YUV matrix for Bink 2 should be full-range BT.601 or the
  studio-range matrix `libbink` uses. Determine by decoding the sample with the
  oracle and checking the luma range; expose the choice as an option if the
  answer differs between files.
- The internal file/module split beyond the sketch above, and the exact test
  fixture layout.
- Player chrome and renderer choice for the first SDL player.

## Risks

- **Deblocking drift.** Documented; correctness is measured against the
  reference decoder, not RAD's.
- **KB2f path breadth.** The old path is the largest and least exercised; the
  single 499-frame sample is the only coverage. Mitigation: oracle checksums and
  malformed-input tests.
- **Audio flag ambiguity.** The flag byte layout differs between revisions on
  paper. Mitigation: derive channel/DCT selection from the flag byte as the
  reference does, and verify against the sample PCM.
- **Encoder compatibility.** A syntactically valid stream may still drift from
  what games expect (slice padding, keyframe flags, frame flags). Mitigation:
  use `bink2enc`'s files as a behavioral reference and validate with the FFmpeg
  oracle; add RAD decoder validation as an optional script.
- **Licensing hygiene.** No AGPL code enters the tree. Decoder ports keep their
  LGPL headers; encoder code is original.
