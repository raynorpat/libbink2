# Decoder Slice Threading and SIMD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decode Bink 2 frames faster by running a frame's slices on threads
and vectorising the hot DSP loops, with output bit-identical to today.

**Architecture:** Per-slice prediction state moves off `Bink2Video` into
`Bink2Slice`; a lazily created internal thread pool (`bink2_thread.c`) runs
one slice per job after the main thread validates all slice offsets. SIMD
versions of the DSP routines live in `bink2_dsp_neon.c` / `bink2_dsp_sse2.c`,
selected at compile time, with the scalar `_c` versions kept as reference.

**Tech Stack:** C99, CMake, POSIX threads / Win32 threads, NEON and SSE2
intrinsics.

**Spec:** `docs/decoder-performance-design.md`

## Global Constraints

- Every golden checksum in `tests/reference_checksums.h` and every encoder
  `recon` comparison passes unchanged. Never edit the checksums file or
  loosen a test.
- No change to `src/bink2.h` or `src/encoder/bink2_encoder.h`.
- C99, `C_EXTENSIONS OFF`, `-Wall -Wextra` warning-free; intrinsics only via
  `arm_neon.h` / `emmintrin.h`.
- No dependency beyond libc, libm and the platform thread library.
- The encoder reconstructs only through the shared decoder functions.
- `KB2f` float IDCT (`bink2_idct_old`) stays scalar.
- Malformed input fails threaded exactly where it fails serial.
- Work on branch `feat/decoder-threads-simd` from `main`. Commit subjects
  are Conventional Commits (`feat:`, `fix:`, `test:`, `docs:`, `chore:`).
- Update docs in the same commit as the behaviour (AGENTS.md rule 8).
- Every task ends with: `cmake --build build && ctest --test-dir build
  --output-on-failure`, plus the same in `build-asan`, with
  `BINK2_TEST_MEDIA=~/Downloads` and `BINK2_ORACLE` set (see memory note) so
  `decode_f`, `decode_g`, `audio` run. Report any skipped test by name.

## Review Focus

1. **Decoding continues after a failed frame.** A corrupted frame N must
   leave frame N-1 visible, and the next keyframe must decode to its golden
   hash, with pointer swapping and threads both active. Test in Task 3 and
   Task 6.
2. **Seek/rewind with a live pool.** `bink2_first` / `bink2_seek_keyframe`
   after threaded frames must give the same hashes as a fresh handle. Test
   in Task 6.
3. **Handles closed without decoding, or many handles at once.** Opening and
   closing without a frame starts no threads; four handles decoding in
   parallel on four application threads all match serial. Test in Task 6.
4. **Empty or one-row slices.** A small `KB2h`+ file with 8 slices
   (height 64) has slices with `y0 == y1`; the empty jobs must be harmless.
   Test in Task 6.
5. **Odd frame sizes in threaded/SIMD RGB.** Width and height not multiples
   of 2, 16 or 32 (333x251) must match scalar RGB byte for byte. Test in
   Task 7 and Task 11.

---

## File map

| File | Change |
| --- | --- |
| `src/tools/bink2_bench.c` | New: benchmark tool |
| `src/bink2_video.h`, `src/bink2_video.c` | Slice context, packet buffer, pointer swap, threaded `decode_impl`, threaded RGB |
| `src/bink2_video_old.c`, `src/bink2_video_new.c` | Take `Bink2Slice *` instead of the shared fields |
| `src/encoder/bink2_enc_video.c`, `src/encoder/bink2_enc_motion.c`, `src/encoder/bink2_encoder.c` | Use `state.slice[0]` |
| `src/bink2_thread.h`, `src/bink2_thread.c` | New: pool, CPU count, env limit |
| `src/bink2_internal.h`, `src/bink2.c` | `thread_limit` on the handle, internal setter |
| `src/bink2_dsp.h`, `src/bink2_dsp.c` | `_c` scalar names, dispatch |
| `src/bink2_dsp_neon.c`, `src/bink2_dsp_sse2.c` | New: SIMD routines |
| `CMakeLists.txt` | New sources, `Threads::Threads`, `BINK2_SIMD`, `bink2_bench` |
| `tests/test_thread.c`, `tests/test_threads.c`, `tests/test_dsp_simd.c` | New tests |
| `tests/test_malformed.c`, `tests/test_video.c` | New cases |
| `docs/architecture.md`, `docs/api.md`, `docs/tools.md`, `README.md` | Docs |

