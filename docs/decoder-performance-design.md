# Decoder performance: slice threading and SIMD

Status: design, not yet implemented.

## Intent

Make the decoder faster for general use, without changing a single output
byte. RAD's own decoder gets its speed by decoding a frame's slices on
separate threads and by using SIMD in the inner loops. libbink2 can do the
same: the bitstream already carries independent slices.

Success means:

- the same golden checksums (`tests/reference_checksums.h`) and the same
  `recon` equality in the encoder tests, with no test tolerance changed;
- a measured speedup on a benchmark that ships with the change, reported per
  phase (cheap fixes, threads, SIMD) so each one's effect is visible;
- no change to `bink2.h` or `bink2_encoder.h`.

There is no numeric speed target. The possible gain from threading is
bounded by the file's slice count, which the encoder chose, not the decoder.

## Goals

- Decode the slices of a frame in parallel, automatically.
- Vectorise the hot DSP and conversion loops with NEON (arm64) and SSE2
  (x86-64), keeping the scalar code as fallback and test reference.
- Remove per-frame overhead that needs neither threads nor SIMD.
- Add a benchmark tool so the gains can be measured and checked again later.

## Non-goals

- Re-slicing a frame more finely than the file's slices. `KB2f` files always
  have 2 slices, `KB2g` 1 or 2, and `KB2h`+ 2, 3, 4 or 8.
- Frame-level threading (decoding frame N+1 while N is displayed).
- The in-loop deblocking filter, which neither the oracle nor libbink2 has.
- AVX2, AVX-512, SVE, or runtime CPU dispatch.
- Audio decoding speed.

## Anti-goals

These would make the work a failure even if it got faster:

- any decoded byte differing from the oracle, or the encoder's `recon`
  diverging from the decoder;
- the encoder getting its own copy of a DSP routine (AGENTS.md rule 2);
- a malformed file that is rejected today being accepted, or decoding
  differently, when threaded;
- a public API or ABI change;
- data races (the change must be clean under ThreadSanitizer as well as
  ASan/UBSan).

## Constraints

- C99 with `C_EXTENSIONS OFF`, `-Wall -Wextra` warning-free. SIMD goes
  through the standard intrinsic headers (`arm_neon.h`, `emmintrin.h`), not
  inline assembly or vector extensions.
- No new dependency beyond libc and libm. Threads use the platform library:
  POSIX threads on Unix-likes, Win32 threads on Windows. CMake links
  `Threads::Threads` where needed.
- The `KB2f` path uses a float IDCT. Changing float operation order changes
  the output, so the float IDCT stays scalar unless a SIMD version is proven
  bit-identical on every reference frame.
- The encoder links the decoder and reaches into the per-slice prediction
  fields directly (`bink2_enc_video.c`, `bink2_enc_motion.c`). Moving those
  fields affects the encoder.

## How it works today

`decode_impl()` in `src/bink2_video.c` reads the frame flags, skips the slice
offset table, then decodes slices one after another. Before each slice after
the first it seeks to that slice's stored offset. Each slice decoder resets
its prediction state at the slice start, writes only its own macroblock rows
of `cur`, and reads motion-compensation sources from `ref`, which is the
completed previous frame. After the last slice, `cur` is `memcpy`ed into
`ref`.

The per-slice prediction state lives on the shared `Bink2Video`:
`old_dc_cur/prev`, `new_q_cur/prev`, `old_mv_cur/prev`, `old_mb_pos`,
`old_comp`. That sharing is the only thing stopping the slices from running
in parallel.

## Approaches considered

**A. Opt-in `bink2_set_threads(obj, n)`.** The default stays
single-threaded; callers opt in. Precise control and easy to test both ways,
but it adds a public function.

**B. Always threaded, no API change. (Chosen.)** The decoder uses one worker
per slice, capped at the CPU count. No caller has to change anything. The
cost is that a host with its own job system cannot switch it off through the
API; the environment variable below exists for testing and as an escape
hatch, not as API.

**C. Expose per-slice decoding and let callers thread it.** The library stays
thread-free and job systems get full control, but it needs the most new API
and gives libbink-style callers nothing.

B was picked: it needs no API change and every caller benefits.

## Design

### Phase 0: benchmark

A new tool, `bink2_bench` (built with `BINK2_BUILD_TOOLS`), decodes a file N
times and reports frames per second for planes only and for planes + RGB.
It needs no extra dependency. It is run on the sample files and on
synthetic `KB2j` files from the encoder at 2, 4 and 8 slices and at
1080p-class sizes, so the threading gain shows on files that have the slices
to exploit. Results are recorded in the commit messages for each phase.

### Phase 1: per-frame overhead

- Swap the `cur` and `ref` plane pointers after a successful frame instead
  of `memcpy`ing every plane. This works only if every macroblock of `cur`
  is written each frame (skip blocks copy from `ref`); the builder confirms
  that, including padding rows and columns beyond the visible size, before
  relying on it. The error path still has to leave the previous picture
  visible.
- Keep the padded packet buffer on `Bink2Video` and grow it when needed,
  instead of `malloc` and `free` per frame.

### Phase 2: per-slice context

