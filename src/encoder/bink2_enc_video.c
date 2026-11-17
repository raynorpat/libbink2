#include "bink2_enc_video.h"
#include "bink2_dsp.h"
#include "bink2_enc_dct.h"
#include "bink2_enc_motion.h"
#include "bink2_enc_syntax.h"
#include "bink2_tables.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_FLAGS_INTRA 0x76018UL
#define FRAME_FLAGS_INTER 0x6e018UL

static int clip_int(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

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

static void update_intra_q(Bink2Video *v, int8_t *q, int dq, int flags)
{
    int mb = v->old_mb_pos;

    if ((flags & 0x20) && (flags & 0x80)) {
        *q = (int8_t)(16 + dq);
    } else if (flags & 0x80) {
        *q = (int8_t)(v->new_q_cur[mb - 1].intra_q + dq);
    } else if (flags & 0x20) {
        *q = (int8_t)(v->new_q_prev[mb].intra_q + dq);
    } else {
        *q = (int8_t)(mid_pred(v->new_q_prev[mb].intra_q,
                               v->new_q_cur[mb - 1].intra_q,
                               v->new_q_prev[mb - 1].intra_q) + dq);
    }
}

static void update_inter_q(Bink2Video *v, int8_t *q, int dq, int flags)
{
    int mb = v->old_mb_pos;

    if ((flags & 0x20) && (flags & 0x80)) {
        *q = (int8_t)(16 + dq);
    } else if (flags & 0x80) {
        *q = (int8_t)(v->new_q_cur[mb - 1].inter_q + dq);
    } else if (flags & 0x20) {
        *q = (int8_t)(v->new_q_prev[mb].inter_q + dq);
    } else {
        *q = (int8_t)(mid_pred(v->new_q_prev[mb].inter_q,
                               v->new_q_cur[mb - 1].inter_q,
                               v->new_q_prev[mb - 1].inter_q) + dq);
    }
}

static int predict_intra_q(Bink2Video *v, int flags)
{
    int8_t predicted;

    update_intra_q(v, &predicted, 0, flags);
    return predicted;
}

static int predict_inter_q(Bink2Video *v, int flags)
{
    int8_t predicted;

    update_inter_q(v, &predicted, 0, flags);
    return predicted;
}

static int quantise_dc(int desired, int prediction, int q)
{
    int pat = bink2g_dc_pat[q < 8 ? 8 : q];
    int raw = desired - prediction;
    int cnt;
    int64_t value;

    if (pat <= 0)
        return 0;
    cnt = (int)(((int64_t)raw * 1024 +
                 (raw >= 0 ? pat / 2 : -pat / 2)) / pat);
    value = (int64_t)cnt * pat;
    /* Match the decoder's (value + 0x200) >> 10, including negatives. */
    if (value >= 0)
        return (int)((value + 0x200) >> 10);
    return -(int)(((-value) + 0x200) >> 10);
}

static void encode_dc_block(Bink2Video *v, int is_luma, int comp, int count,
                            int mindc, int maxdc, int flags,
                            const int32_t desired[16], int q, int32_t tdc[16],
                            int dc_actual[16])
{
    float tdcf[16] = { 0 };
    int mb = v->old_mb_pos;
    int i;

    v->old_comp = comp;
    for (i = 0; i < count; i++) {
        int prediction;

        bink2_video_predict_dc(v, is_luma, mindc, maxdc, flags, tdcf);
        prediction = (int)lround(v->old_dc_cur[mb].dc[comp][i]);
        tdc[i] = quantise_dc(desired[i], prediction, q);
        tdcf[i] = (float)tdc[i];
    }
    bink2_video_predict_dc(v, is_luma, mindc, maxdc, flags, tdcf);
    for (i = 0; i < count; i++)
        dc_actual[i] = (int)lround(v->old_dc_cur[mb].dc[comp][i]);
}

static void fdct_from_plane(const unsigned char *plane, int stride, int width,
                            int height, int x, int y, float coeff[64])
{
    unsigned char block[64];
    int by, bx;

    for (by = 0; by < 8; by++) {
        int sy = y + by;

        if (sy >= height)
            sy = height - 1;
        for (bx = 0; bx < 8; bx++) {
            int sx = x + bx;

            if (sx >= width)
                sx = width - 1;
            block[by * 8 + bx] = plane[sy * stride + sx];
        }
    }
    bink2_fdct_block(block, 8, coeff);
}

static void clamp_levels(int32_t levels[64])
{
    int i;

    for (i = 0; i < 64; i++) {
        if (levels[i] > 1025)
            levels[i] = 1025;
        else if (levels[i] < -1025)
            levels[i] = -1025;
    }
}

static void reconstruct_block(Bink2Video *v, int plane, int x, int y,
                              const int32_t levels[64],
                              const uint16_t qmat[4][64], int q, int dc)
{
    int32_t block[64];

    bink2_dequantise(levels, qmat, q, block);
    block[0] = dc * 8 + 32;
    bink2_idct_new(block);
    bink2_put_block(v->cur[plane] + (size_t)y * v->stride[plane] + x,
                    v->stride[plane], block);
}

static void put_ones(Bink2BitWriter *bw, int count)
{
    while (count > 0) {
        int n = count > 32 ? 32 : count;
        unsigned value = n == 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);

        bink2_bw_put(bw, value, n);
        count -= n;
    }
}

