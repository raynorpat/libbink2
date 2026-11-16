# libbink2 Decoder and Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the standalone LGPL C99 `libbink2` decoder and its tools, covering `KB2f`–`KB2n` video, Bink Audio 2, container parsing, seeking, and reference-checked output.

**Architecture:** One C library with an opaque `bink2` handle. Container state lives in `bink2.c`; video state and dispatch in `bink2_video.c`; the `KB2f` and `KB2g+` bitstream paths stay in separate translation units ported from the pinned FFmpeg Bink 2 reference. Audio is a port of Bink Audio with the Bink 2 branch added. Tools link the same library.

**Tech Stack:** C99, CMake 3.16+, ctest, optional SDL3/SDL2, vendored `stb_image` only in the encoder plan.

**Spec:** `docs/superpowers/specs/2026-11-16-libbink2-design.md`

## Global Constraints

- C99, `C_EXTENSIONS OFF`, CMake 3.16 minimum.
- Public prefixes: `bink2_` functions, `BINK2_` macros, type `bink2`. Library output names `bink2`; CMake alias `Bink2::bink2`.
- License LGPL-2.1-or-later. Ported files keep their FFmpeg copyright headers and add the libbink2 copyright.
- Decoder reference is pinned to `mrtnmtth/FFmpeg-bink2` commit `f4904840e820bfd0fb8fbc22179b39dfdbd3a9e0`: port `libavcodec/bink2.c`, `bink2g.c`, `bink2f.c`, and `binkaudio.c`. Never copy NihAV or `bink2enc` source.
- No mandatory third-party dependency. SDL is optional and only for `bink2_sdl_player`.
- Limits: width/height 1..7680/4800, at most 1,000,000 frames, at most 7 audio tracks, 2 GB file ceiling via `fseek`/`long`.
- Floating-point sources compile with `-ffp-contract=off` on GCC/Clang and `/fp:precise` on MSVC; no fast-math.
- Tests that need media read `BINK2_TEST_MEDIA` and `return 77` when it is unset; the matching ctest property is `SKIP_RETURN_CODE 77`.
- Media: `ge_video_86l.bk2` (`KB2g`, 1024x576, 490 frames), `sc13_01_partial.bk2` (`KB2f`, 1280x736, complete frames 0–235, frame 236 partial, 237+ absent).
- Commit after every task. Run `ctest --test-dir build --output-on-failure` before each commit.

## Review Focus

1. **Truncated files.** A frame starting past EOF must end the stream; a frame starting inside the file but running past EOF must error, never read past the buffer; seek must clamp. Pinned by `test_frames` and `test_decode_f`.
2. **Dimensions not aligned to 32.** Width 720 or height 460 must allocate padded planes, read the column-CBP count for the 32-aligned width, and clamp motion compensation at the right and bottom edges. Pinned by `test_video` and the encoder's odd-size round trip.
3. **Alpha streams.** A `BINK2_FLAG_ALPHA` file must allocate and expose plane 3 and decode its luma-coded alpha without disturbing Y/U/V. No sample exists, so pinned by a synthetic unit test.
4. **Bink Audio 2 RDFT and 7-bit quantiser indices.** No sample exists, so pinned by `test_audio` unit cases around flag mapping and packet parsing.
5. **Malformed or overread input.** Bogus slice offsets, oversized audio chunks, CBP reads past the slice, and motion vectors outside the reference window must return `BINK2_ERROR` without crashing. Pinned by `test_malformed`.

---

### Task 1: Project scaffold and public API

**Files:**
- Create: `CMakeLists.txt`, `src/bink2.h`, `src/bink2_internal.h`, `src/bink2.c`, `tests/CMakeLists.txt`, `tests/test_common.h`, `tests/test_api.c`, `README.md`, `LICENSE`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: the complete public decoder header from the spec; `bink2_open_file`, `bink2_open_filepointer`, `bink2_open_memory`, `bink2_close`, `bink2_info_all`, `bink2_info_video`, `bink2_info_audio`, `bink2_enable_video`, `bink2_enable_audio`, `bink2_first`, `bink2_next`, `bink2_seek_keyframe`, `bink2_get_video`, `bink2_get_plane`, `bink2_get_stride`, `bink2_get_audio`, `bink2_get_audio_size`, `bink2_get_motion`.

- [ ] **Step 1: Write the failing API test**

Create `tests/test_api.c`:

```c
#include "bink2.h"
#include "test_common.h"

int main(void)
{
    unsigned long frames = 0;
    CHECK(bink2_open_file("/nonexistent.bk2", BINK2_MODE_DISK) == NULL);
    bink2_close(NULL);
    CHECK(bink2_info_all(NULL, NULL, &frames, NULL) != 0);
    CHECK(bink2_get_plane(NULL, BINK2_PLANE_Y) == NULL);
    return 0;
}
```