---

### Task 1: Benchmark tool

**Files:**
- Create: `src/tools/bink2_bench.c`
- Modify: `CMakeLists.txt` (inside `if(BINK2_BUILD_TOOLS)`), `tests/CMakeLists.txt`, `docs/tools.md`

**Interfaces:**
- Produces: `bink2_bench [--iterations N] [--rgb] file.bk2`. Prints one line
  `frames=<n> seconds=<s.sss> fps=<f.f>` to stdout; exit 0 on success,
  1 on usage error, 2 on open/decode error.

- [ ] **Step 1: Add a smoke test** `bench_smoke` in `tests/CMakeLists.txt`
  (under the tools block, like `dump`) running
  `bink2_bench --iterations 2 --rgb tests/media/synthetic_kb2j.bk2` with
  `PASS_REGULAR_EXPRESSION "frames=[0-9]+ seconds=[0-9.]+ fps=[0-9.]+"`.
- [ ] **Step 2: Run it, expect FAIL** (target missing):
  `ctest --test-dir build -R '^bench_smoke$'`.
- [ ] **Step 3: Implement the tool.** Decode every frame with
  `bink2_first` / `bink2_next` `N` times (default 5); with `--rgb` also call
  `bink2_get_video` per frame. Time with a wall clock: `clock_gettime(
  CLOCK_MONOTONIC)` (define `_POSIX_C_SOURCE 199309L` at the top) or
  `QueryPerformanceCounter` under `_WIN32`. Never `clock()`: it measures CPU
  time and hides threading gains. Audio disabled. Document the tool in
  `docs/tools.md` with its flags, exit codes and a note that
  `BINK2_THREADS` (Task 6) caps threads.
- [ ] **Step 4: Run, expect PASS.**
- [ ] **Step 5: Record a baseline.** Generate bench clips (not committed)
  into the scratchpad: `python3 tests/make_clip.py -o clip.yuv --width 1920
  --height 1080 --frames 120`, then encode with `bink2_build` at
  `--slices 2`, `4`, `8` (raw YUV input per `docs/tools.md`). Run the bench
  on those and on both sample files; keep the numbers for the commit body.
- [ ] **Step 6: Commit** `feat: add bink2_bench decode benchmark` with the
  baseline table in the body.

### Task 2: Reuse the padded packet buffer

**Files:**
- Modify: `src/bink2_video.h`, `src/bink2_video.c:bink2_video_decode`,
  `bink2_video_uninit`

**Interfaces:**
- Produces: `Bink2Video` fields `unsigned char *packet; size_t
  packet_capacity;`.

- [ ] **Step 1: Add test** `test_packet_growth` in `tests/test_video.c`:
  decode a small frame, then a larger frame, then the small one again
  through `bink2_video_decode` on one `Bink2Video`, and assert each result
  matches a fresh `Bink2Video` decoding the same packet (plane hashes via
  `fnv1a_rows`). Build packets from `tests/media/synthetic_kb2j.bk2`.
- [ ] **Step 2: Run** `ctest -R '^video$'`: it passes already (behaviour
  guard); confirm it still passes after Step 3, and under ASan.
- [ ] **Step 3: Implement.** Grow `packet` to `size + 8` with `realloc`
  only when too small; zero the 8 pad bytes every call. Free in uninit.
- [ ] **Step 4: Run full suite + ASan.**
- [ ] **Step 5: Commit** `feat: reuse the padded packet buffer across frames`.

### Task 3: Swap cur and ref instead of copying

