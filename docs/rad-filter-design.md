# Closing the RAD divergences: deblocking and DC reconstruction

Status: design, not yet implemented.

## Intent

`docs/bink2.md` lists two known divergences between libbink2 and RAD's own
decoder:

1. the in-loop deblocking filter, which neither the pinned FFmpeg tree nor
   NihAV implements, so libbink2's output differs from RAD's by a few levels
   around block edges;
2. the "block reconstruction from DCs" refinement described in the public
   reverse-engineering notes.

The aim is to close both, so libbink2's decoded pictures match what RAD's
decoder produces, and the README and docs no longer have to list them.

No source for either exists outside RAD. The only description of them is in
RAD's shipped decoder binary. The work is therefore split in two, kept
apart on purpose:

- **Study.** One agent studies RAD's decoder DLL statically and writes
  plain-language notes on what the two stages do.
- **Implementation.** A second agent, which never sees the DLL or its
  disassembly, implements from those notes alone.

## Source material

`github.com/marcussacana/Bink2` holds one RAR containing RAD's Bink 2
encoder, an original retail `bink2w32.dll`, and two patched copies of it.
The owner has approved studying the DLL for how it works. The study uses the
**original** DLL. The patched copies are only for working out which code the
patch touched, if that turns out to matter.

That material is unlicensed RAD property. It is never committed, never
copied into the repository tree, and never linked or loaded by anything in
the repository.

## Goals

- Notes good enough to implement both stages without looking at the binary.
- A decoder that applies both stages by default, with an opt-out.
- The FFmpeg golden checksums still pass, byte for byte, with the stages
  switched off.
- The encoder's byte-exact `recon` guarantee still holds with the stages on.
- Docs that describe the new behaviour and drop the divergence sections.

## Non-goals

- Running RAD's DLL. The owner chose static study only (approach A, below).
  Nothing in this work loads or executes the DLL, under Wine or anywhere
  else.
- Byte-exact equality with RAD's output, proven against RAD frames. Without
  running the DLL there are no RAD frames to compare with. Matching rests on
  the notes being right.
- SIMD versions of the new stages. Scalar first. Vectorising them is a
  separate change.
- Other differences between the DLL and libbink2 that the study turns up.
  Those are written down in the notes and left for later.

## Anti-goals

Any of these makes the work a failure, even if the filter works:

- Decompiled or disassembled code, or a near line-by-line transliteration
  of it, in the notes, the repository, or a commit. The notes describe
  behaviour: conditions, thresholds, arithmetic, rounding, order.
- The implementation agent reading the DLL, the disassembly, or any study
  scratch file other than the finished notes.
- A changed value in `tests/reference_checksums.h`, or a changed tolerance,
  to get a green run.
- An encoder-side copy of the filter. The encoder calls the decoder's
  function (Rule 2).
- The stages run on some streams the notes don't cover, producing a
  half-filtered picture. Where the notes don't cover a revision or a case,
  the decoder leaves that stream unfiltered and the docs say so.

## Constraints

- C99, warning-free, scalar code, no new dependencies (AGENTS.md).
- Changing `bink2.h` needs the owner's approval. The owner has approved one
  addition: the opt-out switch described below.
- Rules 3 and 4 apply to any newly parsed syntax. If the stages use the
  `0x10000` row and column bit arrays that `src/bink2_video.c` now reads and
  throws away, that parse needs a malformed-input case in
  `tests/test_malformed.c`. If new syntax is ever written by the encoder, it
  also needs a writer/reader round-trip test.
- Threading: slices decode in parallel. A filter that reads across slice
  edges has to run after every slice of the frame is done, or else follow
  whatever order the notes say RAD uses. Any change in this area needs a
  run of the TSAN build.

## Approaches

The open question was how much the study phase should prove, not just
describe.

- **A. Static study only (chosen).** Disassemble the DLL, find the two
  stages, and describe them in notes. This is the cheapest option and needs
  no Windows runtime. The catch is that nothing checks the notes against
  RAD's real output, so rounding and off-by-one errors can get through.