- [ ] **Step 2: Add the build so the test can fail to compile**

Create `CMakeLists.txt` with options `BINK2_BUILD_STATIC`, `BINK2_BUILD_SHARED`, `BINK2_BUILD_ENCODER` (default `OFF` until the encoder plan adds the target), `BINK2_BUILD_TOOLS`, `BINK2_BUILD_TESTS`, `BINK2_SDL_VERSION`; a `bink2_set_compile_settings` function that sets C99, `-Wall -Wextra`, and the FP flags; static/shared targets from `src/bink2.c`; install rules; and `add_subdirectory(tests)`. Create `tests/CMakeLists.txt` with `test_api` and `add_test(NAME api ...)`.

Run: `cmake -S . -B build && cmake --build build`

Expected: FAIL, `bink2.h` not found.

- [ ] **Step 3: Create the header and stubs**

Write `src/bink2.h` exactly as the decoder interface in the spec, using `unsigned char` returns for status. Add `BINK2_OK 0x00`, `BINK2_DONE 0x00`, `BINK2_MORE 0x01`, `BINK2_LAST 0x02`, `BINK2_ERROR -1`. `bink2_open_*` returns `NULL`; the rest return `BINK2_ERROR` or `NULL`. `README.md` states scope, license, reference attribution, build commands.

Copy `LICENSE` from `../libbink/LICENSE`. Copy `tests/test_common.h` from `../libbink/tests/test_common.h`, rename the guard to `LIBBINK2_TEST_COMMON_H`, and change `test_media_dir()` to read `BINK2_TEST_MEDIA`.

- [ ] **Step 4: Build and pass**

Run: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`

Expected: `api` passes.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src tests README.md LICENSE .gitignore
git commit -m "feat: add libbink2 scaffold and public decoder API"
```

---

### Task 2: LSB-first bit reader and writer

**Files:**
- Create: `src/bink2_bits.c`, `src/bink2_bits.h`, `tests/test_bits.c`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `typedef struct { const unsigned char *data; size_t size; size_t bitpos; } Bink2Bits;`
  - `void bink2_bits_init(Bink2Bits *, const unsigned char *, size_t);`
  - `int bink2_bits_read(Bink2Bits *, int count);` returns `-1` on overread, count 0..32.
  - `int bink2_bits_read1(Bink2Bits *);`
  - `int bink2_bits_read_unary(Bink2Bits *, int stop, int max);` counts bits until `stop`, capped at `max`; returns `-1` on overread.
  - `int bink2_bits_left(const Bink2Bits *);`
  - `void bink2_bits_skip(Bink2Bits *, int count);`
  - `void bink2_bits_align32(Bink2Bits *);`
  - `typedef struct { unsigned char *data; size_t capacity; size_t bitpos; } Bink2BitWriter;`
  - `void bink2_bw_init(Bink2BitWriter *);`, `void bink2_bw_free(Bink2BitWriter *);`, `void bink2_bw_reset(Bink2BitWriter *);`
  - `int bink2_bw_put(Bink2BitWriter *, unsigned value, int count);`, `int bink2_bw_align32(Bink2BitWriter *);`, `size_t bink2_bw_bitpos(const Bink2BitWriter *);`, `size_t bink2_bw_bytes(const Bink2BitWriter *);`, `const unsigned char *bink2_bw_data(const Bink2BitWriter *);`

- [ ] **Step 1: Write the failing reader/writer test**

Create `tests/test_bits.c` with the libbink bitreader cases plus:

```c
static int test_writer_roundtrip(void)
{
    Bink2BitWriter bw;
    Bink2Bits bits;

    bink2_bw_init(&bw);
    CHECK(bink2_bw_put(&bw, 0x2A, 6) == 0);
    CHECK(bink2_bw_put(&bw, 0x15, 5) == 0);
    CHECK(bink2_bw_align32(&bw) == 0);
    CHECK(bink2_bw_bytes(&bw) == 4);
    bink2_bits_init(&bits, bink2_bw_data(&bw), bink2_bw_bytes(&bw));
    CHECK(bink2_bits_read(&bits, 6) == 0x2A);
    CHECK(bink2_bits_read(&bits, 5) == 0x15);
    CHECK(bink2_bits_left(&bits) == 21);
    bink2_bw_free(&bw);
    return 0;
}

static int test_unary(void)
{
    static const unsigned char data[1] = {0x05};
    Bink2Bits bits;

    bink2_bits_init(&bits, data, sizeof(data));
    CHECK(bink2_bits_read_unary(&bits, 1, 8) == 2);
    CHECK(bink2_bits_read_unary(&bits, 1, 8) == 1);
    CHECK(bink2_bits_read_unary(&bits, 1, 8) == -1);
    return 0;
}
```

