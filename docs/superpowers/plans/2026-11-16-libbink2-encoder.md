# libbink2 KB2j Encoder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an independent LGPL C99 Bink 2 video encoder to `libbink2` that writes `KB2j` files and round-trips byte-exactly through the library's own decoder.

**Architecture:** The encoder is a separate static library, `bink2enc`, that links the decoder and reuses its bit writer, VLC tables, quantiser matrices, and integer IDCT. It converts packed RGB or supplied YUV 4:2:0 to planes, searches motion, chooses a macroblock type by rate–distortion, writes the reference-compatible bitstream, and validates its own reconstruction against the decoder.

**Tech Stack:** C99, CMake 3.16+, ctest, vendored `stb_image` (public domain) for image input.

**Spec:** `docs/superpowers/specs/2026-11-16-libbink2-design.md`

**Prerequisite:** the decoder-and-tools plan (`docs/superpowers/plans/2026-11-16-libbink2-decoder.md`) is complete. In particular `Bink2BitWriter`, `bink2_idct_new`, the motion-compensation helpers, the VLC tables, and the frame/slice header parser already exist.

## Global Constraints

- C99, `C_EXTENSIONS OFF`, CMake 3.16 minimum.
- Public prefixes: `bink2_enc_` functions, `BINK2_ENC_` macros. Header `bink2_encoder.h`; archive `libbink2enc.a`; CMake target `bink2_encoder`, alias `Bink2::encoder`.
- License LGPL-2.1-or-later. Encoder code is original; only the decoder's LGPL tables and transforms are reused. Never copy `bink2enc` or NihAV source.
- Output is `KB2j` only: YUV 4:2:0, no alpha, no audio, 2/3/4/8 slices.
- Reuse `src/bink2_bits.h`, `src/bink2_tables.h`, and `src/bink2_dsp.h`; do not duplicate the bit writer or the transforms.
- Encoder options defaults: keyframe interval 150, motion range 8, slices 2, four-vector mode on, quality 80, target bitrate 0 (quality mode), verify off, recon path `NULL`.
- `bink2_enc_options.recon_path`, when set, writes tight raw YUV 4:2:0 frames (`w*h`, then `ceil(w/2)*ceil(h/2)` twice) for the internal reconstruction. Tests rely on it.
- Limits: width/height 1..7680/4800, frame count 1..1,000,000, fps numerator/denominator nonzero, slices one of 2/3/4/8, motion range 0..64.
- Floating-point sources compile with `-ffp-contract=off` on GCC/Clang and `/fp:precise` on MSVC; no fast-math.
- Tests that need media read `BINK2_TEST_MEDIA`; tests that need the oracle read `FFMPEG_BINK2` and skip with 77 when absent.
- Commit after every task. Run `ctest --test-dir build --output-on-failure` before each commit.

## Review Focus

1. **Odd dimensions (720x460).** Macroblock and slice padding must not write outside the planes, and the decoded output must match the encoder's reconstruction exactly. Pinned in Task 7.
2. **Four-vector macroblocks.** Vector prediction, the LRU block-type state, and chroma offsets derived from luma vectors must agree with the decoder. Pinned in Tasks 5 and 7.
3. **Keyframe synchronization.** An intra frame must have bit 0 set in the container index and the matching frame flags; a delta frame must have it clear. Pinned in Task 7.
4. **Rate-control extremes.** `target_bitrate` 0, very small targets, and `qmin == qmax` must terminate and produce a decodable stream. Pinned in Task 6.
5. **Predictor drift at slice edges.** The encoder must simulate DC and MV prediction exactly at the first row and column of each slice. Pinned by the multi-slice round trip in Task 7.

---

### Task 1: Encoder library, public API, and bit counter