**Files:**
- Modify: `src/bink2_video.c:decode_impl` (end), `bink2_video_decode`
  (error path); `tests/test_malformed.c`

**Interfaces:**
- Produces: after a successful frame, `v->cur[i]` and `v->ref[i]` are
  swapped. `cur` holds stale data from two frames ago at the start of the
  next frame.

- [ ] **Step 1: Confirm the precondition.** Read both slice decoders and
  check every macroblock in `[0, aligned_w) x [0, aligned_h)` of every
  plane (alpha included) is fully written each frame (intra, skip via
  `bink2_video_copy_mb`, motion, residual), and that nothing reads `cur`
  pixels outside the macroblock being written. Write the finding in the
  commit body. If it does not hold, stop and report instead of continuing.
- [ ] **Step 2: Add test** `test_error_then_recover` in
  `tests/test_malformed.c`: on the 4-slice fixture, record the plane hashes
  after frame 0; corrupt frame 1's second slice offset (as
  `test_slice_offset` does) and assert `bink2_next` returns `BINK2_ERROR`
  and `bink2_get_plane` hashes equal frame 0's; then `bink2_first` again
  and assert frame 0's hashes are reproduced.
- [ ] **Step 3: Run it, expect PASS** (guard for the change).
- [ ] **Step 4: Implement** the swap. Keep the error-path restore
  (`memcpy` ref into cur), which only runs on failure.
- [ ] **Step 5: Run full suite + ASan; rerun the bench** on the 1080p clip.
- [ ] **Step 6: Commit** `feat: swap reference planes instead of copying`
  with before/after fps.

### Task 4: Per-slice context

**Files:**
- Modify: `src/bink2_video.h`, `src/bink2_video.c`, `src/bink2_video_old.c`,
  `src/bink2_video_new.c`, `src/bink2_video_new.h`,
  `src/encoder/bink2_enc_video.c`, `src/encoder/bink2_enc_motion.c`,
  `src/encoder/bink2_encoder.c`, `docs/architecture.md`

**Interfaces:**
- Produces (in `bink2_video.h`):

```c
#define BINK2_MAX_SLICES 8

typedef struct Bink2Slice {
    Bink2OldDc *dc_cur, *dc_prev;   /* mb_w + 1 entries */
    int32_t *mv_cur, *mv_prev;      /* (mb_w + 1) * 8 entries */
    Bink2Quant *q_cur, *q_prev;     /* mb_w + 1 entries */
    int mb_pos;
    int comp;
} Bink2Slice;
```

  `Bink2Video` loses `old_dc_*`, `old_mv_*`, `new_q_*`, `old_mb_pos`,
  `old_comp` and gains `Bink2Slice slice[BINK2_MAX_SLICES];`, all allocated
  in `bink2_video_init` and cleared in `bink2_video_reset`.
- Changed signatures (slice pointer after `v`):

```c
int bink2_video_decode_slice_old(Bink2Video *v, Bink2Slice *s,
                                 Bink2Bits *bits, int is_keyframe,
                                 int y0, int y1);
int bink2_video_decode_slice_new(Bink2Video *v, Bink2Slice *s,
                                 Bink2Bits *bits, int is_keyframe,
                                 int y0, int y1);
void bink2_video_predict_dc(Bink2Slice *s, int is_luma, float mindc,
                            float maxdc, int flags, float tdc[16]);
void bink2_video_mc_luma(Bink2Video *v, const Bink2Slice *s, int x, int y);
void bink2_video_mc_chroma(Bink2Video *v, const Bink2Slice *s, int x, int y);
void bink2_video_store_motion(Bink2Video *v, const Bink2Slice *s,
                              int x, int y, int nb);
```

  Any static helper in the slice files that read the old fields takes
  `Bink2Slice *` too. The per-frame `memset` of `old_mv_cur` in
  `decode_impl` moves into each slice decoder's start. The encoder uses
  `&state.slice[0]` everywhere it used the old fields.

- [ ] **Step 1: Refactor** (mechanical; the compiler finds every use).
  `decode_impl` passes `&v->slice[i]` for slice `i`.
