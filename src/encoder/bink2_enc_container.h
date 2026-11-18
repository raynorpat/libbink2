#ifndef LIBBINK2_ENC_CONTAINER_H
#define LIBBINK2_ENC_CONTAINER_H

#include <stdio.h>

typedef struct {
    unsigned long rate;
    int channels;
    unsigned long max_decoded;
    unsigned long id;
    int use_dct;
} Bink2EncAudioTrack;

int bink2_container_write_header(FILE *file, unsigned long width,
                                 unsigned long height, unsigned long fps_num,
                                 unsigned long fps_den,
                                 unsigned long frame_count,
                                 unsigned long flags,
                                 const Bink2EncAudioTrack *tracks,
                                 unsigned long track_count,
                                 unsigned long *index_offset);
/**
 * Write count+1 index entries: count frame offsets with their keyframe bit,
 * then the end offset.
 */
int bink2_container_patch_index(FILE *file, unsigned long index_offset,
                                const unsigned long *offsets,
                                const unsigned char *keys,
                                unsigned long count);

#endif