Move the per-slice prediction state into a slice context struct (for
example `Bink2SliceCtx`). `Bink2Video` owns one context per possible slice
(8). The slice decoders and the shared helpers (`bink2_video_predict_dc`, MV
prediction, quantiser prediction) take a context pointer instead of reading
those fields off `Bink2Video`. The encoder uses context 0 through the same
shared functions. No behaviour change; every existing test must pass
unchanged before Phase 3 starts.

Anything else a slice writes is checked to be row-disjoint: plane rows
(slice boundaries are 32-row aligned, so chroma rows are 16-aligned), and the
`mv`/`mv_count` arrays (indexed per macroblock).

### Phase 3: threaded slice decode

- **Offsets first.** The main thread reads the frame header and the
  optional row/column CBP arrays, then validates every slice offset up front
  with the same bounds the serial code uses, and builds one bit reader per
  slice over the shared, read-only packet.
- **Pool.** A small internal pool (new file, for example
  `src/bink2_thread.c`, a thin wrapper over pthreads / Win32) is created
  lazily on the first multi-slice frame and destroyed in
  `bink2_video_uninit`. Worker count is `min(slices, CPUs) - 1`; the calling
  thread decodes a slice too. Workers wait on a condition variable between
  frames; nothing is spawned per frame. Because the pool is lazy, the
  encoder's `Bink2Video`, which never calls `bink2_video_decode`, never
  starts threads.
- **Same error behaviour as serial.** The serial code fails a frame when a
  slice ends past the next slice's stored start (the forward seek would be
  negative) or when the last slice reads more than 64 bits past the packet.
  After joining, the main thread checks each slice's end bit position
  against the next slice's offset and the last against the packet end, and
  fails the frame on the same conditions. Any slice error fails the frame,
  and the existing restore-previous-picture path runs.
- **Fallback.** If the pool cannot be created, decoding falls back to serial;
  it is not an error. Single-slice frames always decode serially.
- **Override.** The `BINK2_THREADS` environment variable, read once when the
  pool is first needed, caps the worker count; `1` forces serial decoding.
  Documented in `docs/api.md` as a testing and troubleshooting knob.
- **RGB.** `bink2_video_rgb()` splits its rows across the same pool, since
  it is a full-frame pass that callers pay for on every frame.

### Phase 4: SIMD

Targets, in the order the Phase 0 profile ranks them; the expected
candidates are:

- `bink2_idct_new`, `bink2_put_block`, `bink2_add_block` (integer IDCT
  path);
- `bink2_mc_luma` (6-tap half-pel filter) and `bink2_mc_chroma`;
- `bink2_put_block_f` / `bink2_add_block_f` (rounding and clamping only; the
  float IDCT itself stays scalar);
- YUV to RGB conversion.

Selection is at compile time: NEON when `__ARM_NEON` is defined (always on
arm64), SSE2 when `__SSE2__` or `_M_X64` is defined (always on x86-64),
otherwise scalar. A CMake option `BINK2_SIMD` (default `ON`) forces scalar
when off. The scalar versions stay compiled in every build as internal
functions so tests can compare against them. SIMD sources live beside
`bink2_dsp.c` (for example `bink2_dsp_neon.c`, `bink2_dsp_sse2.c`) and are
added to `BINK2_DECODER_SOURCES`.

The encoder picks up the SIMD routines automatically through the shared
functions.

### Testing

- Existing golden tests and encoder `recon` tests pass unchanged in every
  phase; with `BINK2_THREADS=1`, with the default, and with
  `-DBINK2_SIMD=OFF`.
- New test: decode synthetic `KB2j` files at 2, 3, 4 and 8 slices
  serially and threaded, comparing every plane of every frame.
- New `test_malformed.c` cases run threaded: a slice offset that points
  before the previous slice's end, one past the packet, and a corrupted
  slice in the middle of the frame. Each must fail exactly as serial does
  (rule 4).
- New DSP test: each SIMD routine against its scalar version on random and
  edge-case inputs (coefficients that saturate, MC at frame borders and at
  every sub-pel position, odd widths for RGB).
- A ThreadSanitizer build (`-fsanitize=thread`) runs the suite for Phase 3;
  the ASan/UBSan build runs for every phase.

### Docs to update with the code (rule 8)

`docs/architecture.md` (threading model, slice context, SIMD files),
`docs/api.md` (threading behaviour, `BINK2_THREADS`, thread safety of a
handle: still one thread per handle at a time), `docs/tools.md`
(`bink2_bench`), README status list, and the `BINK2_SIMD` CMake option in
the build section.

## Left to the builder

- Exact names and layout of the slice context and pool types.
- Pool internals (condition variable vs. semaphore), within the constraints
  above.
- Which routines get SIMD, from the profile; dropping a candidate that shows
  no gain.
- Benchmark output format.

## Calls made in this design, not yet confirmed

- Threads are capped at the CPU count, and the calling thread decodes a
  slice.
- `BINK2_THREADS` environment variable as the only off switch.
- No numeric speed target; success is identical output plus a measured gain.
- No runtime CPU dispatch; SSE2 and NEON only.
- RGB conversion is threaded too.
- This document lives in `docs/` next to `audio-encoder-design.md`, not in
  `docs/superpowers/`, which AGENTS.md marks as historical.
