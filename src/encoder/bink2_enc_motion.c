#include "bink2_enc_motion.h"
#include "bink2_dsp.h"

#include <limits.h>
#include <stdlib.h>

static Bink2EncStats last_stats;

const Bink2EncStats *bink2_enc_last_stats(void)
{
    return &last_stats;
}

void bink2_enc_reset_stats(void)
{
    last_stats.intra = 0;
    last_stats.skip = 0;
    last_stats.motion = 0;
    last_stats.residual = 0;
}

void bink2_enc_count_block(int type)
{
    switch (type) {
    case 0:
        last_stats.intra++;
        break;
    case 1:
        last_stats.skip++;
        break;
    case 2:
        last_stats.motion++;
        break;
    default:
        last_stats.residual++;
        break;
    }
}

#define MV(arr, mb, k, c) ((arr)[(mb) * 8 + (k) * 2 + (c)])

static int mid_pred(int a, int b, int c)
{
    if (a < b) {
        if (b < c)
            return b;
        if (a < c)
            return c;
        return a;
    }
    if (b < c) {
        if (a < c)
            return a;
        return c;
    }
    return b;
}

void bink2_enc_predict_mv(Bink2Video *v, int flags, const int raw[4][2],
                          int nb, int out[4][2])
{
    int mb = v->old_mb_pos;
    int lm = mb > 0 ? mb - 1 : 0;
    int32_t *c = v->old_mv_cur;
    int32_t *l = v->old_mv_cur;
    int32_t *lt = v->old_mv_prev;
    int32_t *t = v->old_mv_prev;
    int vx, vy, k;

    (void)nb;
    vx = raw[0][0];
    vy = raw[0][1];

    if (flags & 0x80) {
        if (!(flags & 0x20)) {
            vx += mid_pred(MV(l, lm, 0, 0), MV(l, lm, 1, 0),
                           MV(l, lm, 3, 0));
            vy += mid_pred(MV(l, lm, 0, 1), MV(l, lm, 1, 1),
                           MV(l, lm, 3, 1));
        }
    } else {
        if (!(flags & 0x20)) {
            vx += mid_pred(MV(lt, lm, 3, 0), MV(t, mb, 2, 0),
                           MV(l, lm, 1, 0));
            vy += mid_pred(MV(lt, lm, 3, 1), MV(t, mb, 2, 1),
                           MV(l, lm, 1, 1));
        } else {
            vx += mid_pred(MV(t, mb, 0, 0), MV(t, mb, 2, 0),
                           MV(t, mb, 3, 0));
            vy += mid_pred(MV(t, mb, 0, 1), MV(t, mb, 2, 1),
                           MV(t, mb, 3, 1));
        }
    }

    for (k = 0; k < 4; k++) {
        MV(c, mb, k, 0) = vx;
        MV(c, mb, k, 1) = vy;
        out[k][0] = vx;
        out[k][1] = vy;
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

void bink2_me_search(const Bink2Frame *src, const Bink2Frame *ref, int mbx,
                     int mby, int range, int four_mv, Bink2Mv out[4],
                     int *nb)
{
    int best_sad = INT_MAX;
    int best_x = 0, best_y = 0;
    int dx, dy, ox, oy, k;

    (void)four_mv;
    if (range < 0)
        range = 0;
    for (dy = -range; dy <= range; dy++) {
        for (dx = -range; dx <= range; dx++) {
            int sad = bink2_me_sad(src, ref, mbx, mby, dx * 2, dy * 2);

            if (sad < best_sad) {
                best_sad = sad;
                best_x = dx * 2;
                best_y = dy * 2;
            }
        }
    }
    for (oy = -1; oy <= 1; oy++) {
        for (ox = -1; ox <= 1; ox++) {
            int sad = bink2_me_sad(src, ref, mbx, mby, best_x + ox,
                                 best_y + oy);

            if (sad < best_sad) {
                best_sad = sad;
                best_x += ox;
                best_y += oy;
            }
        }
    }
    for (k = 0; k < 4; k++) {
        out[k].x = best_x;
        out[k].y = best_y;
    }
    if (nb != NULL)
        *nb = 1;
}
