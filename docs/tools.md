# Command-line tools

Three programs are built when `BINK2_BUILD_TOOLS` is `ON`:

```sh
cmake -S . -B build -DBINK2_BUILD_TOOLS=ON
cmake --build build
```

| Tool | Purpose | Requires |
| --- | --- | --- |
| `bink2_build` | Encode images to a `.bk2` file | `BINK2_BUILD_ENCODER=ON` (the default) |
| `bink2_dump` | Decode a `.bk2` file to PPM frames and print plane checksums | nothing extra |
| `bink2_sdl_player` | Play a `.bk2` file with audio | an SDL3 or SDL2 CMake package |

If SDL is not found, the player is skipped with a status message and the
other tools still build.

## bink2_build

```
bink2_build <input> [-o output.bk2] [options]
```

`<input>` is either one image or a directory of images. Supported formats are
`.ppm`, `.bmp`, `.png`, `.jpg`, and `.jpeg`. Directory entries are ordered by
natural sort, so `frame_2.png` precedes `frame_10.png`.

```sh
build/bink2_build frames/ -o clip.bk2 --fps 30000/1001 --quality 85 --verify
```

| Option | Default | Meaning |
| --- | --- | --- |
| `-o`, `--output FILE` | `<input>.bk2` | Output path. |
| `--fps N[/D]` | `30` | Frame rate as an integer or a fraction. |
| `--keyframe-interval N` | `150` | Frames between keyframes; `0` makes every frame a keyframe. |
| `--motion-range N` | `8` | Motion search radius, 0..64. |
| `--slices N` | `2` | Slices per frame: 2, 3, 4, or 8. |
| `--quality N` | `80` | 0..100. Ignored when `--bitrate` is nonzero. |
| `--bitrate N` | `0` | Target bits per second; `0` selects quality mode. |
| `--four-mv 0\|1` | `1` | Allow four motion vectors per macroblock. |
| `--recon FILE` | off | Write the encoder's reconstruction as raw YUV 4:2:0. |
| `--verify` | off | Decode the result and compare it with the reconstruction. |
| `--verbose` | off | Print progress to `stderr`. |
| `--quiet` | | Cancel `--verbose`. |
| `-h`, `--help` | | Print usage and exit 0. |
| `--version` | | Print the version and exit 0. |

Exit codes match `bink2_enc_status`:

| Code | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Bad command line or option value |
| 2 | Input missing or unreadable |
| 3 | Encoding or writing failed |
| 4 | `--verify` found a mismatch |

The output is `KB2j` video with no audio. See [encoder.md](encoder.md) for
what each option does to the bitstream.

## bink2_dump

```
bink2_dump [--frames N] file.bk2 [outdir]
```

Decodes video and writes `frame_00000.ppm`, `frame_00001.ppm`, ... into
`outdir` (default: the current directory). The directory must already exist.
`--frames N` stops after `N` frames.

It prints a header line, then one line per frame with the FNV-1a hash of each
visible plane:

```
$ build/bink2_dump --frames 1 tests/media/synthetic_kb2j.bk2 /tmp/out
tests/media/synthetic_kb2j.bk2: 160x120, 32 frames, 25.000 fps
frame 0 y=11899069354599489311 u=2874139801710016741 v=14167469714912273877
```

These are the same hashes as the golden values in
`tests/reference_checksums.h`, which makes the tool useful for locating the
first frame that diverges from a reference.

Exit codes: `0` success, `1` open, decode, or write failure, `2` bad command
line.

## bink2_sdl_player

```
bink2_sdl_player video.bk2
```

Plays video at the file's frame rate and mixes all audio tracks to stereo.

| Key | Action |
| --- | --- |
| Space | Pause or resume |
| Left / Right | Seek 30 frames back or forward, snapped to the preceding keyframe |
| Esc, Q | Quit |

Exit codes: `0` success, `1` open or decode failure, `2` bad command line,
`77` SDL could not create a window or renderer (the test suite treats 77 as
a skip).

`BINK2_PLAYER_TEST_FRAMES=N` makes the player exit after `N` frames. The
`sdl` test uses it together with `SDL_VIDEODRIVER=dummy` and
`SDL_AUDIODRIVER=dummy` to run headless.

Force a backend at configure time with `-DBINK2_SDL_VERSION=2` or `3`; the
default `AUTO` prefers SDL3.

## tools/validate.sh

```
tools/validate.sh file.bk2
```

A maintainer script, not an installed tool. It runs the pinned FFmpeg Bink 2
oracle over a file and fails if the oracle prints any diagnostic. It needs the
oracle binary, which `tests/capture_reference.sh` builds into `build/oracle/`;
see [tests/README.md](../tests/README.md) and
[encoder.md](encoder.md#validation).

Exit codes: `0` clean decode, `1` oracle rejected the file or printed
diagnostics, `2` usage error or oracle not found, `77` 8-slice file (not
checkable with the pinned oracle).
