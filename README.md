# libbink2

`libbink2` is a C99 library for decoding Bink 2 video files (`.bk2`,
`KB2*`), in the spirit of [libbink](https://github.com/raynorpat/libbink).
It also includes a `KB2j` encoder and three command-line tools. It depends
only on the C library.

## Status

The decoder and tools are implemented:

- `KB2f` through `KB2n` video, YUV 4:2:0.
- Alpha for `KB2f`. `KB2g`+ alpha streams are rejected at open until that
  syntax is ported; decoding them half-written would corrupt every plane.
- Bink Audio 2, up to seven tracks.
- Keyframe seeking and a 2 GB file ceiling.
- Multithreaded decoding: a frame's slices decode in parallel, up to one
  thread per slice (`BINK2_THREADS` caps it; see `docs/api.md`).
- `bink2_dump`, `bink2_bench`, `bink2_build` (images to `.bk2`, optional WAV audio), and SDL
  players for the renderer, OpenGL, Metal, and Vulkan, plus Direct3D 9/11
  players on Windows.

The encoder writes `KB2j` from RGB images (PPM, BMP, PNG, JPEG) or a YUV
source and supports quality and target-bitrate modes plus one optional
Bink Audio 2 WAV track; see `docs/encoder.md`. It does not write alpha.

The decoder applies RAD's in-loop post-reconstruction filters (residual edge
filter, DC reconstruction, deblocking) by default, derived from study of RAD's
decoder; `bink2_enable_filters(b, 0)` turns them off, which matches the pinned
FFmpeg reference. `KB2l` and `KB2n` are not covered and decode unfiltered. See
`docs/bink2.md`.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.16 or newer and a C99 compiler. The default build type is
`Release`.

Options:

| Option | Default | Effect |
| --- | --- | --- |
| `BINK2_BUILD_STATIC` | `ON` | Build `libbink2.a` |
| `BINK2_BUILD_SHARED` | `ON` | Build the shared `libbink2` |
| `BINK2_BUILD_ENCODER` | `ON` | Build the static encoder library `libbink2enc.a` |
| `BINK2_BUILD_TOOLS` | `OFF` | Build `bink2_dump`, `bink2_bench`, `bink2_build`, and the players |
| `BINK2_BUILD_PLAYER_OPENGL` | `ON` | Build `bink2_sdlgl_player` when SDL is found |
| `BINK2_BUILD_PLAYER_METAL` | `ON` | Build `bink2_sdlmetal_player` on macOS |
| `BINK2_BUILD_PLAYER_VULKAN` | `ON` | Build `bink2_sdlvulkan_player` when Vulkan + shaderc are found |
| `BINK2_BUILD_PLAYER_D3D9` | `ON` | Build `bink2_d3d9_player` on Windows |
| `BINK2_BUILD_PLAYER_D3D11` | `ON` | Build `bink2_d3d11_player` on Windows |
| `BINK2_BUILD_TESTS` | `ON` | Build the test programs |
| `BINK2_SIMD` | `ON` | Use NEON (arm64) or SSE2 (x86-64) for the IDCT, block, motion-compensation, and RGB conversion routines; `OFF` builds scalar code only, with identical output |
| `BINK2_SDL_VERSION` | `AUTO` | SDL backend for the player: `AUTO`, `2`, or `3` |

At least one of the static and shared libraries must be enabled.
`bink2_build` also needs `BINK2_BUILD_ENCODER`. The SDL players are built when
an SDL3 or SDL2 CMake package is found; the Vulkan player also needs a Vulkan
loader and shaderc. Missing players are skipped, not fatal.

## Usage

Decode a file:

```c
#include <bink2.h>

bink2 b = bink2_open_file("video.bk2", BINK2_MODE_DISK);
if (!b)
    return 1;

bink2_enable_video(b, 1);
bink2_enable_audio(b, 0, 1);

char status = bink2_first(b);
while (status == BINK2_MORE || status == BINK2_LAST) {
    /* bink2_get_video(b)              packed RGB24
       bink2_get_plane(b, BINK2_PLANE_Y) with bink2_get_stride()
       bink2_get_audio(b, 0)           s16 PCM for this frame */
    if (status == BINK2_LAST)
        break;
    status = bink2_next(b);
}
bink2_close(b);
```

Encode a directory of images:

```sh
cmake -S . -B build -DBINK2_BUILD_TOOLS=ON && cmake --build build
build/bink2_build frames/ -o clip.bk2 --fps 30 --quality 85 --verify
```

## Documentation

| Page | Contents |
| --- | --- |
| [docs/api.md](docs/api.md) | Decoder and encoder API reference |
| [docs/tools.md](docs/tools.md) | `bink2_build`, `bink2_dump`, and the players |
| [docs/architecture.md](docs/architecture.md) | Source map and design decisions |
| [docs/bink2.md](docs/bink2.md) | Container and bitstream notes |
| [docs/encoder.md](docs/encoder.md) | Encoder internals and validation |
| [docs/audio-encoder-design.md](docs/audio-encoder-design.md) | Encoded audio: block syntax, quality control, limits |
| [tests/README.md](tests/README.md) | Reference checksums and the oracle |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Setup, checks, and pull requests |
| [AGENTS.md](AGENTS.md) | Instructions for coding agents |

## Tests

Synthetic tests need no media. Reference and end-to-end tests read the two
sample files from `BINK2_TEST_MEDIA` and are skipped without it:

```sh
BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build --output-on-failure
```

Golden checksums are generated from the pinned FFmpeg Bink 2 oracle and the
`bink2enc`-generated `KB2j` fixture; see `tests/README.md`. `tools/validate.sh`
runs the pinned oracle over an encoded file and fails on any diagnostic;
8-slice files exit 77 because the pinned oracle's 8-slice path is broken, and
the library round trip covers them.

## License

`libbink2` is licensed under the GNU Lesser General Public License,
version 2.1 or later. See `LICENSE`.

The Bink 2 decoder is derived from the unmerged FFmpeg Bink 2 patches by
Konstantin Shishkov and Paul B Mahol, which are LGPL-2.1-or-later. Bink Audio
2 is derived from FFmpeg's `binkaudio.c` by Peter Ross and Daniel Verkamp.
See `docs/bink2.md` and [CONTRIBUTORS.md](CONTRIBUTORS.md).