static void put_le32(unsigned char *p, unsigned long value)
{
    p[0] = (unsigned char)(value & 0xFF);
    p[1] = (unsigned char)((value >> 8) & 0xFF);
    p[2] = (unsigned char)((value >> 16) & 0xFF);
    p[3] = (unsigned char)((value >> 24) & 0xFF);
}

static int encode_luma(Bink2BitWriter *bw, Bink2Video *v,
                       const Bink2Frame *src, int x, int y, int flags, int q,
                       uint32_t *prev_cbp)
{
    float coeff[64];
    int32_t levels[16][64];
    int32_t desired[16];
    int32_t tdc[16];
    int dc_actual[16];
    int mb = x / 32;
    int idx;

    for (idx = 0; idx < 16; idx++) {
        int pos = bink2_luma_repos[idx];
        int bx = (pos & 3) * 8;
        int by = (pos >> 2) * 8;

        fdct_from_plane(src->y, src->stride[0], src->width, src->height,
                        x + bx, y + by, coeff);
        desired[idx] = clip_int((int)lround((coeff[0] - 32) / 8.0), 0, 2047);
        coeff[0] = 0.0f;
        bink2_quantise(coeff, bink2g_luma_intra_qmat, q, 0, levels[idx]);
        clamp_levels(levels[idx]);
    }

    v->old_mb_pos = mb;
    encode_dc_block(v, 1, 0, 16, 0, 2047, flags, desired, q, tdc,
                    dc_actual);

    bink2_write_cbp_luma(bw, *prev_cbp,
                         (uint32_t)(v->frame_flags & 0x40000), 0xFFFF);
    *prev_cbp = 0xFFFF;
    bink2_write_dcs(bw, tdc, 16, q);

    for (idx = 0; idx < 16; idx++) {
        bink2_write_acs(bw, levels[idx], bink2g_luma_intra_qmat, q, 1);
    }

    for (idx = 0; idx < 16; idx++) {
        int pos = bink2_luma_repos[idx];
        int bx = (pos & 3) * 8;
        int by = (pos >> 2) * 8;

        reconstruct_block(v, 0, x + bx, y + by, levels[idx],
                          bink2g_luma_intra_qmat, q, dc_actual[idx]);
    }
    return 0;
}

