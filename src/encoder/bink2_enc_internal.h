#ifndef LIBBINK2_ENC_INTERNAL_H
#define LIBBINK2_ENC_INTERNAL_H

#include "bink2_encoder.h"
#include "bink2_video.h"

#include <stdint.h>

/** Tight YUV 4:2:0 frame (stride == visible width). */
typedef struct {
    unsigned char *y;
    unsigned char *u;
    unsigned char *v;
    int stride[3];
    int width;
    int height;
} Bink2Frame;

int bink2_frame_alloc(Bink2Frame *frame, int width, int height);
void bink2_frame_free(Bink2Frame *frame);
int bink2_enc_read_frame(const bink2_enc_source *source, unsigned long index,
                         Bink2Frame *frame);
int bink2_enc_encode_intra(const bink2_enc_source *source,
                           const bink2_enc_options *options, FILE *file,
                           short *quality);

#endif
