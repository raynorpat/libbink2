# AGENTS.md

Instructions for coding agents working in this repository. Human contributors
should start with [CONTRIBUTING.md](CONTRIBUTING.md); the commands and rules
are the same.

libbink2 is a C99 library that decodes Bink 2 video (`.bk2`, `KB2f`..`KB2n`)
and encodes `KB2j`. It has no dependencies beyond libc and libm. SDL is
optional and used only by the player.

## Commands

Run from the repository root. `build/` and `build-*/` are ignored by git.

```sh
# configure with the tools, build, test
cmake -S . -B build -DBINK2_BUILD_TOOLS=ON
cmake --build build
ctest --test-dir build --output-on-failure

# one test by name (names are in tests/CMakeLists.txt)
ctest --test-dir build --output-on-failure -R '^enc_inter$'

# include the tests that need the sample files
BINK2_TEST_MEDIA=<dir-with-samples> ctest --test-dir build --output-on-failure

# sanitizer build; use it for any change to parsing or decoding
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DBINK2_BUILD_TOOLS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

A clean run is 27 tests with `BINK2_BUILD_TOOLS=ON` on macOS (the GL, Metal,
and Vulkan player smoke tests skip when `SDL_VIDEODRIVER=dummy` cannot create a
context; Windows adds the D3D9/D3D11 tests). Without `BINK2_TEST_MEDIA`, the
reference tests and player tests report `Skipped` (exit code 77). A skip is
not a pass: say so when reporting results.

The sample files are `sc13_01_partial.bk2` and `ge_video_86l.bk2`. They are
not in the repository and cannot be redistributed. If you do not have them,
run the synthetic tests and state that the reference tests were skipped.

## Where things are

| Path | Contents |
| --- | --- |
| `src/bink2.h` | Public decoder API |
| `src/encoder/bink2_encoder.h` | Public encoder API |
| `src/` | Decoder and codec code shared with the encoder |
| `src/encoder/` | Encoder |
| `src/tools/` | `bink2_build`, `bink2_dump`, `bink2_sdl_player` |
| `tests/` | One `test_*.c` program per CTest test; `test_common.h` has `CHECK` and the hash helpers |
| `tests/reference_checksums.h` | Generated golden values; do not hand-edit |
| `docs/` | [Index](docs/README.md), [architecture](docs/architecture.md), [API](docs/api.md), [tools](docs/tools.md), [format](docs/bink2.md), [encoder](docs/encoder.md) |

Read [docs/architecture.md](docs/architecture.md) before changing library
code. `docs/superpowers/` is historical planning material; do not treat it as
current and do not update it.

## Code conventions

- C99, no compiler extensions (`C_EXTENSIONS OFF`). Builds with
  `-Wall -Wextra` and must stay warning-free.
- Four-space indent, no tabs, lines wrapped at about 80 columns, function
  braces on their own line. Match the file you are editing.
- Public symbols are `bink2_*` and `bink2_enc_*`; internal types are
  `Bink2CamelCase`; file-local functions are `static`.
- A new source file must be added to `BINK2_DECODER_SOURCES` or
  `BINK2_ENCODER_SOURCES` in `CMakeLists.txt`. A new test needs an
  `add_executable` and `add_test` pair in `tests/CMakeLists.txt`.

## Rules

Each rule exists because of a specific change in this repository's history.

1. **Do not regenerate or edit `tests/reference_checksums.h` to make a test
   pass.** The values come from the pinned FFmpeg oracle via
   `tests/capture_reference.sh` (commit `6952c68`). A decoder change that
   alters a checksum is a decoder bug until proven otherwise. Regenerate only
   when the fixtures or the oracle pin change, and say so in the commit.
2. **The encoder must reconstruct with the decoder's functions.** Do not add
   an encoder-side copy of dequantisation, the IDCT, or motion compensation.
   The byte-exact `recon` guarantee depends on shared code (commit `9bcbedb`);
   `test_enc_intra`, `test_enc_inter`, and `test_enc_api` compare every frame.
3. **Every bitstream writer needs a test that reads it back with the decoder's
   reader and checks both end at the same bit position.** Writer and reader
   can disagree on edge cases that ordinary frames rarely hit, such as the
   AC coefficient at scan position 63, which takes no terminator (commits
   `11494f2`, `e4e04e0`).
4. **New parsing code needs a malformed-input case in
   `tests/test_malformed.c`,** run under the sanitizer build. The decoder
   review found bitstream errors that were swallowed instead of propagated,
   which the well-formed fixtures never exercised (commit `7ef74b5`).
5. **Reject streams the decoder cannot fully decode at open; do not decode
   them partially.** `KB2g`+ alpha was claimed as supported and wrote a
   half-initialised alpha plane until it was rejected in commit `7ef74b5`.
6. **A test that cannot run exits 77, never 0.** `tools/validate.sh` returned
   success for 8-slice files it had not checked until commit `fc58f6d`.
7. **No code from `SBerda/bink2enc` enters the library.** It is used only as
   an external fixture generator (commit `c7f7e8c`, `tests/README.md`).
   Code derived from FFmpeg must be LGPL-2.1-or-later and credited in
   `docs/bink2.md`.
8. **Update the docs in the same change as the behaviour.** The README listed
   alpha support the decoder did not have (fixed in `7ef74b5`). When an
   option, flag, exit code, or limit changes, update `docs/api.md`,
   `docs/tools.md`, the `usage()` text in `src/tools/bink2_build.c`, and the
   README status list as applicable.

## Boundaries

**Always**

- Build and run the test suite before reporting work as done, and report
  skipped tests as skipped.
- Add or update a test with every behaviour change.

**Ask first**

- Changing a signature or constant in `bink2.h` or `bink2_encoder.h`. The
  decoder API deliberately mirrors libbink.
- Adding a dependency or a file under `third_party/`.
- Changing the oracle or `bink2enc` pin in `tests/capture_reference.sh`.
- Regenerating `tests/reference_checksums.h` or the files in `tests/media/`.
- Committing a new binary fixture.

**Never**

- Commit Bink sample files, RAD SDK material, or other third-party media.
- Commit build directories or `.superpowers/`.
- Weaken a tolerance or delete a failing assertion to get a green run.
- Push, open a pull request, or file an issue unless the user asks. When
  asked, branch from `main` and follow
  [CONTRIBUTING.md](CONTRIBUTING.md#pull-requests).

## Commits

History uses Conventional Commit subjects: `feat:`, `fix:`, `test:`, `docs:`,
`chore:`, lower case, imperative, no trailing period, for example
`feat: decode Bink Audio 2`. Keep one logical change per commit. Follow the
user's instructions on attribution trailers; existing commits have none.
