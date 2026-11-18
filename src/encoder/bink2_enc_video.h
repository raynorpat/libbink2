#ifndef LIBBINK2_ENC_VIDEO_H
#define LIBBINK2_ENC_VIDEO_H

#include "bink2_enc_internal.h"

/**
 * Encode one all-intra KB2j frame payload. `frame` supplies the source
 * planes and receives the reconstruction; `state` carries the predictor
 * state and the padded reconstruction planes. `slices` and `heights` come
 * from the container flags and must match bink2_video_slice_heights().
 */
int bink2_enc_encode_intra_frame(Bink2BitWriter *bw, Bink2Frame *frame,
                                 Bink2Video *state, int q, int slices,
                                 const int *heights);
int bink2_enc_encode_frame(Bink2BitWriter *bw, Bink2Frame *frame,
                           Bink2Video *state, int q, int slices,
                           const int *heights, int keyframe, int range,
                           int four_mv, Bink2EncStats *stats);
void bink2_enc_reset_frame(Bink2Video *state);

#endif