Register both in `main`, and add `test_bits` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R bits --output-on-failure`

Expected: FAIL to compile, `bink2_bits.h` not found.

- [ ] **Step 3: Implement**

Copy `../libbink/src/bink_bits.c` and rename the reader/writer identifiers to the `bink2_*` names above. Drop `BinkTree`, `bink_tree_*`, and `bink_huff_*`; Bink 2 uses fixed VLC tables. Add `bink2_bits_read_unary` implemented as a bit-at-a-time loop that returns the number of bits read before `stop` or after `max` attempts, and `-1` if the stream ends.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R bits --output-on-failure`

Expected: PASS.

```bash
git add src/bink2_bits.c src/bink2_bits.h tests/test_bits.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add LSB-first bit reader and writer"
```

---

### Task 3: Container header, audio descriptors, and frame index

**Files:**
- Create: `tests/test_container.c`
- Modify: `src/bink2.c`, `src/bink2_internal.h`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `bink2_open_memory`, `bink2_info_*`.
- Produces: `struct bink2_t` fields for version, dimensions, fps, flags, audio tracks, and the `frame_offsets`/`frame_sizes`/`frame_keyframes` arrays. Internal: `int bink2_internal_load_frame(bink2, unsigned long frame);` declared in `bink2_internal.h` (implemented in Task 4).

- [ ] **Step 1: Write the failing container test**

Create `tests/test_container.c` with a builder for a synthetic `KB2g` file:

```c
static size_t build_kb2(unsigned char *buf, const char *sig, unsigned tracks)
{
    size_t off = 0;
    memset(buf, 0, 512);
    memcpy(buf, sig, 4);
    w32(buf + 4, 200);       /* file size - 8, not validated yet */
    w32(buf + 8, 2);         /* frames */
    w32(buf + 12, 64);       /* largest frame */
    w32(buf + 16, 2);
    w32(buf + 20, 1280);
    w32(buf + 24, 736);
    w32(buf + 28, 30000);
    w32(buf + 32, 1001);
    w32(buf + 36, 0x10);
    w32(buf + 40, tracks);
    off = 44;
    if (tracks) {
        unsigned t;
        for (t = 0; t < tracks; t++) w32(buf + off + 4 * t, 69120);
        off += 4 * tracks;
        for (t = 0; t < tracks; t++) {
            buf[off++] = 0x44; buf[off++] = 0xAC; buf[off++] = 0x00;
            buf[off++] = 0x50;
        }
        for (t = 0; t < tracks; t++) { w32(buf + off, t + 1); off += 4; }
    }
    w32(buf + off, 0); off += 4;
    w32(buf + off, 0); off += 4;
    w32(buf + off, 0); off += 4;
    return off;
}
```

Assertions:

```c
b = bink2_open_memory(buf, size);
CHECK(b != NULL);
CHECK(bink2_info_all(b, NULL, &frames, &usf) == 0);
CHECK(frames == 2);
CHECK(bink2_info_video(b, &w, &h, &flags) == 0);
CHECK(w == 1280 && h == 736 && flags == 0x10);
CHECK(bink2_info_audio(b, &mask, channels, depths, rates) == 0);
```

Check `KB2f` with six tracks exposes mask `0x3F`, 44100 Hz, mono; `KB2j` with one track re-reads the audio block after the extra 4-byte field; `KB2i` likewise; `BIKf` and `KB2a` return `NULL`; a header shorter than 44 bytes returns `NULL`; `fps_den == 0` returns `NULL`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R container --output-on-failure`

Expected: FAIL, open returns `NULL`.

- [ ] **Step 3: Implement container parsing**

In `src/bink2_internal.h`, declare `struct bink2_t` and track/audio types with the fields from the spec. In `src/bink2.c`, implement the open paths over `FILE *` and memory, the 44-byte header, the `KB2i+` extra 32-bit field, grouped max-decoded sizes, per-track 24-bit rate + 8-bit flags, track IDs, and the `(frame_count + 1)` 32-bit index. Accept signatures `KB2f`..`KB2n`; reject `BIK*`, `KB2a`, and unknown. Validate dimensions, frame count, fps, and monotonic offsets. Fill `frame_offsets[n] = index[n] & ~1u`, `frame_keyframes[n] = index[n] & 1`, and `frame_sizes[n]` from the next offset or EOF; leave past-EOF entries for Task 4 to reject. Implement `bink2_info_all/video/audio` from the stored fields; channels/depth are 2/16 when the `0x20` flag is set and 1/16 otherwise.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R container --output-on-failure`