**Files:**
- Create: `src/encoder/bink2_encoder.h`, `src/encoder/bink2_encoder.c`, `src/encoder/bink2_enc_bits.h`, `src/encoder/bink2_enc_bits.c`, `tests/test_enc_api.c`, `tests/test_enc_bits.c`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the decoder library, `src/bink2_bits.h`.
- Produces:
  - `typedef enum { BINK2_ENC_OK = 0, BINK2_ENC_ERR_USAGE = 1, BINK2_ENC_ERR_INPUT = 2, BINK2_ENC_ERR_ENCODE = 3, BINK2_ENC_ERR_VERIFY = 4 } bink2_enc_status;`
  - `typedef struct { int width, height; unsigned long frame_count, fps_num, fps_den; int (*read_frame)(void *, unsigned long, unsigned char *); int (*read_frame_yuv)(void *, unsigned long, unsigned char *, unsigned char *, unsigned char *); void *user; } bink2_enc_source;`
  - `typedef struct { unsigned keyframe_interval; unsigned motion_range; unsigned slices; int four_mv; int quality; unsigned target_bitrate; int verify; const char *recon_path; int verbose; } bink2_enc_options;`
  - `void bink2_enc_options_default(bink2_enc_options *);`
  - `bink2_enc_status bink2_enc_encode(const bink2_enc_source *, const bink2_enc_options *, const char *output_path, char *err, size_t errlen);`
  - `typedef struct { size_t bits; } Bink2BitCounter;`
  - `void bink2_bc_init(Bink2BitCounter *);`, `void bink2_bc_put(Bink2BitCounter *, unsigned value, int count);`, `size_t bink2_bc_bits(const Bink2BitCounter *);`

- [ ] **Step 1: Write the failing API and counter tests**

`test_enc_api.c` asserts defaults and validation:

```c
bink2_enc_options o;
bink2_enc_options_default(&o);
CHECK(o.keyframe_interval == 150);
CHECK(o.motion_range == 8);
CHECK(o.slices == 2);
CHECK(o.four_mv == 1);
CHECK(o.quality == 80);
CHECK(o.target_bitrate == 0);
CHECK(o.recon_path == NULL);

CHECK(bink2_enc_encode(NULL, &o, "x.bk2", err, sizeof(err)) == BINK2_ENC_ERR_USAGE);
CHECK(bink2_enc_encode(&src, &o, NULL, err, sizeof(err)) == BINK2_ENC_ERR_USAGE);
```

Set `src.width = 0` and each invalid field in turn; every case returns `BINK2_ENC_ERR_USAGE` and writes a non-empty `err`.

`test_enc_bits.c` asserts the counter: put 3 bits then 5 bits and check `bink2_bc_bits() == 8`; put 32 bits and check 40; init resets to 0.

- [ ] **Step 2: Run to verify failure**

Run: `cmake -S . -B build -DBINK2_BUILD_ENCODER=ON && cmake --build build && ctest --test-dir build -R enc --output-on-failure`

Expected: FAIL to compile.

- [ ] **Step 3: Implement the API and counter**

Add the `bink2_encoder` static target to CMake, including `src/encoder/bink2_encoder.c` and `src/encoder/bink2_enc_bits.c`, linking the decoder. Change the `BINK2_BUILD_ENCODER` option default to `ON`. Write the header and the default/validation logic. `bink2_enc_encode` returns `BINK2_ENC_ERR_ENCODE` with "encoder not implemented" for valid input at this stage. Implement the counter as a size accumulator; no bits are written.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R enc --output-on-failure`

Expected: PASS.

```bash
git add src/encoder tests/test_enc_api.c tests/test_enc_bits.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add the encoder API and bit counter"
```

---

### Task 2: Shared tables, forward DCT, and quantisation

**Files:**
- Create: `src/bink2_tables.c`, `src/bink2_tables.h`, `src/encoder/bink2_enc_dct.c`, `src/encoder/bink2_enc_dct.h`, `tests/test_enc_dct.c`
- Modify: `src/bink2_video_old.c`, `src/bink2_video_new.c`, `src/encoder/bink2_encoder.c`, `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: decoder tables currently static in `bink2_video_old.c` and `bink2_video_new.c`.
- Produces:
  - `extern const uint8_t bink2g_scan[64];`
  - `extern const uint16_t bink2g_luma_intra_qmat[4][64];`, `bink2g_chroma_intra_qmat[4][64];`, `bink2g_inter_qmat[4][64];`
  - `extern const uint16_t bink2g_ac_skip_codes[2][14];`, `bink2g_ac_skip_bits[2][14];`
  - `extern const uint16_t bink2f_ac_val_codes[2][13];`, `bink2f_ac_val_bits[2][13];`
  - `extern const uint16_t bink2f_ac_skip_codes[2][14];`, `bink2f_ac_skip_bits[2][14];`
  - `extern const uint8_t bink2g_mv_codes[];`, `bink2g_mv_bits[];`
  - `extern const int32_t bink2g_dc_pat[];`
  - `void bink2_fdct_block(const uint8_t *pixels, int stride, float out[64]);`
  - `int bink2_quantise(const float in[64], const uint16_t qmat[4][64], int q, int deadzone, int32_t levels[64]);` returns the number of nonzero levels.
  - `void bink2_dequantise(const int32_t levels[64], const uint16_t qmat[4][64], int q, int32_t out[64]);`