- [ ] **Step 2: Run full suite + ASan.** Expected: all pass with no test
  change, including `enc_intra`, `enc_inter`, `enc_api` and the reference
  tests.
- [ ] **Step 3: Update** `docs/architecture.md` (slice context paragraph).
- [ ] **Step 4: Commit** `chore: move slice prediction state into Bink2Slice`.

### Task 5: Thread pool

**Files:**
- Create: `src/bink2_thread.h`, `src/bink2_thread.c`, `tests/test_thread.c`
- Modify: `CMakeLists.txt` (`BINK2_DECODER_SOURCES`;
  `find_package(Threads REQUIRED)`, link `Threads::Threads` PUBLIC to
  `bink2_static`, PRIVATE to `bink2_shared`), `tests/CMakeLists.txt`

**Interfaces:**
- Produces (`bink2_thread.h`):

```c
typedef struct Bink2Pool Bink2Pool;
typedef void (*Bink2Job)(void *arg, int index);

/* Starts `workers` threads (>= 1). NULL on any failure; nothing leaks. */
Bink2Pool *bink2_pool_create(int workers);
/* Runs fn(arg, i) for i in [0, count), on the workers and the caller;
 * returns after all calls finish. Not reentrant. */
void bink2_pool_run(Bink2Pool *pool, Bink2Job fn, void *arg, int count);
/* Joins and frees. NULL is a no-op. */
void bink2_pool_destroy(Bink2Pool *pool);
/* Online CPUs, >= 1. */
int bink2_cpu_count(void);
/* BINK2_THREADS as an integer in 1..64, or 0 when unset or invalid. */
int bink2_env_thread_limit(void);
```

  POSIX: `pthread_mutex_t` + two `pthread_cond_t` (work ready, work done)
  and a generation counter; jobs taken with a shared next-index under the
  mutex. Windows: `CRITICAL_SECTION` + `CONDITION_VARIABLE`. `sysconf(
  _SC_NPROCESSORS_ONLN)` / `GetSystemInfo`. Define `_POSIX_C_SOURCE
  200809L` in `bink2_thread.c` only.

- [ ] **Step 1: Write** `tests/test_thread.c` (`add_test(NAME thread ...)`):
  - `test_runs_every_index`: pool of 3, `count` 8, each job does
    `hits[index]++`; repeat 1000 runs; assert every `hits[i] == 1000`.
  - `test_count_below_workers`: pool of 7, `count` 2 → both hit once.
  - `test_count_zero`: returns without calling fn.
  - `test_create_destroy_loop`: 200 create/destroy cycles without a run.
  - `test_cpu_count`: `bink2_cpu_count() >= 1`.
- [ ] **Step 2: Run, expect FAIL** (link error).
- [ ] **Step 3: Implement** `bink2_thread.c`.
- [ ] **Step 4: Run, expect PASS**; also in a TSan build:
  `cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DBINK2_BUILD_TOOLS=ON
  -DCMAKE_C_FLAGS="-fsanitize=thread"` then `ctest --test-dir build-tsan -R
  '^thread$'` with no TSan report.
- [ ] **Step 5: Commit** `feat: add an internal slice thread pool`.

### Task 6: Threaded slice decoding

**Files:**
- Modify: `src/bink2_video.h`, `src/bink2_video.c:decode_impl`,
  `bink2_video_uninit`, `src/bink2_internal.h`, `src/bink2.c`
  (`ensure_video`), `tests/test_malformed.c`, `docs/api.md`,
  `docs/architecture.md`, `README.md`
- Create: `tests/test_threads.c` (in the `BINK2_BUILD_ENCODER` block of
  `tests/CMakeLists.txt`, linking both libraries)

**Interfaces:**
- Consumes: `Bink2Slice`, `bink2_pool_*`, `bink2_cpu_count`,
  `bink2_env_thread_limit`.
