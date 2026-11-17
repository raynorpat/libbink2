/**
    libbink2 - Bink Audio 2 decoder
    Copyright (C) 2026 libbink2 contributors

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    Derived from FFmpeg's libavcodec/binkaudio.c by Peter Ross and
    Daniel Verkamp, LGPL-2.1-or-later.
*/

#ifndef LIBBINK2_AUDIO_H
#define LIBBINK2_AUDIO_H

typedef struct Bink2Audio Bink2Audio;

typedef struct {
    unsigned long rate;
    int channels;
    int use_dct;
    int version_2;
    int frame_len;
    int overlap_len;
    int num_bands;
    unsigned int bands[26];
    float root;
    float quant[96];
} Bink2AudioInfo;

int bink2_audio_describe(unsigned long rate, int channels, int use_dct,
                         int version_2, Bink2AudioInfo *info);
int bink2_audio_create(Bink2Audio **out, unsigned long rate, int channels,
                       int use_dct, int version_2, unsigned long max_bytes);
void bink2_audio_reset(Bink2Audio *audio);
void bink2_audio_destroy(Bink2Audio *audio);
int bink2_audio_decode(Bink2Audio *audio, const unsigned char *data,
                       unsigned long size);
const unsigned char *bink2_audio_pcm(const Bink2Audio *audio);
unsigned long bink2_audio_pcm_bytes(const Bink2Audio *audio);

#endif