- [ ] **Step 1: Write the failing DCT test**

```c
static int test_constant_block(void)
{
    uint8_t px[64];
    float coeffs[64];
    int32_t levels[64] = {0}, out[64];
    uint8_t recon[64];
    int i;

    for (i = 0; i < 64; i++) px[i] = 128;
    bink2_fdct_block(px, 8, coeffs);
    for (i = 1; i < 64; i++) CHECK(fabsf(coeffs[i]) < 1e-3f);
    CHECK(bink2_quantise(coeffs, bink2g_luma_intra_qmat, 0, 0, levels) >= 0);
    bink2_dequantise(levels, bink2g_luma_intra_qmat, 0, out);
    bink2_idct_new(out);
    for (i = 0; i < 64; i++)
        CHECK(abs((int)out[i] - (int)px[i]) <= 1);
    return 0;
}
```

Add a random-block case using a fixed LCG seed; encode the block at q=0 and assert max absolute error ≤ 4 after `bink2_dequantise` + `bink2_idct_new`. Add a monotonicity check: the nonzero count at q=8 is no greater than at q=0.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R enc_dct --output-on-failure`

Expected: FAIL to compile.

- [ ] **Step 3: Move the tables and implement the transforms**

Move the static tables listed above out of the decoder files into `src/bink2_tables.c`, add `extern` declarations in `src/bink2_tables.h`, and include that header from the decoder files. Keep the values byte-for-byte identical to the pinned reference.

Implement `bink2_fdct_block` as the exact forward pair of `bink2_idct_new`: subtract 128 from each pixel, apply the integer IDCT's inverse transform, and scale so the test's round trip holds. Implement `bink2_quantise` with the reference dequantisation formula:

```c
scale = (int64_t)qmat[q & 3][pos] << (q >> 2);
level = (int)llround(coeff * 128.0 / scale);
if (abs(level) < deadzone) level = 0;
```

`bink2_dequantise` must match the decoder's inline arithmetic:

```c
out[pos] = (int32_t)(((int64_t)levels[pos] * (qmat[q & 3][pos] << (q >> 2)) + 0x40) >> 7);
```

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R enc_dct --output-on-failure`

Expected: PASS.

```bash
git add src/bink2_tables.c src/bink2_tables.h src/bink2_video_old.c src/bink2_video_new.c src/encoder/bink2_enc_dct.c src/encoder/bink2_enc_dct.h tests/test_enc_dct.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: share Bink 2 tables and add forward DCT/quantisation"
```

---

### Task 3: Bitstream writers with decoder round-trip tests

**Files:**
- Create: `src/bink2_video_new.h`, `src/encoder/bink2_enc_syntax.c`, `src/encoder/bink2_enc_syntax.h`, `tests/test_enc_syntax.c`
- Modify: `src/bink2_video_new.c`, `src/encoder/bink2_encoder.c`, `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Bink2BitWriter`, `Bink2Bits`, shared tables.
- Produces:
  - `uint8_t bink2_new_read_btype(Bink2Bits *, uint8_t lru[4]);`
  - `int bink2_new_read_quant(Bink2Bits *, int prev_q);`
  - `uint32_t bink2_new_read_cbp_luma(Bink2Bits *, uint32_t prev_cbp, uint32_t edge);`
  - `uint32_t bink2_new_read_cbp_chroma(Bink2Bits *, uint32_t prev_cbp);`
  - `void bink2_new_read_dcs(Bink2Bits *, int32_t dcs[16], int count, int q);`
  - `void bink2_new_read_acs(Bink2Bits *, int32_t levels[64], const uint16_t qmat[4][64], int q, uint32_t cbp);`
  - `int bink2_new_read_mv(Bink2Bits *, int mvs[4][2], int *nb);`
  - Writers:
    `bink2_write_btype`, `bink2_btype_bits`, `bink2_write_quant`, `bink2_write_cbp_luma`, `bink2_write_cbp_chroma`, `bink2_write_dcs`, `bink2_write_acs`, `bink2_write_mv1`, `bink2_write_mv4` with the signatures in the task body.

