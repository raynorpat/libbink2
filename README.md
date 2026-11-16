# libbink2

`libbink2` is a C99 library for decoding Bink 2 video files (`.bk2`,
`KB2*`), in the spirit of [libbink](https://github.com/raynorpat/libbink).

## Status

The decoder and encoder are under construction. The public decoder API is
stable; video and audio decoding land in the following tasks.

## License

`libbink2` is licensed under the GNU Lesser General Public License,
version 2.1 or later. See `LICENSE`.

The Bink 2 decoder is derived from the unmerged FFmpeg Bink 2 patches by
Konstantin Shishkov and Paul B Mahol, which are LGPL-2.1-or-later.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.16 or newer and a C99 compiler.
