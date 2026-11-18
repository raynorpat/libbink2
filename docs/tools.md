# Command-line tools

The command-line tools and players are built when `BINK2_BUILD_TOOLS` is
`ON`. Players are added when their platform dependencies are present:

```sh
cmake -S . -B build -DBINK2_BUILD_TOOLS=ON
cmake --build build
```

| Tool | Purpose | Requires |
| --- | --- | --- |
| `bink2_build` | Encode images to a `.bk2` file | `BINK2_BUILD_ENCODER=ON` (the default) |
| `bink2_dump` | Decode a `.bk2` file to PPM frames and print plane checksums | nothing extra |
| `bink2_sdl_player` | Play with the SDL renderer, with audio | SDL3 or SDL2 |
| `bink2_sdlgl_player` | Play with SDL + OpenGL 3.2 | SDL3 or SDL2 |
| `bink2_sdlmetal_player` | Play with SDL + Metal | macOS, SDL3 or SDL2 |
| `bink2_sdlvulkan_player` | Play with SDL + Vulkan | SDL, Vulkan loader, shaderc; MoltenVK on macOS |
| `bink2_d3d9_player` | Play with Direct3D 9 | Windows |
| `bink2_d3d11_player` | Play with Direct3D 11 | Windows |

If a dependency is not found, that player is skipped with a status message and
the other tools still build. The Direct3D players use a raw Win32 window and
waveOut audio; they do not need SDL.

The renderer and player layer is adapted from
[libbink](https://github.com/raynorpat/libbink) and remains
LGPL-2.1-or-later.

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
| `-o`, `--output FILE` | see below | Output path. |
| `--fps N[/D]` | `30` | Frame rate as an integer or a fraction. |
| `--keyframe-interval N` | `150` | Frames between keyframes; `0` makes every frame a keyframe. |
| `--motion-range N` | `8` | Motion search radius, 0..64. |
| `--slices N` | `2` | Slices per frame: 2, 3, 4, or 8. |
| `--quality N` | `80` | 0..100. Ignored when `--bitrate` is nonzero. |
| `--bitrate N` | `0` | Target bits per second; `0` selects quality mode. |
| `--audio FILE` | off | Add one PCM WAV track. Mono or stereo, 8/16/24-bit. |
| `--audio-quality N` | `80` | 0..100 audio quality; ignored without `--audio`. |
| `--four-mv 0\|1` | `1` | Allow four motion vectors per macroblock. |
| `--recon FILE` | off | Write the encoder's reconstruction as raw YUV 4:2:0. |
| `--verify` | off | Decode the result and compare it with the reconstruction. |
| `--verbose` | off | Print progress to `stderr`. |
| `--quiet` | | Cancel `--verbose`. |
| `-h`, `--help` | | Print usage and exit 0. |
| `--version` | | Print the version and exit 0. |

Without `-o`, the output is written to the current directory and named after
the last component of `<input>` with its extension replaced by `.bk2`:
`shots/intro.png` and `shots/intro` both give `intro.bk2`. An input ending in
a path separator, such as `frames/`, gives `out.bk2`.

`--audio` adds one Bink Audio 2 DCT track from a PCM RIFF WAV: mono or stereo,
8, 16, or 24-bit, at 1..65535 Hz. Float and ADPCM WAV are rejected. There is
no resampling; the track keeps the WAV rate. Audio is truncated or padded with
silence to the video duration, and `--audio-quality` scales the quantiser
rather than targeting a bitrate. The audio is lossy; `--verify` checks that
every chunk decodes and that the PCM lengths match, not that the samples match
the WAV.

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

Force the SDL version at configure time with `-DBINK2_SDL_VERSION=2` or `3`;
the default `AUTO` prefers SDL3.

The renderer players share the same controls and test hooks:

| Key | Action |
| --- | --- |
| Space | Pause or resume |
| Left / Right | Seek one second |
| M | Toggle motion-compensated interpolation |
| Esc, Q | Quit |

Environment hooks: `BINK2_PLAYER_TEST_FRAMES=N` exits after N frames,
`BINK2_PLAYER_TEST_SEEK=N` seeks before playing, `BINK2_PLAYER_T=0..1` sets the
interpolation time, `BINK2_PLAYER_SHADER=present|interp` picks the initial
shader, and `BINK2_PLAYER_DUMP=frame.ppm` renders one frame for readback and
writes it as a PPM. The Direct3D player uses `BINK2_D3D_TEST_FRAMES` and
`BINK2_D3D_SHADER`.

The players decode YUV planes and convert them with the same full-range
BT.601 matrix as `bink2_get_video`. The renderers sample the per-8×8 luma
motion field derived from the decoder's per-32×32 macroblock vectors; half-pel
vectors are rounded to whole pixels for interpolation.

Backend build options (all default `ON` where the platform applies):

```
-DBINK2_BUILD_PLAYER_OPENGL=ON
-DBINK2_BUILD_PLAYER_METAL=ON
-DBINK2_BUILD_PLAYER_VULKAN=ON
-DBINK2_BUILD_PLAYER_D3D9=ON
-DBINK2_BUILD_PLAYER_D3D11=ON
```

Player smoke tests run with `SDL_VIDEODRIVER=dummy` and return `77` when the
backend cannot create a context, so CTest reports them as skipped rather than
failed. OpenGL, Metal, and Vulkan need a real window; a normal desktop run
with `BINK2_TEST_MEDIA` set is the manual check. Direct3D tests run only on
Windows.

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
