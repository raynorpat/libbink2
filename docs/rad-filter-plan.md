# RAD Divergences Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Study RAD's `bink2w32.dll` statically, hand off behaviour notes,
and implement the deblocking and DC reconstruction stages from those notes,
on by default with an opt-out.

**Architecture:** Phase 1 (Tasks 1–4) runs outside the repository and ends
at the owner's review of `~/Projects/libbink2-re/handoff/notes.md`. Phase 2
(Tasks 5–11) is done by agents that read only that notes file, this plan,
the design, and the repository. The stages live in one shared decoder
module. Both `bink2_video_decode` and the encoder's frame writer call it, so
`recon` stays byte-identical.

**Tech Stack:** C99 and CMake/CTest. Phase 1 uses the
`technical-reverse-engineering` skill (REA), plus Ghidra only if REA isn't
enough. The RAR is extracted with `unar` (installed).

**Spec:** `docs/rad-filter-design.md`

## Global Constraints

- C99, `C_EXTENSIONS OFF`, `-Wall -Wextra` warning-free, four-space indent,
  about 80 columns (AGENTS.md).
- No new dependencies. Nothing under `third_party/`.
- Never committed and never inside the repository tree: the RAR, any DLL,
  disassembly, decompiler output, study scratch files, or RAD-decoded
  frames.
- The notes and all code describe behaviour in their own words, never
  transliterated disassembly.
- **The Phase 2 implementation agents read only
  `~/Projects/libbink2-re/handoff/notes.md` from the study workspace.**
  Each Phase 2 task prompt must say so.
- `tests/reference_checksums.h` is not changed. No tolerance is weakened.
- The only public API change is `bink2_enable_filters` (Task 5).
  `bink2_encoder.h` is unchanged.
- A stage the owner held back in Task 4 is not implemented. Its task is
  replaced by keeping it in the docs' known-divergence list.
- Coverage is decided per revision at open. Filtering never switches off
  partway through a stream.
- Commits: Conventional Commit subjects, lower case, one logical change,
  ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Media and oracle runs set
  `BINK2_TEST_MEDIA="$HOME/Downloads"` and
  `BINK2_ORACLE="$PWD/build/oracle/FFmpeg-bink2/ffmpeg"`.
  Skipped tests are named when reporting.

## Review Focus

1. **Toggling the switch between two inter frames.** Expected: no drift and
   no half-filtered GOP. The new setting takes effect at the next keyframe.
   Pinned in Task 7, `toggle_latches_at_keyframe`.
2. **Seeking with the stages on.** `bink2_seek_keyframe(N)` followed by
   `bink2_next` should give the same planes as decoding straight through to
   that frame. Pinned in Task 7, `seek_matches_linear`.
3. **Picture sizes that aren't a multiple of 32** (the 160x120 `KB2j`
   fixture, `sc13` at 736 rows). The stages must touch only what the notes
   say at the right and bottom borders, and never read outside the planes.
   Pinned in Task 7, `border_planes_bounded`, run under ASAN.
4. **A corrupt frame after good ones.** The restored picture is the last
   good frame as filtered, and the next keyframe decodes correctly. Pinned
   in Task 7, `error_restores_filtered`.
5. **A stream whose revision the notes don't cover.** It decodes exactly
   the same with the switch on or off. Pinned in Task 7,
   `uncovered_revision_unfiltered`.

---

## Phase 1: study (outside the repository)

### Task 1: Set up the study workspace

**Files:**
- Create: `~/Projects/libbink2-re/{src,work,handoff}/` (outside the repo)

- [ ] **Step 1:** Download the RAR into a new, empty
  `~/Projects/libbink2-re/src/`:
  `gh api repos/marcussacana/Bink2/contents/<rar name> --jq .download_url`,
  then `curl -L -o`. Extract it with `unar -o ~/Projects/libbink2-re/src/x`.
  Never run any `.exe` or `.dll` from it.
- [ ] **Step 2:** Identify the original (unpatched) `bink2w32.dll` and
  record in `work/provenance.md`: its path in the archive, its SHA-256, its
  version resource (`strings -el`), and the SHA-256 of both patched copies.
  Expected: three distinct hashes.