Expected: PASS.

```bash
git add src/bink2.c src/bink2_internal.h tests/test_container.c tests/CMakeLists.txt
git commit -m "feat: parse the Bink 2 container header and frame index"
```

---

### Task 4: Frame packets and truncation policy

**Files:**
- Modify: `src/bink2.c`, `src/bink2_internal.h`
- Create: `tests/test_frames.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `struct bink2_t` from Task 3.
- Produces: `int bink2_internal_load_frame(bink2 object, unsigned long frame);` returns `BINK2_OK`, `BINK2_LAST`, or `BINK2_ERROR`, and leaves `video_data`/`video_size` pointing into `frame_data`. Also fills each track's `payload`/`payload_size`.

- [ ] **Step 1: Write the failing packet test**

Build a synthetic three-frame file with two audio tracks. Assert:

```c
CHECK(bink2_internal_load_frame(b, 0) == BINK2_OK);
CHECK(b->video_size == 8);
CHECK(b->audio[0].payload_size == 4);
CHECK(b->audio[1].payload_size == 0);
CHECK(bink2_internal_load_frame(b, 2) == BINK2_OK);
CHECK(bink2_internal_load_frame(b, 3) == BINK2_LAST);
```

Then cut the last frame in half and assert `BINK2_ERROR`. Add an index entry whose offset is past EOF and assert `BINK2_LAST` for it and for every later frame. Add an audio chunk size larger than the remaining packet and assert `BINK2_ERROR`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R frames --output-on-failure`

Expected: FAIL, `bink2_internal_load_frame` undefined.

- [ ] **Step 3: Implement**

Implement offset/size lookup, allocation/reallocation of `frame_data`, reading from `FILE *` or memory, and audio chunk splitting. Return `BINK2_LAST` when `frame >= frame_count` or the start offset is at/after `file_size`; return `BINK2_ERROR` when the end offset exceeds `file_size` or an audio chunk overruns the packet. A zero-size audio chunk leaves `payload = NULL`, `payload_size = 0`.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R frames --output-on-failure`

Expected: PASS.

```bash
git add src/bink2.c src/bink2_internal.h tests/test_frames.c tests/CMakeLists.txt
git commit -m "feat: load frame packets and enforce the truncation policy"
```

---

### Task 5: Video geometry, planes, RGB, and motion storage

**Files:**
- Create: `src/bink2_video.c`, `src/bink2_video.h`, `src/bink2_dsp.c`, `src/bink2_dsp.h`, `tests/test_video.c`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: bit reader.
- Produces:
  - `typedef struct Bink2Video { ... } Bink2Video;` with `cur[4]`, `ref[4]`, `stride[4]`, `plane_w[4]`, `plane_h[4]`, `version`, `width`, `height`, `aligned_w`, `aligned_h`, `mb_w`, `mb_h`, `has_alpha`, `has_prev`, `rgb`, and `mv[2]`/`mv_count` arrays.
  - `int bink2_video_init(Bink2Video *, unsigned long w, unsigned long h, unsigned long flags, int version);`
  - `void bink2_video_uninit(Bink2Video *);`, `void bink2_video_reset(Bink2Video *);`
  - `const unsigned char *bink2_video_plane(Bink2Video *, int plane);`
  - `int bink2_video_stride(const Bink2Video *, int plane);`
  - `const unsigned char *bink2_video_rgb(Bink2Video *);`
  - `int bink2_video_motion(Bink2Video *, unsigned long *mb_w, unsigned long *mb_h, const int16_t **mv_x, const int16_t **mv_y, const unsigned char **mv_count);`
  - `int bink2_video_decode(Bink2Video *, const unsigned char *data, size_t size, int is_keyframe);` (frame header added in Task 7)
  - DSP: `void bink2_idct_old(float block[64]);`, `void bink2_idct_new(int32_t block[64]);`, `void bink2_put_block(uint8_t *dst, int stride, const int32_t block[64]);`, `void bink2_add_block(uint8_t *dst, int stride, const int32_t block[64]);`, `void bink2_mc_luma(uint8_t *dst, int dst_stride, const uint8_t *src, int src_stride, int w, int h, int mvx, int mvy);`, `void bink2_mc_chroma(uint8_t *dst, int dst_stride, const uint8_t *src, int src_stride, int w, int h, int mvx, int mvy);`

- [ ] **Step 1: Write the failing geometry test**

Assertions:

```c
Bink2Video v;
CHECK(bink2_video_init(&v, 720, 460, 0, 'g') == 0);
CHECK(v.aligned_w == 736 && v.aligned_h == 480);
CHECK(v.mb_w == 23 && v.mb_h == 15);
CHECK(v.stride[0] == 736);
CHECK(v.stride[1] == 368);
CHECK(v.plane_w[0] == 720 && v.plane_h[0] == 460);
CHECK(v.plane_w[1] == 360 && v.plane_h[1] == 230);
CHECK(bink2_video_motion(&v, &mw, &mh, &mx, &my, &mc) == 1);
CHECK(mw == 23 && mh == 15);
CHECK(mc[0] == 0);
bink2_video_uninit(&v);
```

Add a known-color RGB check: set Y=128, U=128, V=128 and assert RGB is (128,128,128); set Y=16/235, U=16/240, V=16/240 and assert the studio-range conversion; repeat with the full-range flag for 0/255.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R video --output-on-failure`