- [ ] **Step 1: Write the failing round-trip tests**

For each writer, write a value, rewind a `Bink2Bits` over the same buffer, and read it with the corresponding decoder function:

```c
uint8_t lru[4] = {2, 3, 1, 0};
uint8_t lru2[4];
Bink2BitWriter bw;
Bink2Bits bits;

bink2_bw_init(&bw);
bink2_write_btype(&bw, lru, 3);
memcpy(lru2, lru, sizeof(lru2)); /* writer updates lru; reader expects the pre-write copy */
memcpy(lru2, (uint8_t[4]){2, 3, 1, 0}, 4);
bink2_bits_init(&bits, bink2_bw_data(&bw), bink2_bw_bytes(&bw));
CHECK(bink2_new_read_btype(&bits, lru2) == 3);
```

Add cases:
- quantiser deltas `-8..8` written after a known previous value and read back.
- CBP luma patterns `0x0000`, `0xFFFF`, `0x00FF`, `0x1234`, `0x8001` with previous values `0xFFFF` and `0x00FF`.
- CBP chroma for all 4-bit patterns `0x0`–`0xF` with previous values `0x0` and `0xF`.
- 16 DC values covering zero, ±1, ±2, and the unary escape range.
- 64-coefficient AC arrays that exercise: all-zero, one coefficient, a run of 8 skipped coefficients, and a nonzero value after an escape.
- one-vector and four-vector motion values `(-64, 63)`, `(0, 0)`, `(255, -255)`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R enc_syntax --output-on-failure`

Expected: FAIL to compile.

- [ ] **Step 3: Extract the readers and implement the writers**

Move the inline readers in `bink2_video_new.c` into named functions with the signatures above; the slice decoder calls them. Preserve the exact bit semantics. Implement the writers as the inverse: block-type LRU unary, quantiser delta tables, CBP nibble rules, unary DC with sign, AC skip/level codes including the "next 8 coefficients" rule, and MV size/sign coding. The writers must not assume `Bink2BitWriter` beyond `bink2_bw_put`; use `bink2_bc_put` when computing a bit cost.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R enc_syntax --output-on-failure`

Expected: PASS.

```bash
git add src/bink2_video_new.h src/bink2_video_new.c src/encoder/bink2_enc_syntax.c src/encoder/bink2_enc_syntax.h tests/test_enc_syntax.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add bitstream writers verified against the decoder readers"
```

---

### Task 4: Intra frame encoder, container writer, and byte-exact round trip