- [ ] **Step 3:** Load the original DLL into REA, following the skill's
  instructions, and confirm the analysis lists its exports.

### Task 2: Locate the stages and their place in the frame decode

**Interfaces:**
- Produces: `work/map.md`. It covers notes items 1–3 for each stage: where
  it sits relative to slice decode, IDCT and put/add, MC, the reference
  swap, and output conversion; whether the next frame's reference is
  filtered; and when it runs (revision, frame flags, block types, planes,
  the meaning of the `0x10000` row and column arrays).

- [ ] **Step 1:** From the exported frame-decode entry point, trace down to
  the per-slice macroblock loop. Anchor it with constants libbink2 already
  knows: the `KB2x` revision checks, the frame-flag bits `0x10000`,
  `0x8000`, and `0x4000`, and the tables in `src/bink2_tables.c`.
- [ ] **Step 2:** Find every routine that reads or writes pixels after
  reconstruction and before output conversion. Classify each one as
  deblocking, DC reconstruction, or something else. Record the other
  differences in a "Not in scope" section.
- [ ] **Step 3:** Settle whether DC reconstruction exists as a decoder
  stage. If it doesn't, write that down with the evidence (the routines
  searched and why none qualifies).
- [ ] **Step 4:** For each stage, write down which revisions reach it, and
  which revisions take a code path that skips it.

### Task 3: Write the handoff notes

**Files:**
- Create: `~/Projects/libbink2-re/handoff/notes.md`

- [ ] **Step 1:** Write one section per stage, covering design items 1–6:
  - placement;
  - when it runs;
  - inputs and edges (8x8, 16x16, 32x32, slice, frame border);
  - arithmetic as math or plain pseudocode in your own words: tables as
    data, integer widths, rounding, clamping, and edge order;
  - at least three worked examples, each a small pixel array plus
    parameters with the expected output (filtered, below threshold, and a
    rounding or clamping case), worked by hand from the code;
  - a `[read]` or `[inferred]` tag on every claim.
- [ ] **Step 2:** Add a header with the DLL's version and SHA-256, a table
  of revisions covered for each stage (every frame-level case accounted
  for, or the revision marked uncovered), the open questions, and the "Not
  in scope" list.
- [ ] **Step 3:** Self-check: grep the notes for addresses
  (`0x[0-9a-f]{6,}`), register names, and decompiler identifiers
  (`FUN_`, `local_`, `param_`). Expected: none, except table data.

### Task 4: Owner review gate

- [ ] **Step 1:** Give the owner `~/Projects/libbink2-re/handoff/notes.md`
  and stop.
- [ ] **Step 2:** Record the owner's decision at the top of the notes:
  each stage approved or held back, and the `[inferred]` claims accepted.
  Phase 2 starts only after this is written.

---

## Phase 2: implementation (reads only the approved notes)

### Task 5: Opt-out switch

**Files:**
- Modify: `src/bink2.h` (the "ENABLE/DISABLE SWITCHES" block),
  `src/bink2.c`, `src/bink2_internal.h`, `src/bink2_video.h`
- Modify: `src/tools/bink2_dump.c`, `src/tools/bink2_bench.c`
- Modify: `tests/test_decode_f.c`, `tests/test_decode_g.c`, and every test
  that compares against `reference_checksums.h` (find them with
  `grep -l reference_checksums tests/*.c`)
- Test: `tests/test_api.c`

**Interfaces:**
- Produces: `char bink2_enable_filters(bink2 object, unsigned char enable);`
  It returns `0`, or `BINK2_ERROR` for a NULL object. The default is
  enabled. It sets `object->filters_requested`. `Bink2Video` gains
  `int filters_on`, which is latched from `filters_requested` each time a
  keyframe is decoded (`bink2_first`, `bink2_seek_keyframe`, or a keyframe
  reached by `bink2_next`).
- Produces: `bink2_dump --no-filters` and `bink2_bench --no-filters`.

