#include "bink2_enc_motion.h"
#include "bink2_dsp.h"
#include "bink2_video_new.h"

#include <limits.h>
#include <stdlib.h>

#define MV(arr, mb, k, c) ((arr)[(mb) * 8 + (k) * 2 + (c)])

void bink2_enc_predict_mv(Bink2Video *v, int flags, const int raw[4][2],
                          int nb, int out[4][2])
{
    int mb = v->slice[0].mb_pos;
    int k;

    /* The decoder's predictor, so both sides agree. */
    bink2_new_predict_mv(&v->slice[0], flags, raw, nb);
    for (k = 0; k < 4; k++) {
        out[k][0] = (int)MV(v->slice[0].mv_cur, mb, k, 0);
        out[k][1] = (int)MV(v->slice[0].mv_cur, mb, k, 1);
    }
}

int bink2_me_sad(const Bink2Frame *src, const Bink2Frame *ref, int mbx,
                 int mby, int mvx, int mvy)
{
    unsigned char pred[32 * 32];
    int qx, qy, by, bx;
    int sad = 0;

    for (qy = 0; qy < 2; qy++)
        for (qx = 0; qx < 2; qx++)
            bink2_mc_luma(pred + (qy * 16) * 32 + qx * 16, 32, ref->y,
                          ref->stride[0], mbx + qx * 16, mby + qy * 16,
                          src->width, src->height, mvx, mvy);

    for (by = 0; by < 32; by++) {
        int sy = mby + by;

        if (sy >= src->height)
            sy = src->height - 1;
        for (bx = 0; bx < 32; bx++) {
            int sx = mbx + bx;

            if (sx >= src->width)
                sx = src->width - 1;
            sad += abs((int)src->y[sy * src->stride[0] + sx] -
                       (int)pred[by * 32 + bx]);
        }
    }
    return sad;
}

void bink2_enc_invert_mv(Bink2Video *v, int flags, const Bink2Mv desired[4],
                         int nb, int raw[4][2])
{
    static const int first_row_order[4] = { 0, 2, 1, 3 };
    const int *order = ((flags & 0x80) && !(flags & 0x20))
                           ? first_row_order : NULL;
    int raws[4][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
    int predicted[4][2];
    int step;

    for (step = 0; step < nb; step++) {
        int j = order != NULL ? order[step] : step;

        bink2_enc_predict_mv(v, flags, raws, nb, predicted);
        raws[j][0] = desired[j].x - predicted[j][0];
        raws[j][1] = desired[j].y - predicted[j][1];
    }
    bink2_enc_predict_mv(v, flags, raws, nb, predicted);
    for (step = 0; step < 4; step++) {
        raw[step][0] = raws[step][0];
        raw[step][1] = raws[step][1];
    }
}

static int sad16_for_mv(const Bink2Frame *src, const Bink2Frame *ref, int x,
                        int y, int mvx, int mvy)
{
    unsigned char pred[16 * 16];
    int bx, by;
    int sad = 0;

    bink2_mc_luma(pred, 16, ref->y, ref->stride[0], x, y, src->width,
                  src->height, mvx, mvy);
    for (by = 0; by < 16; by++) {
        int sy = y + by;

        if (sy >= src->height)
            sy = src->height - 1;
        for (bx = 0; bx < 16; bx++) {
            int sx = x + bx;

            if (sx >= src->width)
                sx = src->width - 1;
            sad += abs((int)src->y[sy * src->stride[0] + sx] -
                       (int)pred[by * 16 + bx]);
        }
    }
    return sad;
}

static void search_one(const Bink2Frame *src, const Bink2Frame *ref, int x,
                       int y, int range, int four, Bink2Mv *out)
{
    int best_sad = INT_MAX;
    int best_x = 0, best_y = 0;
    int dx, dy, ox, oy;

    for (dy = -range; dy <= range; dy++) {
        for (dx = -range; dx <= range; dx++) {
            int sad = four ? sad16_for_mv(src, ref, x, y, dx * 2, dy * 2)
                           : bink2_me_sad(src, ref, x, y, dx * 2, dy * 2);

            if (sad < best_sad) {
                best_sad = sad;
                best_x = dx * 2;
                best_y = dy * 2;
            }
        }
    }
    for (oy = -1; oy <= 1; oy++) {
        for (ox = -1; ox <= 1; ox++) {
            int sad = four ? sad16_for_mv(src, ref, x, y, best_x + ox,
                                          best_y + oy)
                           : bink2_me_sad(src, ref, x, y, best_x + ox,
                                          best_y + oy);

            if (sad < best_sad) {
                best_sad = sad;
                best_x += ox;
                best_y += oy;
            }
        }
    }
    out->x = best_x;
    out->y = best_y;
}

void bink2_me_search(const Bink2Frame *src, const Bink2Frame *ref, int mbx,
                     int mby, int range, int four_mv, Bink2Mv out[4],
                     int *nb)
{
    int q;

    if (range < 0)
        range = 0;
    if (four_mv) {
        for (q = 0; q < 4; q++) {
            search_one(src, ref, mbx + (q & 1) * 16, mby + (q >> 1) * 16,
                       range, 1, &out[q]);
        }
        if (nb != NULL)
            *nb = 4;
        return;
    }
    search_one(src, ref, mbx, mby, range, 0, &out[0]);
    for (q = 1; q < 4; q++)
        out[q] = out[0];
    if (nb != NULL)
        *nb = 1;
}