- Produces:
  - `Bink2Video` fields `Bink2Pool *pool; int pool_workers; int
    thread_limit;` (`thread_limit` 0 = auto).
  - `void bink2_video_set_threads(Bink2Video *v, int n);` (internal; 0 auto,
    1 serial, n>1 exact cap ignoring CPU count; destroys an existing pool
    when the value changes).
  - `struct bink2_t` field `int thread_limit;` and
    `void bink2_internal_set_threads(bink2 object, int n);` in
    `bink2_internal.h`, applied in `ensure_video` and to an existing video.
  - Effective threads = `min(num_slices, limit)` where `limit` is
    `thread_limit` if nonzero, else `bink2_env_thread_limit()` if nonzero,
    else `bink2_cpu_count()`. The pool (`threads - 1` workers) is created on
    the first frame that needs it; creation failure means serial.

`decode_impl` becomes:

```text
parse flags, skip CBP arrays            (unchanged)
start[0] = bits.bitpos
for i in 1..n-1:
    t = le32(offset i-1) * 8
    fail if t > real_size*8 or t < start[i-1]   (serial would fail too)
    start[i] = t
run slices (pool or serial loop), each with its own Bink2Bits over the
    same padded buffer at start[i], recording status[i] and end[i]
fail if any status[i] != 0
for i in 1..n-1: fail if align32(end[i-1]) > start[i]
fail if end[n-1] > real_size*8 + 64
swap planes, has_prev = 1
```

- [ ] **Step 1: Write** `tests/test_threads.c`. Encode with
  `bink2_enc_encode` (`read_frame_yuv` from a moving gradient like
  `tests/make_clip.py`, `keyframe_interval` 4, 12 frames) into files in the
  working directory, then decode each file on two handles, one with
  `bink2_internal_set_threads(h, 1)` and one with `8`, comparing Y/U/V
  `fnv1a_rows` hashes and `fnv1a` of `bink2_get_video` every frame:
  - `test_slices_match_serial`: 320x288 at slices 2, 3, 4, 8.
  - `test_empty_slices`: 96x64 at slices 8 (Review Focus 4).
  - `test_seek_after_threaded`: after the full threaded pass,
    `bink2_seek_keyframe(h, 8)` and `bink2_first(h)` hashes equal the
    serial handle's for those frames (Review Focus 2).
  - `test_open_close_no_decode`: 100 open/close cycles of an 8-slice file
    without decoding (Review Focus 3; leaks show under ASan).
  - `test_parallel_handles`: four application threads (use
    `bink2_pool_create(4)` + `bink2_pool_run` as the driver), each decoding
    the 8-slice file on its own handle, all hashes equal serial
    (Review Focus 3).
- [ ] **Step 2: Add to** `tests/test_malformed.c`, each run with
  `bink2_internal_set_threads(b, 1)` and `8`, asserting the same return
  code and same plane hashes in both modes:
  - `test_slice_overlap`: slice 2's offset pointing at slice 1's start + 4
    bytes (inside slice 1) → `BINK2_ERROR`.
  - `test_slice_offset_backwards`: slice 3's offset below slice 2's.
  - `test_threaded_fuzz`: 300 deterministic single-byte corruptions inside
    frame 0's slice data (LCG seed 1), result code and hashes equal across
    modes.
  - Rerun `test_error_then_recover` from Task 3 with threads 8 (Review
    Focus 1).
- [ ] **Step 3: Run both, expect FAIL** (setter undefined).
- [ ] **Step 4: Implement** the setter, pool lifecycle and the new
  `decode_impl`.
- [ ] **Step 5: Run full suite, ASan, and TSan** (`build-tsan`, all tests).
  Then run the full suite again with `BINK2_THREADS=1`.
- [ ] **Step 6: Docs.** `docs/api.md`: decoding uses up to one thread per
  slice, capped at the CPU count; `BINK2_THREADS=N` caps it, `1` is serial;
  a handle is still used by one application thread at a time.
  `docs/architecture.md`: threading model. `README.md` status list.