- **B. Static study plus running the DLL as a reference.** Build a 32-bit
  loader with `i686-w64-mingw32-gcc`, run the original DLL under Wine or
  a UTM Windows VM, and dump RAD's decoded frames as pass/fail references.
  This proves the result, but it means running unlicensed RAD code and
  setting up a Windows runtime.
- **C. B plus tracing inside the DLL.** Capture buffers between stages.
  This is only worth it if B shows mismatches nobody can explain.

The owner picked A. The notes therefore have to make up for having no RAD
frames to test against; see "What the notes must contain".

## Decided

- **Phases:** study, then a handoff of notes, then implementation by a
  separate agent with no access to the binary.
- **Default:** both stages on by default, with an opt-out (the owner's
  option 1 of three: on with opt-out, off with opt-in, always on with no
  switch).
- **The opt-out is part of the public API.** The tests use it to keep
  comparing against the FFmpeg checksums, and users can use it to get the
  old output.
- **Encoder:** whenever the decoder would apply a stage to a frame, the
  encoder's reconstruction calls the same decoder function, so `recon`
  stays byte-identical to the decoder's default output. The owner accepts
  that the encoder's files may then drift in FFmpeg and other non-RAD
  decoders (see Phase 2, Encoder).
- **Review gate:** the owner approves each stage's notes separately before
  it is implemented (see Phase 1).

## Phase 1: study

Where things go (my call):

- `~/Projects/libbink2-re/` is the study workspace, outside the repository.
  It holds the RAR, the extracted DLL, the disassembly, and the working
  notes. Nothing in it is committed.
- `~/Projects/libbink2-re/handoff/notes.md` is the finished notes. This is
  the only file the implementation agent reads.

Tooling: the `technical-reverse-engineering` skill (REA). There is no
Ghidra or Wine on this machine. Installing Ghidra is acceptable if REA isn't
enough. Installing Wine is not needed for A.

### What the notes must contain

For each stage:

1. **Where it sits** in the decode of a frame, relative to the steps
   libbink2 already has: slice decode, IDCT and put/add, motion
   compensation, the swap of reference planes, and output conversion. State
   whether the reference for the next frame is the filtered or the
   unfiltered picture. That is what makes a filter in-loop or a post-filter.
2. **When it runs:** which revisions (`KB2f` to `KB2n`), which frame flags,
   which block types (intra, inter, skipped), which planes (Y, U, V, alpha),
   and whether the `0x10000` row and column arrays switch it on or off per
   edge. If the arrays mean something else, say what.
3. **Inputs:** which quantiser, neighbouring pixels, block modes, or motion
   vectors it reads, and across which edges (8x8, 16x16, 32x32 macroblock,
   slice, frame border).
4. **The arithmetic**, written as math or plain pseudocode in the notes'
   own words: thresholds and tables (table values may be listed, since they
   are data), integer widths, rounding and shifts, clamping, and the order
   in which edges are processed (vertical then horizontal, row by row, and
   so on) wherever the order changes the result.