static int encode_chroma(Bink2BitWriter *bw, Bink2Video *v,
                         const Bink2Frame *src, int plane, int comp, int x,
                         int y, int flags, int q, uint32_t *prev_cbp)
{
    const unsigned char *sp = plane == 2 ? src->v : src->u;
    int sstride = src->stride[plane];
    int cw = (src->width + 1) / 2;
    int ch = (src->height + 1) / 2;
    int cx = x / 2;
    int cy = y / 2;
    int mb = x / 32;
    float coeff[64];
    int32_t levels[4][64];
    int32_t desired[16] = { 0 };
    int32_t tdc[4];
    int dc_actual[4];
    int idx;

    for (idx = 0; idx < 4; idx++) {
        int bx = (idx & 1) * 8;
        int by = (idx >> 1) * 8;

        fdct_from_plane(sp, sstride, cw, ch, cx + bx, cy + by, coeff);
        desired[idx] = clip_int((int)lround((coeff[0] - 32) / 8.0), 0, 2047);
        coeff[0] = 0.0f;
        bink2_quantise(coeff, bink2g_chroma_intra_qmat, q, 0, levels[idx]);
        clamp_levels(levels[idx]);
    }

    v->old_mb_pos = mb;
    encode_dc_block(v, 0, comp, 4, 0, 2047, flags, desired, q, tdc,
                    dc_actual);

    bink2_write_cbp_chroma(bw, *prev_cbp, 0xF);
    *prev_cbp = 0xF;
    bink2_write_dcs(bw, tdc, 4, q);

    for (idx = 0; idx < 4; idx++)
        bink2_write_acs(bw, levels[idx], bink2g_chroma_intra_qmat, q, 1);

    for (idx = 0; idx < 4; idx++) {
        int bx = (idx & 1) * 8;
        int by = (idx >> 1) * 8;

        reconstruct_block(v, plane, cx + bx, cy + by, levels[idx],
                          bink2g_chroma_intra_qmat, q, dc_actual[idx]);
    }
    return 0;
}

static int encode_slice(Bink2BitWriter *bw, Bink2Video *v, Bink2Frame *src,
                        int y0, int y1, int q)
{
    int y, x;

    memset(v->new_q_prev, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2Quant));
    memset(v->old_mv_prev, 0,
           ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

    for (y = y0; y < y1; y += 32) {
        uint32_t prev_y = 0, prev_u = 0, prev_v = 0;

        memset(v->new_q_cur, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2Quant));
        memset(v->old_mv_cur, 0,
               ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

        for (x = 0; x < src->width; x += 32) {
            int flags = 0;
            int mb = x / 32;
            int predicted;
            int dq;

            if (y == y0)
                flags |= 0x80;
            if (x == 0)
                flags |= 0x20;
            if (x == 32)
                flags |= 0x200;
            if (x + 32 >= src->width)
                flags |= 0x40;

            v->old_mb_pos = mb;
            predicted = predict_intra_q(v, flags);
            dq = q - predicted;
            update_intra_q(v, &v->new_q_cur[mb].intra_q, dq, flags);
            bink2_write_quant(bw, predicted, q);

            encode_luma(bw, v, src, x, y, flags, q, &prev_y);
            encode_chroma(bw, v, src, 2, 1, x, y, flags, q, &prev_v);
            encode_chroma(bw, v, src, 1, 2, x, y, flags, q, &prev_u);
        }

        {
            Bink2OldDc *dc_swap = v->old_dc_cur;
            Bink2Quant *q_swap = v->new_q_cur;
            int32_t *mv_swap = v->old_mv_cur;

            v->old_dc_cur = v->old_dc_prev;
            v->old_dc_prev = dc_swap;
            v->new_q_cur = v->new_q_prev;
            v->new_q_prev = q_swap;
            v->old_mv_cur = v->old_mv_prev;
            v->old_mv_prev = mv_swap;
        }
    }
    return 0;
}

static void copy_reconstruction(Bink2Frame *frame, const Bink2Video *v)
{
    int cw = (frame->width + 1) / 2;
    int ch = (frame->height + 1) / 2;
    int y;

    for (y = 0; y < frame->height; y++)
        memcpy(frame->y + (size_t)y * frame->stride[0],
               v->cur[0] + (size_t)y * v->stride[0],
               (size_t)frame->width);
    for (y = 0; y < ch; y++) {
        memcpy(frame->u + (size_t)y * frame->stride[1],
               v->cur[1] + (size_t)y * v->stride[1], (size_t)cw);
        memcpy(frame->v + (size_t)y * frame->stride[2],
               v->cur[2] + (size_t)y * v->stride[2], (size_t)cw);
    }
}