Expected: FAIL to compile.

- [ ] **Step 3: Implement geometry, RGB, and motion storage**

Copy the plane layout and RGB conversion structure from `../libbink/src/bink_video.c`, changing the alignment to 32 for luma and 16 for chroma and the block grid to 32x32. Allocate four planes; allocate alpha only when `flags & BINK2_FLAG_ALPHA`. Allocate `mv[0]`, `mv[1]`, and `mv_count` for `mb_w * mb_h * 4` entries. Implement both studio and full-range conversion behind an internal `range` field; default to full range and revisit in Task 6. Stub `bink2_video_decode` to return `BINK2_ERROR`. Copy the IDCT and block put/add bodies from `../libbink/src/bink_dsp.c`; add `bink2_idct_new` and the MC functions from the reference `bink2g.c`/`bink2f.c` (port only, no tests yet).

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R video --output-on-failure`

Expected: PASS.

```bash
git add src/bink2_video.c src/bink2_video.h src/bink2_dsp.c src/bink2_dsp.h tests/test_video.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add Bink 2 video geometry, RGB conversion, and motion storage"
```

---

### Task 6: Pinned oracle build and golden checksums

**Files:**
- Create: `tests/capture_reference.sh`, `tests/checksum_frames.py`, `tests/make_clip.py`, `tests/reference_checksums.h`, `tests/README.md`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `BINK2_TEST_MEDIA`, `FFMPEG_BINK2` (optional path), `BINK2_ORACLE_DIR` (optional build directory).
- Produces: `tests/reference_checksums.h` with `typedef struct { unsigned long frame; uint64_t y, u, v, a; } Bink2FrameChecksum;` arrays `bink2_kb2f_checksums[]` (236 entries), `bink2_kb2g_checksums[]` (490 entries), and per-track audio lengths/checksums for the two samples.

- [ ] **Step 1: Write the capture script**

`capture_reference.sh` does, in order:

1. If `FFMPEG_BINK2` is unset, clone `https://github.com/mrtnmtth/FFmpeg-bink2.git` into `${BINK2_ORACLE_DIR:-build/oracle}`, check out `f4904840e820bfd0fb8fbc22179b39dfdbd3a9e0`, run `./configure --disable-doc --enable-gpl --disable-x86asm`, and `make -j`.
2. For `sc13_01_partial.bk2`, decode `-frames:v 236 -f rawvideo -pix_fmt yuv420p`.
3. For `ge_video_86l.bk2`, decode all 490 frames the same way.
4. For each sample, decode each audio stream with `-map 0:a:<n> -f s16le -acodec pcm_s16le`.
5. `checksum_frames.py` reads the raw planes plus width/height, computes per-plane FNV-1a over visible rows, and emits the header. `make_clip.py` writes a deterministic 64-frame 320x240 YUV 4:2:0 clip used by the synthetic `KB2j` fixture in Task 10.

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" tests/capture_reference.sh`

Expected: `tests/reference_checksums.h` contains exactly 236 and 490 entries, and six audio track entries for `sc13`.

- [ ] **Step 2: Determine the RGB range**

Decode one `ge_video` frame to raw YUV and inspect min/max luma with a short Python one-liner. Record the result in `tests/README.md`. If luma spans 0–255, keep full range; if it stays inside 16–235, switch the default in `bink2_video.c` to studio range and update the Task 5 RGB test.

- [ ] **Step 3: Commit the tooling and goldens**

```bash
git add tests/capture_reference.sh tests/checksum_frames.py tests/make_clip.py tests/reference_checksums.h tests/README.md
git commit -m "test: capture pinned Bink 2 reference checksums"
```

---

### Task 7: `KB2f` keyframe decode