- [ ] **Step 7: Rerun the bench** at 2/4/8 slices; commit
  `feat: decode slices in parallel` with the numbers.

### Task 7: Threaded RGB conversion

**Files:**
- Modify: `src/bink2_video.c:bink2_video_rgb`, `src/bink2_dsp.h`,
  `src/bink2_dsp.c`, `tests/test_threads.c`

**Interfaces:**
- Produces: row converter shared with Task 11:

```c
void bink2_yuv_row_c(unsigned char *rgb, const unsigned char *y,
                     const unsigned char *u, const unsigned char *v,
                     int width, int studio_range);
void bink2_yuv_row(unsigned char *rgb, const unsigned char *y,
                   const unsigned char *u, const unsigned char *v,
                   int width, int studio_range);   /* dispatching */
```

  `bink2_video_rgb` splits `plane_h[0]` into `pool_workers + 1` bands on
  even row boundaries and runs them on `v->pool` when it exists, otherwise
  serially. It never creates the pool itself.

- [ ] **Step 1: Add** `test_rgb_odd_size` to `tests/test_threads.c`: 333x251,
  slices 4, RGB hash equal across threads 1 and 8 (Review Focus 5).
- [ ] **Step 2: Run, expect PASS** (guard), then implement.
- [ ] **Step 3: Run full suite, ASan, TSan; bench with `--rgb`.**
- [ ] **Step 4: Commit** `feat: convert RGB on the slice threads`.

### Task 8: SIMD scaffolding and integer IDCT

Before this task, profile the 1080p 4-slice clip (`xcrun xctrace record
--template 'Time Profiler'` or `perf record`) and order Tasks 8 to 11 by
their share of time. Skip a task whose routine is under 3% of decode time
and say so in the final report.

**Files:**
- Create: `src/bink2_dsp_neon.c`, `src/bink2_dsp_sse2.c`,
  `tests/test_dsp_simd.c`
- Modify: `src/bink2_dsp.h`, `src/bink2_dsp.c`, `CMakeLists.txt`,
  `tests/CMakeLists.txt`, `README.md` (build section)