void bink2_enc_reset_frame(Bink2Video *v)
{
    int i;

    for (i = 0; i < 4; i++) {
        unsigned long h = (i == 1 || i == 2) ? v->aligned_h / 2
                                             : v->aligned_h;

        if (v->cur[i] != NULL)
            memset(v->cur[i], 0, (size_t)v->stride[i] * h);
    }
    memset(v->new_q_cur, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2Quant));
    memset(v->new_q_prev, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2Quant));
    memset(v->old_mv_cur, 0, ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));
    memset(v->old_mv_prev, 0, ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));
    v->rgb_valid = 0;
    v->old_mb_pos = 0;
    v->old_comp = 0;
}

static void update_reference(Bink2Video *v)
{
    int i;

    for (i = 0; i < 4; i++) {
        unsigned long h = (i == 1 || i == 2) ? v->aligned_h / 2
                                             : v->aligned_h;

        if (v->cur[i] != NULL && v->ref[i] != NULL)
            memcpy(v->ref[i], v->cur[i], (size_t)v->stride[i] * h);
    }
    v->has_prev = 1;
}

static int block_sad(const Bink2Frame *src, const Bink2Video *v, int x,
                     int y)
{
    int sad = 0;
    int bx, by;

    for (by = 0; by < 32 && y + by < src->height; by++)
        for (bx = 0; bx < 32 && x + bx < src->width; bx++)
            sad += abs((int)src->y[(size_t)(y + by) * src->stride[0] + x +
                                   bx] -
                       (int)v->cur[0][(size_t)(y + by) * v->stride[0] + x +
                                      bx]);
    return sad;
}

static void residual_coeffs(const Bink2Frame *src, const Bink2Video *v,
                            int plane, int x, int y, float coeff[64])
{
    float predicted[64];
    const unsigned char *sp;
    int stride, w, h;
    int i;

    if (plane == 0) {
        sp = src->y;
        stride = src->stride[0];
        w = src->width;
        h = src->height;
    } else if (plane == 1) {
        sp = src->u;
        stride = src->stride[1];
        w = (src->width + 1) / 2;
        h = (src->height + 1) / 2;
    } else {
        sp = src->v;
        stride = src->stride[2];
        w = (src->width + 1) / 2;
        h = (src->height + 1) / 2;
    }
    fdct_from_plane(sp, stride, w, h, x, y, coeff);
    fdct_from_plane(v->cur[plane], v->stride[plane], v->plane_w[plane],
                    v->plane_h[plane], x, y, predicted);
    for (i = 0; i < 64; i++)
        coeff[i] -= predicted[i];
}

static int encode_residual_luma(Bink2BitWriter *bw, Bink2Video *v,
                                const Bink2Frame *src, int x, int y, int q,
                                uint32_t *prev_cbp)
{
    float coeff[64];
    int32_t levels[16][64];
    int32_t desired[16];
    int32_t tdc[16];
    int dc_actual[16];
    int mb = x / 32;
    int idx;

    for (idx = 0; idx < 16; idx++) {
        int pos = bink2_luma_repos[idx];
        int bx = (pos & 3) * 8;
        int by = (pos >> 2) * 8;

        residual_coeffs(src, v, 0, x + bx, y + by, coeff);
        desired[idx] = clip_int((int)lround((coeff[0] - 32) / 8.0), -1023,
                                1023);
        coeff[0] = 0.0f;
        bink2_quantise(coeff, bink2g_inter_qmat, q, 0, levels[idx]);
        clamp_levels(levels[idx]);
    }

    v->old_mb_pos = mb;
    encode_dc_block(v, 1, 0, 16, -1023, 1023, 0xA8, desired, q, tdc,
                    dc_actual);

    bink2_write_cbp_luma(bw, *prev_cbp,
                         (uint32_t)(v->frame_flags & 0x40000), 0xFFFF);
    *prev_cbp = 0xFFFF;
    bink2_write_dcs(bw, tdc, 16, q);
    for (idx = 0; idx < 16; idx++)
        bink2_write_acs(bw, levels[idx], bink2g_inter_qmat, q, 1);

    for (idx = 0; idx < 16; idx++) {
        int pos = bink2_luma_repos[idx];
        int bx = (pos & 3) * 8;
        int by = (pos >> 2) * 8;
        int32_t block[64];

        bink2_dequantise(levels[idx], bink2g_inter_qmat, q, block);
        block[0] = dc_actual[idx] * 8 + 32;
        bink2_idct_new(block);
        bink2_add_block(v->cur[0] + (size_t)(y + by) * v->stride[0] + x +
                        bx, v->stride[0], block);
    }
    return 0;
}