**Files:**
- Create: `src/encoder/bink2_enc_internal.h`, `src/encoder/bink2_enc_video.c`, `src/encoder/bink2_enc_video.h`, `src/encoder/bink2_enc_container.c`, `src/encoder/bink2_enc_container.h`, `tests/test_enc_intra.c`
- Modify: `src/encoder/bink2_encoder.c`, `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1–3, `bink2_idct_new`, `bink2_put_block`.
- Produces:
  - `typedef struct { unsigned char *y, *u, *v; int stride[3]; int width, height; } Bink2Frame;`
  - `int bink2_enc_encode_intra(const bink2_enc_source *, const bink2_enc_options *, FILE *, short *quality);`
  - Container helpers: `int bink2_container_write_header(FILE *, unsigned long width, unsigned long height, unsigned long fps_num, unsigned long fps_den, unsigned long frame_count, unsigned long flags, unsigned long *index_offset);`, `int bink2_container_patch_index(FILE *, unsigned long index_offset, const unsigned long *offsets, const unsigned char *keys, unsigned long count);`

- [ ] **Step 1: Write the failing intra round-trip test**

Create a 320x240, 3-frame gradient with a `read_frame_yuv` callback. Encode with `options.recon_path = "recon.yuv"` and `keyframe_interval = 0`. Then:

```c
b = bink2_open_file("out.bk2", BINK2_MODE_DISK);
CHECK(b != NULL);
bink2_info_video(b, &w, &h, &flags);
CHECK(w == 320 && h == 240);
bink2_enable_video(b, 1);
for (f = 0; f < 3; f++) {
    int st = f == 0 ? bink2_first(b) : bink2_next(b);
    CHECK(st != BINK2_ERROR);
    /* compare each plane to the matching recon.yuv frame */
}
```

Compare visible rows with `memcmp` against the recon file. Assert all three container index entries have bit 0 set. Open the file with the decoder and assert `bink2_info_all` reports 3 frames.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R enc_intra --output-on-failure`

Expected: FAIL, encode returns `BINK2_ENC_ERR_ENCODE`.

- [ ] **Step 3: Implement the intra path and container**

Write the 48-byte `KB2j` header (the 44-byte fields plus the KB2i+ extra word `0x87e70800`). The container video-flags field carries the slice count for `KB2h+`: `0x10` for 2 slices, `0x11` for 3, `0x12` for 4, `0x13` for 8. Reserve the `(frame_count + 1)` index, write each frame packet aligned to 4 bytes, and patch the index with `offset | keyframe`. Frame payload: the fixed 32-bit `FRAME_FLAGS_INTRA = 0x76018`, one 32-bit offset per remaining slice, then the row-CBP block: a single `0` bit followed by `((height + 15) >> 4) * 2 - 1` one bits. Then one intra macroblock per 32x32 block: quantiser delta, CBP, DCs, ACs, IDCT, and put. Convert RGB with the same full-range matrix as the decoder; use `read_frame_yuv` directly when supplied. Write the reconstruction to `recon_path` as tight YUV420.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R enc_intra --output-on-failure`

Expected: PASS.

```bash
git add src/encoder/bink2_enc_internal.h src/encoder/bink2_enc_video.c src/encoder/bink2_enc_video.h src/encoder/bink2_enc_container.c src/encoder/bink2_enc_container.h src/encoder/bink2_encoder.c tests/test_enc_intra.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: encode KB2j intra frames and the container"
```

---

### Task 5: Motion search, skip, and motion-only macroblocks

**Files:**
- Create: `src/encoder/bink2_enc_motion.c`, `src/encoder/bink2_enc_motion.h`, `tests/test_enc_inter.c`
- Modify: `src/encoder/bink2_enc_video.c`, `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Bink2Frame`, `bink2_mc_luma`, `bink2_mc_chroma`.
- Produces:
  - `typedef struct { int x, y; } Bink2Mv;`
  - `void bink2_me_search(const Bink2Frame *src, const Bink2Frame *ref, int mbx, int mby, int range, int four_mv, Bink2Mv out[4], int *nb);`
  - `typedef struct { unsigned intra, skip, motion, residual; } Bink2EncStats;`
  - `const Bink2EncStats *bink2_enc_last_stats(void);`

- [ ] **Step 1: Write the failing inter test**

Encode four 320x240 frames: frame 0 is a gradient, frame 1 is identical, frame 2 is frame 0 shifted right by 8 pixels, frame 3 is noisy frame 2. Use `keyframe_interval = 0`. Decode and compare every frame to `recon.yuv` byte for byte. Assert `bink2_enc_last_stats()->skip > 0` after frame 1, and that frame 2 has at least one macroblock with a nonzero vector reported by `bink2_get_motion` (vector values in half-pel units).

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R enc_inter --output-on-failure`

Expected: FAIL.

- [ ] **Step 3: Implement motion and the simple modes**