- [ ] **Step 1:** Add to `test_api.c`:
  `CHECK(bink2_enable_filters(NULL, 0) == BINK2_ERROR);` and, on an opened
  fixture, `CHECK(bink2_enable_filters(b, 0) == 0);` and
  `CHECK(bink2_enable_filters(b, 1) == 0);`.
- [ ] **Step 2:** Run `ctest --test-dir build -R '^api$'`. Expected: build
  failure (undeclared function).
- [ ] **Step 3:** Implement the switch and the latching. In every
  reference-checksum test, call `bink2_enable_filters(b, 0)` right after
  `bink2_enable_video(b, 1)`. Add `--no-filters` to dump and bench. If
  `tests/run_dump.cmake` compares against reference values, make it pass
  `--no-filters`.
- [ ] **Step 4:** Run the full suite with media and oracle. Expected: all
  pass. With no stage implemented yet, output is unchanged.
- [ ] **Step 5:** Update `docs/api.md` (the function, its latching rule, the
  default) and `docs/tools.md` (both flags, plus the usage text in each
  tool). Commit: `feat: add bink2_enable_filters switch`.

### Task 6: Deblocking stage module and unit tests

**Files:**
- Create: `src/bink2_filter.c`, `src/bink2_filter.h`. Add the `.c` to
  `BINK2_DECODER_SOURCES`.
- Create: `tests/test_filter.c`, with an `add_executable`/`add_test` pair
  named `filter`.

**Interfaces:**
- Produces: `int bink2_filter_covers(int version, int stage);` where
  `stage` is `BINK2_STAGE_DEBLOCK` (0) or `BINK2_STAGE_DCRECON` (1). It
  returns 1 exactly for the revisions the approved notes mark covered.
- Produces: one pure edge or block primitive with the signature the notes
  imply. It works on caller-owned pixel memory with an explicit stride or
  step and the notes' parameters, does no allocation, and does not touch
  `Bink2Video`.
- Produces: `void bink2_filter_deblock(Bink2Video *v);`. It applies the
  stage to the planes the notes name, in the notes' edge order, using the
  stored per-frame state from Task 7.

- [ ] **Step 1:** Write `test_filter.c`. There is one test per worked
  example in the notes, named `deblock_example_<n>`. Each builds the
  example's input buffer, calls the primitive, and compares every output
  byte with the notes' expected values. Add
  `covers_matches_notes`, which checks `bink2_filter_covers` for every
  revision from `'f'` to `'n'` against the notes' coverage table.
- [ ] **Step 2:** Run `ctest -R '^filter$'`. Expected: build failure.
- [ ] **Step 3:** Implement the primitive and `bink2_filter_covers` from the
  notes.
- [ ] **Step 4:** Run `ctest -R '^filter$'`. Expected: PASS. If an example
  disagrees, stop and report which one. Do not change the expected values:
  the notes are the reference.
- [ ] **Step 5:** Implement `bink2_filter_deblock` as a loop over edges that
  calls the primitive. Commit: `feat: add the deblocking filter primitive`.

### Task 7: Run deblocking in the decoder

**Files:**
- Modify: `src/bink2_video.c` (`decode_impl`; replace `skip_block_flags`
  with a storing reader if the notes say the `0x10000` arrays steer the
  filter), `src/bink2_video.h`
- Test: new `tests/test_filter_decode.c` (CTest name `filter_decode`),
  `tests/test_malformed.c`

**Interfaces:**
- Consumes: `bink2_filter_covers`, `bink2_filter_deblock`, and
  `Bink2Video.filters_on` (Tasks 5 and 6).
- Produces: `void bink2_video_apply_filters(Bink2Video *v);`. It runs every
  approved, covered stage on the just-decoded frame when `v->filters_on` is
  set. `Bink2Video` gains `unsigned char *row_flags, *col_flags` (sizes
  `row_size` and `col_size` as computed in `decode_impl`) if the arrays are
  used.
- If the notes say the stage is in-loop, it runs in place on `cur` after
  the slice status checks and before the alpha copy, so the next swap
  makes the filtered picture the reference. If they say it's a post-filter,
  `Bink2Video` gains `out[4]`. The stage writes there, and
  `bink2_video_plane` and the RGB path read from `out` while `cur` and
  `ref` stay unfiltered.