**Files:**
- Create: `src/bink2_video_old.c`, `tests/test_decode_f.c`
- Modify: `src/bink2_video.c`, `src/bink2_dsp.c`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Bink2Video`, `Bink2Bits`, `bink2_idct_old`, `bink2_put_block`.
- Produces: `int bink2_video_decode_slice_old(Bink2Video *v, Bink2Bits *bits, int is_keyframe, int y0, int y1);` and a working `bink2_video_decode` for keyframes.

- [ ] **Step 1: Write the failing keyframe test**

```c
b = bink2_open_file(path, BINK2_MODE_DISK);
CHECK(b != NULL);
CHECK(bink2_enable_video(b, 1) == 0);
int st = bink2_first(b);
CHECK(st == BINK2_MORE || st == BINK2_LAST);
CHECK(bink2_get_plane(b, BINK2_PLANE_Y) != NULL);
CHECK(fnv1a_rows(bink2_get_plane(b, 0), w, h, bink2_get_stride(b, 0)) ==
      bink2_kb2f_checksums[0].y);
```

Repeat for U and V. Skip with `return 77` when `BINK2_TEST_MEDIA` is unset or the file is missing.

- [ ] **Step 2: Run to verify failure**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R decode_f --output-on-failure`

Expected: FAIL, color planes do not match.

- [ ] **Step 3: Port the `KB2f` intra path**

In `bink2_video_decode`, read the 32-bit frame flags and the slice offsets, compute slice heights for `version <= 'f'` (2 slices, first `(height / 2 + 16) & ~31`), skip the offset words, and call the old slice decoder for each slice. In `bink2_video_old.c`, port `bink2f_decode_slice` intra handling: block types, old CBP luma/chroma, per-component quantiser delta, old DC coding, old AC value/skip codebooks and scan, `bink2_idct_old`, and `put` for intra blocks. Use the reference's DC predictor state per macroblock row. For this task only intra blocks are required; inter block types return `BINK2_ERROR` if encountered.

- [ ] **Step 4: Pass and commit**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R decode_f --output-on-failure`

Expected: PASS.

```bash
git add src/bink2_video_old.c src/bink2_video.c src/bink2_dsp.c tests/test_decode_f.c tests/CMakeLists.txt
git commit -m "feat: decode KB2f keyframes"
```

---

### Task 8: `KB2f` inter frames, motion, and truncation

**Files:**
- Modify: `src/bink2_video_old.c`, `src/bink2_video.c`
- Modify: `tests/test_decode_f.c`

**Interfaces:**
- Consumes: Task 7.
- Produces: full `KB2f` decoding and the `bink2_video_motion` output for decoded frames.

- [ ] **Step 1: Extend the test**

Loop frames 0–235, comparing all three plane checksums. Assert frame 236 returns `BINK2_ERROR`, frame 237 returns `BINK2_LAST`, and `bink2_seek_keyframe(b, 200)` lands on a frame whose container entry has bit 0 set. After decoding an inter frame, assert `bink2_get_motion` returns valid dimensions and every `mv_count` is 0, 1, or 4; if the frame has any motion, at least one vector is nonzero.

- [ ] **Step 2: Run to verify failure**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R decode_f --output-on-failure`

Expected: FAIL at frame 1.

- [ ] **Step 3: Port inter decoding**

Port `bink2f_decode_slice` inter handling: LRU block types, old MV coding, MV prediction, `bink2_mc_luma`/`bink2_mc_chroma`, skip copy, and old residual parsing. Store each macroblock's vectors (1 or 4) into `mv[]` and set `mv_count`. On a frame error, publish nothing and leave the previous frame intact. Implement `bink2_first`/`bink2_next`/`bink2_seek_keyframe` over `bink2_internal_load_frame` and the keyframe array, including the truncation return codes.

- [ ] **Step 4: Pass and commit**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R decode_f --output-on-failure`

Expected: PASS for all 236 frames.

```bash
git add src/bink2_video_old.c src/bink2_video.c tests/test_decode_f.c
git commit -m "feat: decode KB2f inter frames and seek"
```

---

### Task 9: `KB2g+` keyframe and slice setup

**Files:**
- Create: `src/bink2_video_new.c`, `tests/test_decode_g.c`
- Modify: `src/bink2_video.c`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 5 DSP and state.
- Produces: `int bink2_video_decode_slice_new(Bink2Video *v, Bink2Bits *bits, int is_keyframe, int y0, int y1);`, slice-height selection for `KB2g`/`KB2h+`, row/column CBP flag reading, and new-path intra decode.

- [ ] **Step 1: Write the failing keyframe test**

Assert `ge_video` frame 0 plane checksums match `bink2_kb2g_checksums[0]`. Add a unit case for slice heights: `version 'g'`, height 96 gives 1 slice; height 576 gives 2; `version 'h'` with container flags `& 3` 0/1/2/3 gives 2/3/4/8 slices with the reference's `start`/`end` computation.

- [ ] **Step 2: Run to verify failure**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R decode_g --output-on-failure`