Add the frame header for `FRAME_FLAGS_INTER = 0x6e018`, the block-type LRU, one-vector and four-vector MV coding, skip copies, and motion compensation. Implement a predictor- and temporal-guided diamond search over half-pel luma with the reference window clamp; derive chroma vectors by `>> 2`. Compare skip, motion, and intra by SAD plus a bit-cost estimate, and choose the smallest. Update the stats counters.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R enc_inter --output-on-failure`

Expected: PASS.

```bash
git add src/encoder/bink2_enc_motion.c src/encoder/bink2_enc_motion.h src/encoder/bink2_enc_video.c tests/test_enc_inter.c CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add motion search, skip, and motion-only macroblocks"
```

---

### Task 6: Residual coding, mode decision, and rate control

**Files:**
- Modify: `src/encoder/bink2_enc_video.c`, `src/encoder/bink2_encoder.c`
- Modify: `tests/test_enc_inter.c`
- Create: `tests/test_enc_rate.c`

**Interfaces:**
- Consumes: Tasks 2–5.
- Produces: residual macroblocks, quantiser selection, the quality/bitrate modes, and scene-cut keyframes.

- [ ] **Step 1: Write the failing tests**

`test_enc_inter` gains a noisy frame case: assert `stats.residual > 0` and that decoded PSNR against the source is higher than the motion-only decode of the same frame. `test_enc_rate` encodes 120 frames of a moving pattern at `quality` 40 and 90 and asserts the high-quality file is larger and has higher PSNR. It then encodes with `target_bitrate = 100` KiB/s and asserts the actual size is within ±25% of `100 * duration`. A scene-cut case inserts a black frame at index 60 with `keyframe_interval = 150` and asserts the container index has bit 0 set at frame 60. Extreme cases `target_bitrate = 1`, `keyframe_interval = 0`, and `quality = 0` must produce a decodable file.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R 'enc_inter|enc_rate' --output-on-failure`

Expected: FAIL.

- [ ] **Step 3: Implement residual coding and rate control**

Encode the residual of the motion-compensated prediction with CBP, DCs, quantised ACs, and add on decode. Extend the mode decision to compare skip/motion/residual/intra with a lambda weighted by the estimated bits from `Bink2BitCounter`. Choose the quantiser from the quality setting, then adjust it per frame with a simple feedback loop to hit `target_bitrate`; clamp to 1..36. Mark scene cuts intra when the frame's SAD against the previous frame exceeds a fixed threshold; make the container keyframe bit and the frame flags agree.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R 'enc_inter|enc_rate' --output-on-failure`

Expected: PASS.

```bash
git add src/encoder/bink2_enc_video.c src/encoder/bink2_encoder.c tests/test_enc_inter.c tests/test_enc_rate.c
git commit -m "feat: add residual coding, mode decision, and rate control"
```

---

### Task 7: Slices, four-vector mode, odd sizes, and keyframe flags

**Files:**
- Modify: `src/encoder/bink2_enc_video.c`, `src/encoder/bink2_enc_container.c`
- Create: `tests/test_enc_options.c`

**Interfaces:**
- Consumes: Tasks 4–6.
- Produces: full option coverage and the multi-slice writer.

- [ ] **Step 1: Write the failing option matrix test**

For each `slices` in `{2, 3, 4, 8}`, encode 20 frames of a 160x120 clip, decode every frame, and compare to `recon.yuv` byte for byte. Repeat with `four_mv = 0` and `four_mv = 1`. Encode 720x460 for 5 frames and verify decoded strides and exact reconstruction. Encode with `keyframe_interval = 10` and assert the index has bit 0 set on frames 0, 10, and 20 only. Encode with `motion_range = 0` and assert every macroblock reports `mv_count == 0`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build && ctest --test-dir build -R enc_options --output-on-failure`

Expected: FAIL on at least one slice count.

- [ ] **Step 3: Implement**

Compute slice boundaries exactly as the decoder does for `KB2h+`: `num_slices = 2/3/4/8`, `end = aligned_height + 32 * num_slices - 1`, and each boundary rounded down to 32. Write offsets for slices 1..n-1 in the frame header, align each slice to 4 bytes, and update the low two bits of the container video-flags field. Ensure the slice-edge predictors start fresh as the decoder expects. Respect `four_mv` and `motion_range` in the search and coding.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R enc_options --output-on-failure`

Expected: PASS.