static int encode_residual_chroma(Bink2BitWriter *bw, Bink2Video *v,
                                  const Bink2Frame *src, int plane, int comp,
                                  int x, int y, int q, uint32_t *prev_cbp)
{
    int cx = x / 2;
    int cy = y / 2;
    int mb = x / 32;
    float coeff[64];
    int32_t levels[4][64];
    int32_t desired[16] = { 0 };
    int32_t tdc[4];
    int dc_actual[4];
    int idx;

    for (idx = 0; idx < 4; idx++) {
        int bx = (idx & 1) * 8;
        int by = (idx >> 1) * 8;

        residual_coeffs(src, v, plane, cx + bx, cy + by, coeff);
        desired[idx] = clip_int((int)lround((coeff[0] - 32) / 8.0), -1023,
                                1023);
        coeff[0] = 0.0f;
        bink2_quantise(coeff, bink2g_inter_qmat, q, 0, levels[idx]);
        clamp_levels(levels[idx]);
    }

    v->old_mb_pos = mb;
    encode_dc_block(v, 0, comp, 4, -1023, 1023, 0xA8, desired, q, tdc,
                    dc_actual);

    bink2_write_cbp_chroma(bw, *prev_cbp, 0xF);
    *prev_cbp = 0xF;
    bink2_write_dcs(bw, tdc, 4, q);
    for (idx = 0; idx < 4; idx++)
        bink2_write_acs(bw, levels[idx], bink2g_inter_qmat, q, 1);

    for (idx = 0; idx < 4; idx++) {
        int bx = (idx & 1) * 8;
        int by = (idx >> 1) * 8;
        int32_t block[64];

        bink2_dequantise(levels[idx], bink2g_inter_qmat, q, block);
        block[0] = dc_actual[idx] * 8 + 32;
        bink2_idct_new(block);
        bink2_add_block(v->cur[plane] + (size_t)(cy + by) * v->stride[plane] +
                        cx + bx, v->stride[plane], block);
    }
    return 0;
}

