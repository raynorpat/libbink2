/**
    libbink2 - Bink 2 encoder
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
*/

#ifndef LIBBINK2_ENCODER_H
#define LIBBINK2_ENCODER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BINK2_ENC_OK = 0,
    BINK2_ENC_ERR_USAGE = 1,
    BINK2_ENC_ERR_INPUT = 2,
    BINK2_ENC_ERR_ENCODE = 3,
    BINK2_ENC_ERR_VERIFY = 4
} bink2_enc_status;

/**
 * One audio track. `sample_count` is per channel. `channels == 0` disables
 * audio.
 */
typedef struct {
    unsigned long rate;
    int channels;
    unsigned long sample_count;
} bink2_enc_audio;

/**
 * Frame source. Supply either `read_frame` (packed RGB24, width*height*3
 * bytes, top-down) or `read_frame_yuv` (tight Y, then U, then V planes), or
 * both. Returning nonzero from a callback reports a read failure.
 *
 * When `audio.channels` is nonzero, `read_audio` fills
 * `sample_count * channels` interleaved host-endian signed 16-bit samples for
 * the requested range. It must be repeatable.
 */
typedef struct {
    int width;
    int height;
    unsigned long frame_count;
    unsigned long fps_num;
    unsigned long fps_den;
    bink2_enc_audio audio;
    int (*read_frame)(void *user, unsigned long frame, unsigned char *dst);
    int (*read_frame_yuv)(void *user, unsigned long frame, unsigned char *y,
                          unsigned char *u, unsigned char *v);
    int (*read_audio)(void *user, unsigned long start_sample,
                      unsigned long sample_count, short *dst);
    void *user;
} bink2_enc_source;

typedef struct {
    unsigned keyframe_interval; /* frames between keyframes; 0 = all intra */
    unsigned motion_range;      /* search radius in luma samples, 0..64 */
    unsigned slices;            /* 2, 3, 4 or 8 */
    int four_mv;                /* allow four motion vectors per macroblock */
    int quality;                /* 0..100; higher is larger/better */
    unsigned target_bitrate;    /* bits per second; 0 = quality mode */
    int audio_quality;          /* 0..100; higher is larger/better */
    int verify;                 /* round-trip through the decoder */
    const char *recon_path;     /* optional tight raw YUV 4:2:0 dump */
    int verbose;                /* progress on stderr */
} bink2_enc_options;

void bink2_enc_options_default(bink2_enc_options *options);

/**
 * Encode a KB2j file to `output_path`. On failure returns a non-OK status and,
 * when `err` is non-NULL, writes a NUL-terminated message into it.
 */
bink2_enc_status bink2_enc_encode(const bink2_enc_source *source,
                                  const bink2_enc_options *options,
                                  const char *output_path, char *err,
                                  size_t errlen);

#ifdef __cplusplus
}
#endif

#endif