```bash
git add src/encoder/bink2_enc_video.c src/encoder/bink2_enc_container.c tests/test_enc_options.c
git commit -m "feat: support slices, four-vector mode, and odd sizes"
```

---

### Task 8: `bink2_build` and image input

**Files:**
- Create: `src/tools/bink2_build.c`, `src/encoder/bink2_enc_image.c`, `src/encoder/bink2_enc_image.h`, `third_party/stb/stb_image.h`, `tests/test_enc_image.c`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`, `README.md`

**Interfaces:**
- Consumes: `bink2_enc_encode`.
- Produces: `bink2_build <images> [-o out.bk2] [options]`; `int bink2_enc_image_load(const char *path, unsigned char **rgb, unsigned *w, unsigned *h);`

- [ ] **Step 1: Write the failing loader and CLI tests**

`test_enc_image` writes tiny PPM, BMP, PNG, and JPEG fixtures (BMP/PNG/JPEG generated with fixed byte arrays or copied from `../libbink/tests`) and asserts each loads to the expected dimensions and a non-empty RGB buffer. `bink2_build_help` runs `bink2_build --help` and expects exit code 0. An end-to-end run builds three PPM frames with `--verify` and asserts exit code 0 and a readable output file.

- [ ] **Step 2: Run to verify failure**

Run: `cmake -S . -B build -DBINK2_BUILD_TOOLS=ON && cmake --build build && ctest --test-dir build -R 'enc_image|build_help|build_e2e' --output-on-failure`

Expected: FAIL.

- [ ] **Step 3: Implement**

Copy the image loader and natural-sort logic from `../libbink/src/encoder/bink_enc_image.c` and `bink_enc_stb.c`, renaming to `bink2_enc_image_*`. Copy `third_party/stb/stb_image.h`. Port `bink_build.c` to `bink2_build.c`, replacing the Bink 1 encoder calls with `bink2_enc_encode` and exposing `--slices`, `--quality`, `--bitrate`, `--four-mv`, `--recon`. Add the tool to CMake and install rules.

- [ ] **Step 4: Pass and commit**

Run: `cmake --build build && ctest --test-dir build -R 'enc_image|build_help|build_e2e' --output-on-failure`

Expected: PASS.

```bash
git add src/tools/bink2_build.c src/encoder/bink2_enc_image.c src/encoder/bink2_enc_image.h third_party/stb/stb_image.h tests/test_enc_image.c CMakeLists.txt tests/CMakeLists.txt README.md
git commit -m "feat: add bink2_build and image input"
```

---

### Task 9: Oracle validation, documentation, and release polish

**Files:**
- Create: `tools/validate.sh`, `docs/encoder.md`
- Modify: `README.md`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the complete encoder and decoder.
- Produces: an external validation script and encoder documentation.

- [ ] **Step 1: Write the validation script**

`tools/validate.sh` takes a generated `.bk2` and optionally `FFMPEG_BINK2` and `BINK2ENC_BIN`. It runs the oracle:

```bash
"$FFMPEG_BINK2" -v error -i "$1" -f null - 2> oracle.log
test ! -s oracle.log
```

and fails on any output. When `BINK2ENC_BIN` is set, it encodes the same raw YUV clip with both encoders, compares bitrate and PSNR via a short Python helper, and prints the ratio; it does not fail unless PSNR is more than 3 dB worse at the same bitrate.

- [ ] **Step 2: Run the validation**

Generate a 5-second 1280x720 clip and an encoded output, then run the script. Expected: oracle log empty, `bink2_build --verify` exit 0.

- [ ] **Step 3: Write docs and run sanitizers**

`docs/encoder.md` documents the `KB2j` subset, the options, the rate-control behavior, the reconstruction guarantee, and the known deblurring divergence. Run:

```bash
cmake -S . -B build-asan -DBINK2_BUILD_TOOLS=ON -DBINK2_BUILD_ENCODER=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build-asan --output-on-failure
```

Expected: PASS with no sanitizer report.

- [ ] **Step 4: Commit**

```bash
git add tools/validate.sh docs/encoder.md README.md tests/CMakeLists.txt
git commit -m "docs: document the encoder and add oracle validation"
```
