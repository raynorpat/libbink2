/**
    libbink2 - A C library for decoding Bink 2 video files
    Copyright (C) 2026 libbink2 contributors

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, see
    <http://www.gnu.org/licenses/>.

    The Bink 2 decoder is derived from the unmerged FFmpeg Bink 2
    patches by Konstantin Shishkov and Paul B Mahol, which are
    LGPL-2.1-or-later.
*/

#ifndef LIBBINK2_BINK2_H
#define LIBBINK2_BINK2_H

/* needed for FILE* here */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** forward declaration for the opaque decoder object */
typedef struct bink2_t *bink2;

/** return codes from bink2_first(), bink2_next() and bink2_seek_keyframe() */
#define BINK2_OK     0x00
#define BINK2_DONE   0x00
#define BINK2_MORE   0x01
#define BINK2_LAST   0x02
#define BINK2_ERROR  -1

/** file-processing mode, pass to bink2_open_file() */
#define BINK2_MODE_DISK    0x00
#define BINK2_MODE_MEMORY  0x01

/** plane indices for bink2_get_plane() */
#define BINK2_PLANE_Y  0
#define BINK2_PLANE_U  1
#define BINK2_PLANE_V  2
#define BINK2_PLANE_A  3

/** video flags reported by bink2_info_video() */
#define BINK2_FLAG_ALPHA 0x00100000
#define BINK2_FLAG_GRAY  0x00020000

#ifdef __cplusplus
extern "C" {
#endif

/* OPEN OPERATIONS */
/**
 * Open a Bink 2 file from disk. `mode` is accepted for libbink API
 * compatibility; disk files are read on demand and `BINK2_MODE_MEMORY` is
 * currently equivalent to `BINK2_MODE_DISK`. Use bink2_open_memory() to
 * decode from a buffer.
 */
bink2 bink2_open_file(const char *filename, unsigned char mode);
/** open a Bink 2 file from an existing stream */
bink2 bink2_open_filepointer(FILE *file, unsigned char mode);
/** open a Bink 2 file from a memory buffer */
bink2 bink2_open_memory(const unsigned char *buffer, unsigned long size);

/* CLOSE OPERATIONS */
/** close a Bink 2 file and release all memory */
void bink2_close(bink2 object);

/* GET FILE INFO OPERATIONS */
char bink2_info_all(const bink2 object, unsigned long *frame,
                    unsigned long *frame_count, double *usf);
char bink2_info_video(const bink2 object, unsigned long *w, unsigned long *h,
                      unsigned long *flags);
char bink2_info_audio(const bink2 object, unsigned char *track_mask,
                      unsigned char channels[7], unsigned char bitdepth[7],
                      unsigned long audio_rate[7]);

/* ENABLE/DISABLE SWITCHES */
char bink2_enable_video(bink2 object, unsigned char enable);
char bink2_enable_audio(bink2 object, unsigned char track,
                        unsigned char enable);

/* FRAME OPERATIONS */
/** rewind to the first frame and decode it */
char bink2_first(bink2 object);
/** advance to the next frame and decode it */
char bink2_next(bink2 object);
/** seek to the first keyframe at or before N and decode it */
char bink2_seek_keyframe(bink2 object, unsigned long frame);

/* FRAME DATA ACCESS */
/** retrieve the current frame as packed RGB24, converted on demand */
const unsigned char *bink2_get_video(const bink2 object);
/** retrieve a decoded Y/U/V/A plane */
const unsigned char *bink2_get_plane(const bink2 object, int plane);
/** retrieve the row stride in bytes of a decoded plane */
int bink2_get_stride(const bink2 object, int plane);
/** native-endian s16 for this frame (mono S0 S1, stereo L0 R0), or NULL */
const unsigned char *bink2_get_audio(const bink2 object, unsigned char track);
/** byte length of that PCM, or 0 when the pointer is NULL */
unsigned long bink2_get_audio_size(const bink2 object, unsigned char track);
/**
 * Per-32x32-macroblock motion vectors of the current frame, in half-pel
 * luma samples. Arrays hold mb_w * mb_h * 4 entries, indexed
 * (by * mb_w + bx) * 4 + k. mv_count is 1 or 4 for a motion-compensated
 * macroblock and 0 otherwise. Returns 1 when available and 0 when the
 * file has no enabled video. Any out-parameter may be NULL.
 */
int bink2_get_motion(const bink2 object, unsigned long *mb_w,
                     unsigned long *mb_h, const int16_t **mv_x,
                     const int16_t **mv_y, const unsigned char **mv_count);

#ifdef __cplusplus
}
#endif

#endif