**Interfaces:**
- Produces:
  - Scalar reference names in `bink2_dsp.h`: `bink2_idct_new_c`,
    `bink2_put_block_c`, `bink2_add_block_c` (today's bodies, renamed).
  - The existing names `bink2_idct_new`, `bink2_put_block`,
    `bink2_add_block` keep their signatures and call the SIMD version when
    `BINK2_SIMD_NEON` or `BINK2_SIMD_SSE2` is defined, else the `_c`.
  - `bink2_dsp.h` defines `BINK2_SIMD_NEON` when `__ARM_NEON` and
    `BINK2_SIMD_SSE2` when `__SSE2__` or `_M_X64`, unless
    `BINK2_NO_SIMD` is defined.
  - CMake `option(BINK2_SIMD "Use NEON/SSE2 DSP routines" ON)`; when OFF,
    `target_compile_definitions(... PRIVATE BINK2_NO_SIMD)` on both
    libraries. Both SIMD files are always in `BINK2_DECODER_SOURCES` and
    compile to a single unused `typedef` on other targets.

  The IDCT must keep the scalar's semantics: inputs truncated to `int16_t`,
  each 1-D pass computed in 32-bit, results truncated to `int16_t` between
  passes, arithmetic `>>` for the `IDCT_MUL_*` macros. Use 32-bit lanes for
  the intermediates.

- [ ] **Step 1: Write** `tests/test_dsp_simd.c` (`add_test(NAME dsp_simd)`):
  - `test_idct_new`: 20000 blocks from an LCG (seed 1): coefficients
    uniform in [-32768, 32767], in [-2048, 2047], sparse (DC only, one AC),
    and all `INT16_MIN`/`INT16_MAX`; assert `memcmp` of
    `bink2_idct_new` vs `bink2_idct_new_c` output == 0.
  - `test_put_add_block`: random blocks in [-600, 600] and random dst with
    stride 40; `put` and `add` outputs equal to `_c`.
  - When built with `BINK2_NO_SIMD` the test still runs (it compares the
    scalar with itself) and passes.
- [ ] **Step 2: Run, expect FAIL** (`_c` undefined).
- [ ] **Step 3: Implement** the rename, dispatch, NEON and SSE2 versions.
- [ ] **Step 4: Run full suite + ASan; also configure `build-scalar` with
  `-DBINK2_SIMD=OFF`** and run the full suite there.
- [ ] **Step 5: Bench, commit** `feat: vectorise the integer IDCT and block
  writes`.

### Task 9: SIMD motion compensation

**Files:**
- Modify: `src/bink2_dsp.h`, `src/bink2_dsp.c`, `src/bink2_dsp_neon.c`,
  `src/bink2_dsp_sse2.c`, `tests/test_dsp_simd.c`

**Interfaces:**
- Produces: `bink2_mc_luma_c`, `bink2_mc_chroma_c` (renamed scalar);
  `bink2_mc_luma` / `bink2_mc_chroma` dispatch. SIMD covers only the case
  where the source block plus filter taps lies inside the frame; the
  `edge_copy` path stays scalar.

- [ ] **Step 1: Add** `test_mc_luma` / `test_mc_chroma`: a 96x80 random
  frame; for every block position on a 8-pixel grid including all four
  borders and every `mvx, mvy` in [-70, 70], output equal to `_c`.
- [ ] **Step 2: Run, expect FAIL.** **Step 3: Implement.**
- [ ] **Step 4: Full suite, ASan, `build-scalar`; bench; commit**
  `feat: vectorise motion compensation`.

### Task 10: SIMD float block writes

**Files:**
- Modify: `src/bink2_dsp.h`, `src/bink2_dsp.c`, `src/bink2_dsp_neon.c`,
  `src/bink2_dsp_sse2.c`, `tests/test_dsp_simd.c`

**Interfaces:**
- Produces: `bink2_put_block_f_c`, `bink2_add_block_f_c`; dispatching
  `bink2_put_block_f`, `bink2_add_block_f`. Rounding must match `lrintf`
  in round-to-nearest-even: NEON `vcvtnq_s32_f32`, SSE2 `_mm_cvtps_epi32`.

- [ ] **Step 1: Add** `test_block_f`: random floats in [-600, 600] plus
  every `k + 0.5` for k in [-300, 300] (ties) and ±0.0; outputs equal `_c`.
- [ ] **Step 2–4:** fail, implement, full suite + ASan + `build-scalar`
  (`decode_f` must pass with media), bench, commit
  `feat: vectorise KB2f block writes`.

### Task 11: SIMD RGB conversion

**Files:**
- Modify: `src/bink2_dsp.c`, `src/bink2_dsp_neon.c`, `src/bink2_dsp_sse2.c`,
  `tests/test_dsp_simd.c`

**Interfaces:**
- Consumes: `bink2_yuv_row_c` / `bink2_yuv_row` from Task 7.

- [ ] **Step 1: Add** `test_yuv_row`: widths 1..70 and 333, random planes,
  both ranges; outputs equal `_c` (Review Focus 5).
- [ ] **Step 2–4:** fail, implement (SIMD body plus scalar tail), full suite
  + ASan + `build-scalar`, bench with `--rgb`, commit
  `feat: vectorise YUV to RGB conversion`.

### Task 12: Final verification and docs

**Files:**
- Modify: `docs/architecture.md` (SIMD files and dispatch), `README.md`,
  `AGENTS.md` (test count in the Commands section, new
  `BINK2_SIMD`/TSan commands)

- [ ] **Step 1: Run the matrix** with media and oracle set: `build`,
  `build-asan`, `build-tsan`, `build-scalar`, and `build` with
  `BINK2_THREADS=1`. All pass; skips named.
- [ ] **Step 2: Final bench table**: baseline vs each phase, per file, with
  and without `--rgb`.
- [ ] **Step 3: Update docs**, commit `docs: describe decoder threading and
  SIMD`.