static int encode_inter_slice(Bink2BitWriter *bw, Bink2Video *v,
                              const Bink2Frame *src, const Bink2Frame *ref,
                              int y0, int y1, int range, int four_mv,
                              int q)
{
    int y, x;

    memset(v->new_q_prev, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2Quant));
    memset(v->old_mv_prev, 0,
           ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

    for (y = y0; y < y1; y += 32) {
        uint8_t lru[4] = { 2, 3, 1, 0 };
        uint32_t prev_y = 0, prev_u = 0, prev_v = 0;

        memset(v->new_q_cur, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2Quant));
        memset(v->old_mv_cur, 0,
               ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

        for (x = 0; x < src->width; x += 32) {
            int flags = 0;
            int mb = x / 32;
            Bink2Mv mv[4];
            int nb = 1;
            int type;
            int raw[4][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
            int predicted[4][2];
            int sad;

            if (y == y0)
                flags |= 0x80;
            if (x == 0)
                flags |= 0x20;
            if (x == 32)
                flags |= 0x200;
            if (x + 32 >= src->width)
                flags |= 0x40;

            v->old_mb_pos = mb;
            bink2_me_search(src, ref, x, y, range, four_mv, mv, &nb);
            if (mv[0].x == 0 && mv[0].y == 0) {
                type = 1;
                bink2_write_btype(bw, lru, type);
                update_inter_q(v, &v->new_q_cur[mb].inter_q, 0, flags);
                update_intra_q(v, &v->new_q_cur[mb].intra_q, 0, flags);
                bink2_video_copy_mb(v, x, y);
                bink2_enc_count_block(type);
                continue;
            }

            bink2_enc_predict_mv(v, flags, raw, nb, predicted);
            raw[0][0] = mv[0].x - predicted[0][0];
            raw[0][1] = mv[0].y - predicted[0][1];
            bink2_enc_predict_mv(v, flags, raw, nb, predicted);
            bink2_video_mc_luma(v, x, y);
            bink2_video_mc_chroma(v, x, y);
            sad = block_sad(src, v, x, y);
            update_intra_q(v, &v->new_q_cur[mb].intra_q, 0, flags);

            if (sad <= 32 * 32 * 2) {
                type = 2;
                bink2_write_btype(bw, lru, type);
                update_inter_q(v, &v->new_q_cur[mb].inter_q, 0, flags);
                bink2_write_mv1(bw, raw[0][0], raw[0][1]);
            } else {
                int predicted_q = predict_inter_q(v, flags);
                int dq = q - predicted_q;

                type = 3;
                bink2_write_btype(bw, lru, type);
                update_inter_q(v, &v->new_q_cur[mb].inter_q, dq, flags);
                bink2_write_mv1(bw, raw[0][0], raw[0][1]);
                bink2_write_quant(bw, predicted_q, q);
                encode_residual_luma(bw, v, src, x, y, q, &prev_y);
                bink2_bw_put(bw, 1, 1);
                encode_residual_chroma(bw, v, src, 2, 1, x, y, q,
                                       &prev_v);
                encode_residual_chroma(bw, v, src, 1, 2, x, y, q,
                                       &prev_u);
            }
            bink2_video_store_motion(v, x, y, nb);
            bink2_enc_count_block(type);
        }

        {
            Bink2OldDc *dc_swap = v->old_dc_cur;
            Bink2Quant *q_swap = v->new_q_cur;
            int32_t *mv_swap = v->old_mv_cur;

            v->old_dc_cur = v->old_dc_prev;
            v->old_dc_prev = dc_swap;
            v->new_q_cur = v->new_q_prev;
            v->new_q_prev = q_swap;
            v->old_mv_cur = v->old_mv_prev;
            v->old_mv_prev = mv_swap;
        }
    }
    return 0;
}

static void frame_header(Bink2BitWriter *bw, int keyframe, int height,
                         int slices)
{
    unsigned long flags = keyframe ? FRAME_FLAGS_INTRA : FRAME_FLAGS_INTER;
    int row_size;
    int s;

    bink2_bw_put(bw, flags, 32);
    for (s = 1; s < slices; s++)
        bink2_bw_put(bw, 0, 32);
    if (!keyframe)
        return;
    row_size = (int)((((height + 15) & ~15UL) >> 3) - 1);
    bink2_bw_put(bw, 0, 1);
    put_ones(bw, row_size);
}

int bink2_enc_encode_frame(Bink2BitWriter *bw, Bink2Frame *frame,
                           Bink2Video *v, int q, int slices,
                           const int *heights, int keyframe, int range,
                           int four_mv)
{
    unsigned long offsets[8] = { 0 };
    Bink2Frame ref_view;
    int s;

    v->frame_flags = keyframe ? FRAME_FLAGS_INTRA : FRAME_FLAGS_INTER;
    ref_view.y = v->ref[0];
    ref_view.u = v->ref[1];
    ref_view.v = v->ref[2];
    ref_view.stride[0] = v->stride[0];
    ref_view.stride[1] = v->stride[1];
    ref_view.stride[2] = v->stride[2];
    ref_view.width = frame->width;
    ref_view.height = frame->height;

    frame_header(bw, keyframe, frame->height, slices);
    for (s = 0; s < slices; s++) {
        int y0 = s == 0 ? 0 : heights[s - 1];
        int y1 = heights[s];

        if (s > 0) {
            bink2_bw_align32(bw);
            offsets[s] = bink2_bw_bitpos(bw) / 8;
        }
        if (keyframe)
            encode_slice(bw, v, frame, y0, y1, q);
        else
            encode_inter_slice(bw, v, frame, &ref_view, y0, y1, range,
                               four_mv, q);
    }
    bink2_bw_align32(bw);
    for (s = 1; s < slices; s++)
        put_le32(bw->data + 4 + 4 * (s - 1), offsets[s]);
    copy_reconstruction(frame, v);
    update_reference(v);
    return 0;
}

int bink2_enc_encode_intra_frame(Bink2BitWriter *bw, Bink2Frame *frame,
                                 Bink2Video *v, int q, int slices,
                                 const int *heights)
{
    return bink2_enc_encode_frame(bw, frame, v, q, slices, heights, 1, 0, 0);
}

int bink2_frame_alloc(Bink2Frame *frame, int width, int height)
{
    int cw = (width + 1) / 2;
    int ch = (height + 1) / 2;

    if (frame == NULL || width < 1 || height < 1)
        return -1;
    memset(frame, 0, sizeof(*frame));
    frame->width = width;
    frame->height = height;
    frame->stride[0] = width;
    frame->stride[1] = cw;
    frame->stride[2] = cw;
    frame->y = (unsigned char *)calloc(1, (size_t)width * height);
    frame->u = (unsigned char *)calloc(1, (size_t)cw * ch);
    frame->v = (unsigned char *)calloc(1, (size_t)cw * ch);
    if (frame->y == NULL || frame->u == NULL || frame->v == NULL) {
        bink2_frame_free(frame);
        return -1;
    }
    return 0;
}

void bink2_frame_free(Bink2Frame *frame)
{
    if (frame == NULL)
        return;
    free(frame->y);
    free(frame->u);
    free(frame->v);
    memset(frame, 0, sizeof(*frame));
}

static unsigned char clip8(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return (unsigned char)value;
}

static int convert_rgb(const unsigned char *rgb, int width, int height,
                       Bink2Frame *frame)
{
    int cw = (width + 1) / 2;
    int ch = (height + 1) / 2;
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const unsigned char *p = rgb + ((size_t)y * width + x) * 3;
            int r = p[0], g = p[1], b = p[2];

            frame->y[(size_t)y * frame->stride[0] + x] =
                clip8((int)lround(0.299 * r + 0.587 * g + 0.114 * b));
        }
    }
    for (y = 0; y < ch; y++) {
        for (x = 0; x < cw; x++) {
            int sx = x * 2;
            int sy = y * 2;
            int ux2 = sx + 1 < width ? sx + 1 : sx;
            int uy2 = sy + 1 < height ? sy + 1 : sy;
            const unsigned char *p00 = rgb + ((size_t)sy * width + sx) * 3;
            const unsigned char *p10 = rgb + ((size_t)sy * width + ux2) * 3;
            const unsigned char *p01 = rgb + ((size_t)uy2 * width + sx) * 3;
            const unsigned char *p11 = rgb + ((size_t)uy2 * width + ux2) * 3;
            double r = (p00[0] + p10[0] + p01[0] + p11[0]) / 4.0;
            double g = (p00[1] + p10[1] + p01[1] + p11[1]) / 4.0;
            double b = (p00[2] + p10[2] + p01[2] + p11[2]) / 4.0;

            frame->u[(size_t)y * frame->stride[1] + x] =
                clip8((int)lround(-0.168736 * r - 0.331264 * g + 0.5 * b +
                                  128.0));
            frame->v[(size_t)y * frame->stride[2] + x] =
                clip8((int)lround(0.5 * r - 0.418688 * g - 0.081312 * b +
                                  128.0));
        }
    }
    return 0;
}

int bink2_enc_read_frame(const bink2_enc_source *source, unsigned long index,
                         Bink2Frame *frame)
{
    if (source == NULL || frame == NULL)
        return -1;
    if (source->read_frame_yuv != NULL) {
        if (source->read_frame_yuv(source->user, index, frame->y, frame->u,
                                   frame->v) != 0)
            return -1;
        return 0;
    }
    if (source->read_frame != NULL) {
        size_t size = (size_t)source->width * (size_t)source->height * 3;
        unsigned char *rgb = (unsigned char *)malloc(size);

        if (rgb == NULL)
            return -1;
        if (source->read_frame(source->user, index, rgb) != 0) {
            free(rgb);
            return -1;
        }
        convert_rgb(rgb, source->width, source->height, frame);
        free(rgb);
        return 0;
    }
    return -1;
}
