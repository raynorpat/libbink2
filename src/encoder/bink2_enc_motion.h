#ifndef LIBBINK2_ENC_MOTION_H
#define LIBBINK2_ENC_MOTION_H

#include "bink2_enc_internal.h"

typedef struct {
    int x, y;
} Bink2Mv;

void bink2_me_search(const Bink2Frame *src, const Bink2Frame *ref, int mbx,
                     int mby, int range, int four_mv, Bink2Mv out[4],
                     int *nb);
int bink2_me_sad(const Bink2Frame *src, const Bink2Frame *ref, int mbx,
                 int mby, int mvx, int mvy);

/** Replicates the decoder's KB2g+ MV predictor (one or four vectors). */
void bink2_enc_predict_mv(Bink2Video *v, int flags, const int raw[4][2],
                          int nb, int out[4][2]);
/** Invert the predictor for the desired vectors, respecting its dependency order. */
void bink2_enc_invert_mv(Bink2Video *v, int flags, const Bink2Mv desired[4],
                         int nb, int raw[4][2]);

#endif
