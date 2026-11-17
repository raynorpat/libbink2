# libbink2

`libbink2` is a C99 library for decoding Bink 2 video files (`.bk2`,
`KB2*`), in the spirit of [libbink](https://github.com/raynorpat/libbink).

## Status

The decoder and tools are implemented:

- `KB2f` through `KB2n` video, YUV 4:2:0.
- Alpha for `KB2f`. `KB2g`+ alpha streams are rejected at open until that
  syntax is ported; decoding them half-written would corrupt every plane.
- Bink Audio 2, up to seven tracks.
- Keyframe seeking and a 2 GB file ceiling.
- `bink2_dump`, `bink2_build` (images to `.bk2`), and an SDL player.

The encoder writes `KB2j` from RGB images (PPM, BMP, PNG, JPEG) or a YUV
source and supports quality and target-bitrate modes; see `docs/encoder.md`. The in-loop deblocking
filter is not implemented on decode, matching the pinned FFmpeg reference;
decoded output differs from RAD's decoder by a few levels around block edges.

## License

`libbink2` is licensed under the GNU Lesser General Public License,
version 2.1 or later. See `LICENSE`.

The Bink 2 decoder is derived from the unmerged FFmpeg Bink 2 patches by
Konstantin Shishkov and Paul B Mahol, which are LGPL-2.1-or-later. Bink Audio
2 is derived from FFmpeg's `binkaudio.c` by Peter Ross and Daniel Verkamp.
See `docs/bink2.md`.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.16 or newer and a C99 compiler. The SDL player is built when
an SDL3 or SDL2 CMake package is found; `-DBINK2_SDL_VERSION=2` or `3` forces
a backend.

Options:

- `BINK2_BUILD_STATIC` (default `ON`)
- `BINK2_BUILD_SHARED` (default `ON`)
- `BINK2_BUILD_TOOLS` (default `OFF`) — builds `bink2_dump` and the player
- `BINK2_BUILD_TESTS` (default `ON`)
- `BINK2_SDL_VERSION` (`AUTO`, `2`, `3`; default `AUTO`)

## Usage

```c
#include <bink2.h>

bink2 b = bink2_open_file("video.bk2", BINK2_MODE_DISK);
if (!b)
    return 1;

bink2_enable_video(b, 1);
bink2_enable_audio(b, 0, 1);
bink2_first(b);
/* bink2_get_plane(b, BINK2_PLANE_Y), bink2_get_video(b),
   bink2_get_audio(b, 0), ... */
bink2_close(b);
```

## Tests

Synthetic tests need no media. Reference and end-to-end tests read the two
sample files from `BINK2_TEST_MEDIA`:

```sh
BINK2_TEST_MEDIA="$HOME/Downloads" ctest --test-dir build --output-on-failure
```

Golden checksums are generated from the pinned FFmpeg Bink 2 oracle and the
`bink2enc`-generated `KB2j` fixture; see `tests/README.md`. `tools/validate.sh`
runs the pinned oracle over an encoded file and fails on any diagnostic.