- [ ] **Step 1:** Write the tests in `test_filter_decode.c`. Tests needing
  media exit 77 without `BINK2_TEST_MEDIA`.
  - `filters_change_output`: for a covered sample, at least one frame's
    luma hash differs between filters on and off.
  - `threads_match`: filtered hashes for every frame are equal with
    `bink2_internal_set_threads(b, 1)` and with the default.
  - `toggle_latches_at_keyframe`: decode to an inter frame with filters on,
    call `bink2_enable_filters(b, 0)`, then decode to the next keyframe.
    Every frame before that keyframe matches an all-on decode. Every frame
    from the keyframe on matches an all-off decode.
  - `seek_matches_linear`: hashes after `seek_keyframe(k)` plus
    `bink2_next` × 2 equal the straight-through decode's hashes at those
    frames.
  - `border_planes_bounded`: decode all 32 frames of
    `tests/media/synthetic_kb2j.bk2` filtered. It must succeed, and every
    plane must match the unfiltered decode outside the area the notes allow
    the stage to touch.
  - `error_restores_filtered`: after N good frames, feed a truncated packet.
    `bink2_get_plane` equals frame N's filtered planes, and the next
    keyframe matches a clean decode.
  - `uncovered_revision_unfiltered`: for a fixture of a revision
    `bink2_filter_covers` rejects, filtered and unfiltered hashes are equal
    on every frame. If every fixture's revision is covered, test this at
    the `bink2_video_apply_filters` level with `v->version` forced to an
    uncovered one.
- [ ] **Step 2:** If the row and column arrays are now stored, add to
  `test_malformed.c` a frame with `0x10000` set whose arrays run past the
  end of the packet. Expected: `BINK2_ERROR`, not a read overrun.
- [ ] **Step 3:** Run `ctest -R '^(filter_decode|malformed)$'`. Expected:
  FAIL.
- [ ] **Step 4:** Implement `bink2_video_apply_filters`, the call site, and
  the array storage. If the stage reads across slice edges, run it after
  all slices have joined. Otherwise it may run per slice inside
  `decode_slice_job`.
- [ ] **Step 5:** Run the full suite on `build`, `build-asan`, `build-tsan`,
  and `build-scalar` (the last with `BINK2_THREADS=1`), with media.
  Expected: all pass, and the reference tests are unchanged. Commit:
  `feat: apply the deblocking filter on decode`.

### Task 8: DC reconstruction stage

Skip this task if Task 4 held the stage back, or if Task 2 found no such
stage. Either way, Task 11 documents the outcome.

**Files:** `src/bink2_filter.c`, `src/bink2_filter.h`,
`tests/test_filter.c`, `tests/test_filter_decode.c`, `src/bink2_video.c`

**Interfaces:**
- Produces: the notes' primitive for this stage and
  `void bink2_filter_dcrecon(Bink2Video *v);`. They're called from
  `bink2_video_apply_filters` in the order the notes give relative to
  deblocking.

- [ ] **Step 1:** Add `dcrecon_example_<n>` tests from the notes' worked
  examples, and extend `covers_matches_notes` to
  `BINK2_STAGE_DCRECON`. Run them. Expected: FAIL.
- [ ] **Step 2:** Implement the primitive and the frame function, and call
  them from `bink2_video_apply_filters`.
- [ ] **Step 3:** Run `filter` and `filter_decode` (all of Task 7's tests
  now cover both stages), then the four build variants as in Task 7, Step
  5. Expected: PASS. Commit:
  `feat: apply DC reconstruction on decode`.

### Task 9: Encoder reconstructs through the stages

**Files:**
- Modify: `src/encoder/bink2_enc_video.c` (before each
  `copy_reconstruction(frame, v)`)
- Test: `tests/test_enc_inter.c`, `tests/test_enc_intra.c`,
  `tests/test_enc_api.c`