Expected: FAIL.

- [ ] **Step 3: Port the new intra path**

Port the frame header addition (`bink2g` slice table, `skip_bits(32 + 32 * (num_slices - 1))`), the `0x10000` row/column CBP flag arrays, and `bink2g_decode_slice` intra handling: new CBP luma/chroma, quantiser prediction and delta, unary DC coding, AC skip codes, integer IDCT. Handle alpha luma blocks when `has_alpha`. Dispatch from `bink2_video_decode` by version.

- [ ] **Step 4: Pass and commit**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R decode_g --output-on-failure`

Expected: PASS.

```bash
git add src/bink2_video_new.c src/bink2_video.c tests/test_decode_g.c tests/CMakeLists.txt
git commit -m "feat: decode KB2g+ keyframes and slice layout"
```

---

### Task 10: `KB2g+` inter frames, alpha, and full `ge_video`

**Files:**
- Modify: `src/bink2_video_new.c`
- Modify: `tests/test_decode_g.c`

**Interfaces:**
- Consumes: Task 9.
- Produces: full `KB2g+` decode, alpha plane, and per-macroblock motion output.

- [ ] **Step 1: Extend the test**

Decode all 490 frames and compare plane checksums. Add a synthetic alpha test: initialize a `Bink2Video` with `BINK2_FLAG_ALPHA`, set distinct Y and A patterns, convert to RGB, and assert alpha is allocated and the RGB path ignores it. Add an odd-size test: encode-free synthetic slice-height checks for 720x460 and assert no out-of-bounds under ASan.

- [ ] **Step 2: Run to verify failure**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R decode_g --output-on-failure`

Expected: FAIL at frame 1.

- [ ] **Step 3: Port inter and alpha**

Port `bink2g_decode_slice` inter handling: LRU block types with the new unary code, new MV coding (one or four vectors, escape codes), MV prediction, motion compensation, residual decode, and DC prediction for inter blocks. Add the alpha plane path using the same luma code path. Fill `mv[]`/`mv_count` for each macroblock and zero the frame's motion when decode fails.

- [ ] **Step 4: Add the generated `KB2j` fixture**

In Task 6's script, build `bink2enc` from `https://github.com/SBerda/bink2enc` at commit `dc40df99613a5df543b4fd19ca0214912cb53646`, generate a 64-frame 320x240 raw YUV clip with `tests/make_clip.py`, and produce `tests/media/synthetic_kb2j.bk2`. Decode it with the oracle, store checksums in `reference_checksums.h`, and add a test case that decodes it with `libbink2`. Do not copy any `bink2enc` source into the tree.

- [ ] **Step 5: Pass and commit**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R decode_g --output-on-failure`

Expected: PASS for all 490 frames and the synthetic `KB2j` file.

```bash
git add src/bink2_video_new.c tests/test_decode_g.c tests/reference_checksums.h tests/capture_reference.sh tests/checksum_frames.py
git commit -m "feat: decode KB2g+ inter frames and alpha"
```

---

### Task 11: Bink Audio 2

**Files:**
- Create: `src/bink2_audio.c`, `src/bink2_audio.h`, `tests/test_audio.c`
- Modify: `src/bink2.c`, `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Bink2AudioTrack` and the frame packet payloads from Task 4.
- Produces:
  - `typedef struct Bink2Audio Bink2Audio;`
  - `Bink2Audio *bink2_audio_open(unsigned long rate, int channels, int use_dct, int version_2);`
  - `void bink2_audio_close(Bink2Audio *);`
  - `int bink2_audio_decode(Bink2Audio *, const unsigned char *chunk, size_t size, short *out, size_t capacity_samples, size_t *out_samples);`
  - Public `bink2_get_audio`/`bink2_get_audio_size`.

- [ ] **Step 1: Write the failing audio test**

For both samples, enable every audio track, decode the first 60 frames, and assert the per-track PCM byte counts match the reference lengths and FNV checksums allow at most 1 LSB difference per sample. Add unit cases: the flag byte selects DCT for `0x10`, stereo for `0x20`; a zero-size chunk produces zero samples; a chunk shorter than 4 bytes returns `BINK2_ERROR`.

- [ ] **Step 2: Run to verify failure**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R audio --output-on-failure`

Expected: FAIL to compile.

- [ ] **Step 3: Implement**

