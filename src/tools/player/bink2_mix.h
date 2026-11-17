/**
    libbink2 - playback audio mixer
    Copyright (C) 2026 libbink2 contributors
    Adapted from libbink.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
*/

#ifndef LIBBINK2_BINK2_MIX_H
#define LIBBINK2_BINK2_MIX_H

#include <stdint.h>

typedef struct {
    const int16_t *pcm;
    unsigned long samples; /* per channel */
    unsigned long rate;
    int channels;
    /* Source position, in samples, of output frame 0. Zero starts at the
       first sample; carry bink2_mix_next_phase from one packet to the next
       so a resampled track does not restart its phase at every packet. */
    double phase;
} Bink2MixTrack;

/* Output frames this track's samples cover at out_rate, from its phase. */
unsigned long bink2_mix_frames(const Bink2MixTrack *track,
                              unsigned long out_rate);

/* Phase for the track's next packet after bink2_mix_frames frames. */
double bink2_mix_next_phase(const Bink2MixTrack *track, unsigned long out_rate);

/* Write dst_frames frames of out_channels. Return 0, or -1 on a NULL
   destination or a zero rate. */
int bink2_mix(const Bink2MixTrack *tracks, int count,
             unsigned long out_rate, int out_channels,
             int16_t *dst, unsigned long dst_frames);

/* Bytes of PCM for one picture at the device rate. */
unsigned long bink2_frame_bytes(unsigned long rate, int channels, double usf);

/* 1 while this picture still owes one frame of played PCM.
   A queue that has already given up frame_bytes is not a reason to wait.
   An empty queue does not wait. */
int bink2_audio_waiting(unsigned long queued_at_mark,
                       unsigned long queued_now,
                       unsigned long frame_bytes);

/* Mixed-PCM byte offset of picture `frame` on the device timeline. */
unsigned long bink2_soundtrack_offset(unsigned long rate, int channels,
                                     double usf, unsigned long frame);

#endif
