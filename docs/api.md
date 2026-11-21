# API reference

`libbink2` has two public headers: `bink2.h` (decoder, in `libbink2`) and
`bink2_encoder.h` (encoder, in `libbink2enc`). Both are plain C99 and usable
from C++. Everything else under `src/` is internal and may change.

## Linking

With CMake, add the repository as a subdirectory and link the alias targets:

```cmake
add_subdirectory(libbink2)
target_link_libraries(my_app PRIVATE Bink2::bink2)      # decoder
target_link_libraries(my_tool PRIVATE Bink2::encoder)   # encoder + decoder
```

`Bink2::bink2` resolves to the shared library when `BINK2_BUILD_SHARED` is
`ON` and to the static one otherwise. `Bink2::encoder` is always static and
links the decoder, because the encoder reconstructs frames with decoder code.

An installed tree provides `bink2.h`, `bink2_encoder.h`, `libbink2` and
`libbink2enc.a`. No CMake package config or pkg-config file is installed, so
link installed libraries by name (`-lbink2enc -lbink2 -lm`, plus
`-lpthread` on Unix-likes when linking the static library).

## Threads

Neither library keeps mutable global state. Separate decoder handles, and
separate `bink2_enc_encode()` calls, can run on different threads at the same
time. One handle must not be used from two threads at once.

The decoder decodes a frame's slices in parallel. Each handle starts its own
worker threads on the first frame with more than one slice, uses up to one
thread per slice (the calling thread decodes one of them), and stops them in
`bink2_close()`. `bink2_get_video()` splits its RGB conversion across the same
threads. A file has 1 to 8 slices, chosen when it was encoded, so the
speedup depends on the file. The output is identical to serial decoding.

The thread count is capped at the number of online CPUs. Set the environment
variable `BINK2_THREADS` to an integer from 1 to 64 to cap it instead;
`BINK2_THREADS=1` decodes serially and starts no threads. The variable is read
when a handle starts its threads, so set it before the first frame call. If
the threads cannot be started, the handle decodes serially.

## Decoder (`bink2.h`)

### Decode loop

```c
#include <bink2.h>

int play(const char *path)
{
    unsigned long width, height, frames;
    double usf;
    char status;
    bink2 b = bink2_open_file(path, BINK2_MODE_DISK);

    if (!b)
        return 1;
    bink2_info_all(b, NULL, &frames, &usf);
    bink2_info_video(b, &width, &height, NULL);
    bink2_enable_video(b, 1);
    bink2_enable_audio(b, 0, 1);

    status = bink2_first(b);
    while (status == BINK2_MORE || status == BINK2_LAST) {
        const unsigned char *rgb = bink2_get_video(b);          /* width*height*3 */
        const unsigned char *pcm = bink2_get_audio(b, 0);       /* may be NULL */
        unsigned long pcm_bytes = bink2_get_audio_size(b, 0);

        /* present rgb, queue pcm, wait usf microseconds */
        (void)rgb; (void)pcm; (void)pcm_bytes;

        if (status == BINK2_LAST)
            break;
        status = bink2_next(b);
    }
    bink2_close(b);
    return status == BINK2_ERROR;
}
```

Video and audio are both **disabled** after open. A frame call decodes only
what is enabled, so enable what you need before `bink2_first()`.

`bink2_enable_filters(b, enable)` switches the decoder's post-reconstruction
filters; they are **enabled** after open. It returns `0`, or `BINK2_ERROR`
for a `NULL` object. The setting is latched whenever a keyframe is decoded
(by `bink2_first()`, `bink2_seek_keyframe()`, or `bink2_next()` reaching a
keyframe) and applies to that keyframe and the inter frames after it, so a
change made mid-stream takes effect at the next keyframe.

### Opening and closing

| Function | Notes |
| --- | --- |
| `bink2_open_file(filename, mode)` | Opens and owns the file. `mode` exists for libbink API compatibility; `BINK2_MODE_MEMORY` currently behaves like `BINK2_MODE_DISK`. |
| `bink2_open_filepointer(file, mode)` | Reads from a stream you opened in binary mode. The stream is **not** closed by `bink2_close()`; close it yourself afterwards. |
| `bink2_open_memory(buffer, size)` | Decodes from a buffer. The buffer is not copied; keep it alive and unchanged until `bink2_close()`. |
| `bink2_close(object)` | Frees everything. `NULL` is accepted. |