Copy `../libbink/src/bink_audio.c` and rename to `bink2_audio_*`. Keep the DCT/RDFT transforms and overlap handling. Add the `version_2` branches from the pinned `binkaudio.c`: 7-bit quantiser indices, direct coefficient reads, and the Bink Audio 2 packet-reset behavior. Decode the flag byte as `0x10` DCT, `0x20` stereo, `0x40` 16-bit. Convert float output to clamped s16. Wire each track's `payload` into `bink2_audio_decode` from `bink2_first/next`.

- [ ] **Step 4: Pass and commit**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R audio --output-on-failure`

Expected: PASS within the 1-LSB tolerance.

```bash
git add src/bink2_audio.c src/bink2_audio.h src/bink2.c tests/test_audio.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: decode Bink Audio 2"
```

---

### Task 12: `bink2_dump`, malformed-input suite, and sanitizers

**Files:**
- Create: `src/tools/bink2_dump.c`, `tests/test_malformed.c`, `tests/run_dump.cmake`, `tests/check_dump.cmake`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/usage.md`

**Interfaces:**
- Consumes: the complete decoder.
- Produces: `bink2_dump [--frames N] video.bk2 [outdir]` and a malformed-input test suite.

- [ ] **Step 1: Write the failing dump and malformed tests**

`test_malformed` builds synthetic files with: a slice offset past EOF, a slice offset pointing backwards, a CBP that reads past the slice, a motion vector outside the reference window, an audio chunk size larger than the packet, and a header claiming 7681 pixels wide. Every case must return `NULL` or `BINK2_ERROR`, not crash.

`run_dump.cmake` runs `bink2_dump --frames 1` on `ge_video_86l.bk2` and checks that one PPM exists and starts with `P6`.

- [ ] **Step 2: Run to verify failure**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R 'malformed|dump' --output-on-failure`

Expected: FAIL, tool and test are missing.

- [ ] **Step 3: Implement**

Port `bink2_dump` from `../libbink/src/tools/bink_dump.c`, switching to `bink2_*`. It writes P6 files and per-frame checksums, and stops cleanly at a truncated frame. Add the malformed checks at each assumption: `bink2_video_decode` rejects slice offsets that are not strictly increasing or exceed the packet; the slice decoders check `bink2_bits_left` before every CBP, DC, and AC run and return `BINK2_ERROR`; MV values are validated against the reference window before use; `bink2_internal_load_frame` rejects an audio chunk larger than the remaining packet. Every bit read already returns `-1` on overread, so propagate that as `BINK2_ERROR`.

- [ ] **Step 4: Run ASan and pass**

Run:

```bash
cmake -S . -B build-asan -DBINK2_BUILD_TOOLS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build-asan --output-on-failure
```

Expected: PASS, no sanitizer report.

- [ ] **Step 5: Commit**

```bash
git add src/tools tests docs/usage.md CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add bink2_dump and the malformed-input suite"
```

---

### Task 13: SDL player and format documentation

**Files:**
- Create: `src/tools/bink2_sdl_player.c`, `src/tools/bink2_mix.c`, `src/tools/bink2_mix.h`, `tests/run_sdl.cmake`, `docs/bink2.md`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`, `README.md`

**Interfaces:**
- Consumes: `bink2_get_video`, `bink2_get_audio`.
- Produces: `bink2_sdl_player video.bk2`, SDL3 preferred with SDL2 fallback, RGB texture upload, audio queue, Esc/Q quit, Space pause, Left/Right seek.

- [ ] **Step 1: Write the failing player test**

`run_sdl.cmake` sets `BINK2_PLAYER_TEST_FRAMES=3` and `SDL_VIDEODRIVER=dummy`, runs the player on `ge_video`, and expects exit code 0. Skip with 77 when SDL or media is unavailable.

- [ ] **Step 2: Run to verify failure**

Run: `cmake -S . -B build -DBINK2_BUILD_TOOLS=ON && cmake --build build --target bink2_sdl_player`

Expected: FAIL, target does not exist.

- [ ] **Step 3: Implement**

Copy the audio mixing logic from `../libbink/src/tools/bink_mix.c` and rename it. Write a minimal player: `bink2_get_video` gives RGB24; upload one SDL texture per frame; queue `bink2_get_audio` for every enabled track; handle the three keys. Add the CMake SDL detection from `libbink`, gated on found SDL and not cross-compiling without a package path.

Write `docs/bink2.md` with the container layout, the frame/slice layout, the two bitstream generations, the audio descriptor, the known deblocking divergence, and the reference attribution.

- [ ] **Step 4: Pass and commit**

Run: `BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build -R sdl --output-on-failure`

Expected: PASS.

```bash
git add src/tools tests/run_sdl.cmake docs/bink2.md README.md CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add the SDL player and format documentation"
```