5. **Worked examples:** at least three per stage. Each one is a small input
   (an edge's pixels plus the parameters) with the output worked out by hand
   from the disassembly. These are the implementer's only check that it read
   the notes the way the study agent meant. Cover a filtered case, an
   unfiltered case (below the threshold), and a clamping or rounding edge
   case.
6. **Confidence:** for each claim, whether it was read directly from the
   code or inferred. List open questions explicitly.

The notes also state:

- whether "block reconstruction from DCs" is a real decoder stage. If the
  DLL has no such stage, the notes say so and phase 2 drops it, with a
  docs-only fix;
- the DLL's version string and the hash of the file studied.

### Phase 1 done when

The notes cover items 1 to 6 for every stage the DLL actually has, and the
owner has reviewed them. That review is the gate for reverse-engineered
material entering the project. It works stage by stage:

- the owner approves or holds back each stage separately;
- a claim marked "inferred" counts as covered only if the owner accepts it
  in the review;
- a stage held back is not implemented in phase 2. It stays listed as a
  known divergence, as today.

## Phase 2: implementation

The implementation agent gets the notes, this design, and the repository.

### API

A new decoder switch in `bink2.h`, with the filters on unless a caller turns
them off. The exact form (a mode bit for `bink2_open_file`, or a setter
such as `bink2_set_filters(bink2, int on)` that also covers
`bink2_open_memory`, which takes no mode) is the builder's call. The rules:

- it must work for all three open functions;
- it must be documented in `docs/api.md`;
- it must be the only public change.

### Decoder

- The stages go in shared codec code (for example a new
  `src/bink2_deblock.c` added to `BINK2_DECODER_SOURCES`), callable on a
  frame's planes.
- Coverage is decided once per stream, at open, from the revision. A
  revision counts as covered for a stage only if the approved notes cover
  every frame-level case for it (all frame flags, block types, and
  row/column array values). Filtering is never switched off partway through
  a stream, because an in-loop stage that stopped mid-GOP would corrupt
  every later reference. Streams of uncovered revisions decode unfiltered,
  and the docs list which revisions are covered. (My call.)

### Encoder

- For `KB2j` output, the encoder's reconstruction calls the decoder's stage
  function wherever the decoder would.
- The encoder already sets frame flag `0x10000` and writes the row arrays
  (`FRAME_FLAGS_INTRA`/`FRAME_FLAGS_INTER` in
  `src/encoder/bink2_enc_video.c`). It keeps writing what it writes now. If
  that switches a stage on, `recon` follows the decoder's default and
  includes the stage. The owner has accepted the result: the encoder's
  `KB2j` files then drift, inter frame by inter frame, when decoded with the
  pinned FFmpeg tree, with any other non-RAD decoder, or with libbink2's
  opt-out. `docs/encoder.md` and `docs/tools.md` say so. `options.verify`
  compares against the decoder's default, so it still applies.
  `tools/validate.sh` checks only the oracle's diagnostics, not pixels, so
  it keeps working. Its docs state that it does not check filtered output.
  Choosing filter flags in the encoder, or adding an encoder switch, is later
  work.
- The `recon` comparisons in `test_enc_intra`, `test_enc_inter`, and
  `test_enc_api` must pass with the decoder at its default (stages on).

### Tests

- **Existing reference tests** (`decode_f`, `decode_g`, the `KB2j` fixture,
  and the rest) open with the stages switched off and must pass unchanged
  against `tests/reference_checksums.h`.
- **Unit tests** for each stage, built from the notes' worked examples. An
  example is checked against the notes' expected output, not against
  libbink2's own output.
- **Filtered regression checksums.** Hashes of libbink2's own output with
  the stages on, for the samples and the `KB2j` fixture. These go in a new
  generated header, separate from `reference_checksums.h`. Its comment says
  they pin libbink2's behaviour and are not a correctness reference. This
  is my call: they catch accidental changes later, but they prove nothing
  about matching RAD.
- **Scalar and threaded:** the filtered output is the same with
  `BINK2_THREADS=1` and with the default thread count, on the default build,
  the scalar build, ASAN, and TSAN.
- **Malformed input:** one case per newly parsed field.

### Docs

Updated in the same change (Rule 8):

- `docs/bink2.md`: the divergence section is replaced with a description of
  the stages. The description is credited as derived from study of RAD's
  decoder, and it says the FFmpeg-based checksums cover only the unfiltered
  path.
- `docs/api.md`: the switch, and the "Deblocking" row in the support table.
- `docs/architecture.md`: "The pinned FFmpeg tree is the correctness
  target" is updated to describe the two-path model.
- `docs/encoder.md`: its divergence section, and the effect on `recon`.
- `README.md` and `tests/README.md`: the divergence sentences.

## Left to the builder

- The exact name and shape of the opt-out.
- File and function names, and how the stage plugs into the slice thread
  pool.
- How to generate the filtered regression checksums (a script under
  `tests/`, matching how `reference_checksums.h` is produced).
- How the work splits into commits, as long as each commit is one logical
  change with a Conventional Commit subject.