Open returns `NULL` when the file is not a `KB2f`..`KB2n` stream, when the
header is implausible (zero or over 7680x4800 dimensions, zero or over
1,000,000 frames, a zero frame rate, more than seven audio tracks, an audio
track with a zero sample rate), or when it is a `KB2g`+
stream with alpha. `bink2_open_file()` prints one line to `stderr` for the
alpha case; no other open failure prints anything.

Files are limited to 2 GB because the container uses 32-bit offsets and I/O
goes through `fseek`.

### File information

All three return `0` on success and `BINK2_ERROR` for a `NULL` object. Any
out-pointer may be `NULL`.

| Function | Reports |
| --- | --- |
| `bink2_info_all` | Current frame index (0-based), frame count, and `usf`: microseconds per frame. |
| `bink2_info_video` | Visible width and height, and the container video flags (`BINK2_FLAG_ALPHA`, `BINK2_FLAG_GRAY`). |
| `bink2_info_audio` | `track_mask` with bit `n` set for track `n`, plus channel count, bit depth, and sample rate per track. The arrays must hold seven entries. |

### Frame stepping

| Function | Effect |
| --- | --- |
| `bink2_first` | Rewinds to frame 0 and decodes it. |
| `bink2_next` | Decodes the following frame. |
| `bink2_seek_keyframe(b, n)` | Decodes the nearest keyframe at or before `n`. `n` is clamped to the last frame whose data is present in the file. |

Return values:

| Value | Meaning |
| --- | --- |
| `BINK2_MORE` | A frame was decoded and more follow. |
| `BINK2_LAST` | The last available frame was decoded, or there is no further frame. |
| `BINK2_ERROR` | The frame could not be decoded. |

`BINK2_OK` and `BINK2_DONE` are both `0` and are never returned by the three
calls above; test for `BINK2_MORE` and `BINK2_LAST`.

Things to know:

- A truncated file ends early with `BINK2_LAST` rather than an error. Frames
  whose packets start beyond the real end of file are treated as absent.
- After `bink2_next()` returns `BINK2_ERROR`, the frame index has still
  advanced. Calling `bink2_next()` again skips the bad frame, but inter frames
  that follow depend on it and are only reliable from the next keyframe.
- An audio decode failure does not fail the frame. That track yields no PCM
  for the frame; the picture and other tracks continue.
- `bink2_first()` and `bink2_seek_keyframe()` clear the audio overlap state,
  so the first audio after a seek does not blend with pre-seek audio.

### Frame data

Returned pointers belong to the decoder and are valid until the next frame
call or `bink2_close()`. Fetch them again after every frame: the Y, U and V
planes alternate between two buffers, so a pointer kept from an earlier frame
points at an older picture.

| Function | Returns |
| --- | --- |
| `bink2_get_video` | Packed RGB24, top-down, `width * height * 3` bytes with no row padding. Converted on demand from YUV with full-range BT.601. |
| `bink2_get_plane(b, plane)` | One 8-bit plane: `BINK2_PLANE_Y`, `_U`, `_V`, or `_A`. Chroma is 4:2:0, so `(width+1)/2` by `(height+1)/2` visible samples. |
| `bink2_get_stride(b, plane)` | Bytes per row of that plane. Planes are padded to a multiple of 32 luma samples, so stride is usually larger than the visible width; always step rows by stride. |
| `bink2_get_audio(b, track)` | Interleaved native-endian signed 16-bit PCM for this frame, or `NULL`. |
| `bink2_get_audio_size(b, track)` | Byte length of that PCM, or `0`. |
| `bink2_get_motion` | Per-macroblock motion vectors; see below. |

The video getters return `NULL` (stride `0`) until a frame has been decoded
with video enabled. The audio getters return `NULL`/`0` for a disabled or
out-of-range track.

`bink2_get_motion()` exposes the vectors of the current frame for analysis
or visualisation. Macroblocks are 32x32 luma samples; `mb_w` and `mb_h`
receive the grid size. `mv_x`, `mv_y`, and `mv_count` each hold
`mb_w * mb_h * 4` entries indexed `(by * mb_w + bx) * 4 + k`. Vectors are in
half-pel luma units. `mv_count` is `1` or `4` for a motion-compensated
macroblock and `0` otherwise. It returns `1` when the data is available.

### Supported streams

