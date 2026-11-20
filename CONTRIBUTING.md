# Contributing to libbink2

Bug reports, test files, and patches are welcome. This page covers setup, the
checks a change must pass, and what a pull request should contain.

By contributing you agree that your work is licensed under the GNU Lesser
General Public License, version 2.1 or later, the same as the rest of the
library. See [LICENSE](LICENSE).

## Set up

You need CMake 3.16 or newer and a C99 compiler. SDL2 or SDL3 is needed only
for the player. Python 3 is needed only to regenerate test references.

```sh
git clone https://github.com/raynorpat/libbink2.git
cd libbink2
cmake -S . -B build -DBINK2_BUILD_TOOLS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Every test should pass or skip. The reference tests (`decode_f`,
`decode_g`, `audio`) and the player tests skip unless the reference samples
are available; see [Reference samples](#reference-samples).
`enc_audio_oracle` skips unless `BINK2_ORACLE` points at the pinned FFmpeg
build; see [tests/README.md](tests/README.md#encoded-audio-oracle).

## Find your way around

- [docs/architecture.md](docs/architecture.md) maps the source tree and
  explains the design constraints.
- [docs/bink2.md](docs/bink2.md) describes the format.
- [docs/README.md](docs/README.md) lists everything else.

## Report a bug

Open an issue at <https://github.com/raynorpat/libbink2/issues> with:

- the libbink2 commit, your OS, and your compiler;
- the file's first line of `bink2_dump` output (revision, size, frame count);
- what happened and what you expected, including the first bad frame number
  if the picture is wrong;
- for a crash, a sanitizer trace if you can produce one.

Do not attach copyrighted game video. If the problem needs a specific file,
describe where it comes from, or reduce it to a clip you have the right to
share. A file produced by `bink2_build` is always fine to attach.

Report memory-safety bugs triggered by a crafted file the same way; the
project has no private security channel.

## Make a change

1. Branch from `main`.
2. Write the test first where you can. Each test is a small program in
   `tests/` that returns nonzero on failure; `tests/test_common.h` provides
   `CHECK` and the checksum helpers. Register it in `tests/CMakeLists.txt`.
3. Keep the change focused. Unrelated cleanup belongs in its own pull request.
4. Update the documentation that describes what you changed.

### Code style

- C99 without extensions; no warnings under `-Wall -Wextra`.
- Four-space indent, no tabs, lines wrapped at about 80 columns, function
  braces on their own line.
- `bink2_*` and `bink2_enc_*` for public symbols, `Bink2CamelCase` for
  internal types, `static` for file-local functions.
- No new dependencies in the libraries. The decoder uses libc and libm only.

### Rules that are easy to break

- **Golden checksums are not adjustable.** `tests/reference_checksums.h` is
  generated from a pinned FFmpeg build. If your change alters a checksum, the
  change is wrong unless you can show the oracle agrees with it.
- **The encoder shares the decoder's reconstruction code.** Do not duplicate
  the IDCT, dequantisation, or motion compensation on the encoder side.
- **The decoder reads untrusted files.** Bound every size and offset you read
  and add a malformed-input case to `tests/test_malformed.c`.
- **Licensing.** Only add code you wrote or code under a license compatible
  with LGPL-2.1-or-later, and credit its source in `docs/bink2.md`. Do not
  contribute code derived from RAD's SDK or from a disassembly of RAD's
  binaries.

## Local checks

Run these before opening a pull request.

```sh
# 1. normal build and tests
cmake --build build && ctest --test-dir build --output-on-failure

# 2. sanitizer build, required for decoder and parser changes
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DBINK2_BUILD_TOOLS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

# 3. thread sanitizer build, required for slice threading changes
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DBINK2_BUILD_TOOLS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=thread"
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure

# 4. scalar build, required for SIMD changes
cmake -S . -B build-scalar -DBINK2_BUILD_TOOLS=ON -DBINK2_SIMD=OFF
cmake --build build-scalar
ctest --test-dir build-scalar --output-on-failure

# 5. whitespace
git diff --check
```

The project has no CI yet, so these local runs are the only gate. Paste the
`ctest` summary into the pull request.

For encoder changes, also encode something with `--verify`:

```sh
build/bink2_build tests/media/fixture.png -o /tmp/fixture.bk2 --verify
```

For audio encoder changes, add `--audio file.wav` with a PCM WAV of your own,
and run `enc_audio_oracle` with `BINK2_ORACLE` set if you can build the
oracle. Encoded audio is lossy: the tests check SNR and PCM length, so do not
add byte-exact audio comparisons.

### Reference samples

The reference tests compare the decoder with the pinned oracle on two files,
`sc13_01_partial.bk2` and `ge_video_86l.bk2`. They are not redistributable and
are not in the repository. If you have them, point the tests at their
directory:

```sh
BINK2_TEST_MEDIA=<dir-with-samples> ctest --test-dir build --output-on-failure
```

If you do not, say in the pull request that the reference tests were skipped
and a maintainer will run them. Regenerating references is a maintainer task
described in [tests/README.md](tests/README.md).

## Pull requests

- Title in the style of the existing history: `feat: ...`, `fix: ...`,
  `test: ...`, `docs: ...`, or `chore: ...`, imperative and lower case.
- Describe what changed and why, and how you tested it, including which tests
  were skipped.
- One logical change per pull request. Rebase on `main` rather than merging
  it in.
- A maintainer reviews every pull request. Expect questions about bitstream
  changes: say which part of the pinned FFmpeg source or which sample file
  supports the behaviour.

## Using coding agents

Agent-assisted contributions are accepted on the same terms as any other. You
are responsible for the change: read it, build it, and run the checks above.
Agents should follow [AGENTS.md](AGENTS.md).

## Credits

Contributors are listed in [CONTRIBUTORS.md](CONTRIBUTORS.md). Add yourself in
your first pull request if you would like to be named.