**Interfaces:**
- Consumes: `bink2_video_apply_filters` (Task 7). The encoder's
  `Bink2Video` latches `filters_on = 1` at each keyframe it writes.

- [ ] **Step 1:** Confirm that the existing `recon` comparisons, which
  decode with the default (filters on), now fail when Task 7 or Task 8
  changes `KB2j` output. Run
  `ctest -R '^(enc_inter|enc_intra|enc_api)$'`. If `KB2j` is uncovered,
  they pass already. Record that in the commit message and still make the
  call, so coverage added later applies automatically.
- [ ] **Step 2:** Call `bink2_video_apply_filters(v)` before
  `copy_reconstruction(frame, v)` at every site, so `update_reference`
  copies the filtered picture for an in-loop stage.
- [ ] **Step 3:** Add to `test_enc_inter.c`, `recon_matches_unfiltered_off`:
  decoding the same file with `bink2_enable_filters(b, 0)` differs from
  `recon` if and only if `bink2_filter_covers('j', ...)` is true for some
  stage. This pins the accepted drift.
- [ ] **Step 4:** Run the full suite. Expected: PASS. Update
  `docs/encoder.md` (the reconstruction guarantee and the accepted
  FFmpeg/opt-out drift) and the validation paragraph in `docs/tools.md`
  (`validate.sh` checks diagnostics only, not filtered pixels). Commit:
  `feat: reconstruct encoder frames through the decoder filters`.

### Task 10: Filtered regression checksums

**Files:**
- Create: `tests/capture_filtered.sh`, `tests/filtered_checksums.h`
  (generated), `tests/test_decode_filtered.c` (CTest name
  `decode_filtered`, exits 77 without media)
- Modify: `tests/README.md`

**Interfaces:**
- Produces: `bink2_kb2f_filtered_checksums[]`,
  `bink2_kb2g_filtered_checksums[]`, and `bink2_kb2j_filtered_checksums[]`,
  all `Bink2FrameChecksum` (same struct and same frame ranges as
  `reference_checksums.h`). The header's comment says: "libbink2's own
  filtered output; pins behaviour, not a correctness reference."

- [ ] **Step 1:** Write `capture_filtered.sh`. It runs `build/bink2_dump`
  (filters on) over the two samples and the `KB2j` fixture, and pipes the
  YUV through `tests/checksum_frames.py` to emit the header. Run it.
- [ ] **Step 2:** Write `test_decode_filtered.c`. It mirrors
  `test_decode_f.c` with filters on, against the new arrays. Run
  `ctest -R '^decode_filtered$'` with media. Expected: PASS.
- [ ] **Step 3:** Document in `tests/README.md` what the header is, how to
  regenerate it, and that a change to it means a filter behaviour change.
  Commit: `test: pin filtered decoder output`.

### Task 11: Docs and final verification

**Files:** `docs/bink2.md`, `docs/api.md`, `docs/architecture.md`,
`docs/encoder.md`, `README.md`, `tests/README.md`

- [ ] **Step 1:** In `docs/bink2.md`, replace "Known divergence" with a
  description of the stages: when they run, the revisions covered, and in
  or out of the loop. Credit it as "derived from study of RAD's decoder".
  Keep any held-back or uncovered stage or revision listed as a remaining
  divergence. In the `docs/api.md` support table, set "Deblocking" (and
  add "DC reconstruction") to the covered revisions. In
  `docs/architecture.md`, rewrite "The pinned FFmpeg tree is the
  correctness target" for two paths: the FFmpeg checksums cover filters
  off, and the filtered checksums pin our own output. Update the
  divergence sentences in `README.md` and `tests/README.md`.
- [ ] **Step 2:** Run the full matrix from AGENTS.md (`build`, `build-asan`,
  `build-tsan`, `build-scalar` with `BINK2_THREADS=1`) with media and
  oracle. Expected: all pass. Only the GL, Metal, and Vulkan player tests
  skip, and they are named in the report.
- [ ] **Step 3:** `git grep -nE 'FUN_|local_[0-9a-f]|param_[0-9]'`.
  Expected: no matches. Commit:
  `docs: describe the RAD filter stages`.