| Feature | Support |
| --- | --- |
| Revisions | `KB2f` through `KB2n` |
| Pixel format | 8-bit YUV 4:2:0 |
| Alpha | `KB2f` only; `KB2g`+ alpha files fail to open |
| Audio | Bink Audio 2, up to seven tracks |
| Seeking | Keyframes only |
| Deblocking | Not implemented; see [bink2.md](bink2.md#known-divergence) |

## Encoder (`bink2_encoder.h`)

The encoder writes a complete `KB2j` file in one call. It pulls frames and
optional audio from callbacks you provide. Video output is 8-bit YUV 4:2:0
with no alpha; audio is one optional Bink Audio 2 track.

```c
#include <bink2_encoder.h>
#include <stdio.h>

static int read_rgb(void *user, unsigned long frame, unsigned char *dst)
{
    /* fill dst with width*height*3 bytes of top-down RGB24 */
    return 0; /* nonzero reports a read failure */
}

int encode(void *frames_ctx)
{
    bink2_enc_source source = {0};
    bink2_enc_options options;
    char err[256];

    source.width = 640;
    source.height = 360;
    source.frame_count = 120;
    source.fps_num = 30;
    source.fps_den = 1;
    source.read_frame = read_rgb;
    source.user = frames_ctx;

    bink2_enc_options_default(&options);
    options.quality = 85;
    options.verify = 1;

    if (bink2_enc_encode(&source, &options, "out.bk2", err, sizeof(err))
            != BINK2_ENC_OK) {
        fprintf(stderr, "encode failed: %s\n", err);
        return 1;
    }
    return 0;
}
```

### Source

| Field | Constraint |
| --- | --- |
| `width`, `height` | 1..7680 and 1..4800. Odd sizes are accepted. |
| `frame_count` | 1..1,000,000 |
| `fps_num`, `fps_den` | Each 1..4294967295; the header stores them as 32-bit fields |
| `read_frame` | Packed RGB24, top-down, `width*height*3` bytes |
| `read_frame_yuv` | Tight Y, U, V planes (chroma `(width+1)/2` by `(height+1)/2`) |
| `audio.rate` | 1..65535 Hz when audio is enabled |
| `audio.channels` | 0 disables audio; 1 or 2 enables it |
| `audio.sample_count` | Samples per channel; must be nonzero with audio |
| `read_audio` | Interleaved host-endian s16 for the requested sample range; required with audio |
| `user` | Passed back to the callbacks |

Set at least one frame callback. Use `read_frame_yuv` when you already have
YUV and want to avoid a colour conversion. `read_audio` fills
`sample_count * channels` shorts starting at `start_sample` and returns 0, or
nonzero on failure. It must be repeatable because the encoder may request the
same range more than once. The encoder truncates or silence-pads the track to
the video duration; it never resamples.

### Options

Always start from `bink2_enc_options_default()`.

| Field | Default | Meaning |
| --- | --- | --- |
| `keyframe_interval` | 150 | Frames between keyframes; `0` makes every frame a keyframe. Scene cuts add keyframes regardless. |
| `motion_range` | 8 | Luma search radius, 0..64. `0` disables the search. |
| `slices` | 2 | 2, 3, 4, or 8. |
| `four_mv` | 1 | Allow four motion vectors per macroblock. |
| `quality` | 80 | 0..100; used when `target_bitrate` is `0`. |
| `target_bitrate` | 0 | Video bits per second; nonzero selects bitrate mode. Audio is not counted, so a file with audio is larger by the audio bitrate. |
| `audio_quality` | 80 | 0..100 for the audio track; ignored without audio. |
| `verify` | 0 | Decode the written file and compare it to the encoder's reconstruction. |
| `recon_path` | `NULL` | Write the reconstruction as tight raw YUV 4:2:0. |
| `verbose` | 0 | Progress on `stderr`. |

### Status codes

| Status | Value | Cause |
| --- | --- | --- |
| `BINK2_ENC_OK` | 0 | File written. |
| `BINK2_ENC_ERR_USAGE` | 1 | `NULL` argument, or a source or option value out of range. |
| `BINK2_ENC_ERR_INPUT` | 2 | A frame or audio callback returned nonzero, or audio setup failed. |
| `BINK2_ENC_ERR_ENCODE` | 3 | The output could not be created or written, or encoding failed. |
| `BINK2_ENC_ERR_VERIFY` | 4 | `verify` was set and the decoded file did not match. |

On failure `err` receives a NUL-terminated message when it is non-`NULL`.
The output file may exist in a partial state after a failure; delete it.

How the encoder makes its decisions is described in [encoder.md](encoder.md).
