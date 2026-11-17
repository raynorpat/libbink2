#include "bink2_enc_video.h"
#include "bink2_dsp.h"
#include "bink2_enc_dct.h"
#include "bink2_enc_syntax.h"
#include "bink2_tables.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_FLAGS_INTRA 0x76018UL

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

static int predict_intra_q(Bink2Video *v, int flags)
{
    int8_t predicted;

    update_intra_q(v, &predicted, 0, flags);
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
    int dc_coeff[16];
    int32_t tdc[16];
    int dc_actual[16];
    float tdcf[16];
    float zeros[16] = { 0 };
    int mb = x / 32;
    int idx;

    for (idx = 0; idx < 16; idx++) {
        int pos = bink2_luma_repos[idx];
        int bx = (pos & 3) * 8;
        int by = (pos >> 2) * 8;

        fdct_from_plane(src->y, src->stride[0], src->width, src->height,
                        x + bx, y + by, coeff);
        dc_coeff[idx] = (int)lround(coeff[0]);
        coeff[0] = 0.0f;
        bink2_quantise(coeff, bink2g_luma_intra_qmat, q, 0, levels[idx]);
        clamp_levels(levels[idx]);
    }

    v->old_mb_pos = mb;
    v->old_comp = 0;
    bink2_video_predict_dc(v, 1, 0, 2047, flags, zeros);
    for (idx = 0; idx < 16; idx++) {
        int prediction = (int)lround(v->old_dc_cur[mb].dc[0][idx]);
        int desired = (int)lround((dc_coeff[idx] - 32) / 8.0);

        desired = clip_int(desired, 0, 2047);
        tdc[idx] = quantise_dc(desired, prediction, q);
        tdcf[idx] = (float)tdc[idx];
    }
    bink2_video_predict_dc(v, 1, 0, 2047, flags, tdcf);
    for (idx = 0; idx < 16; idx++)
        dc_actual[idx] = (int)lround(v->old_dc_cur[mb].dc[0][idx]);

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
    int dc_coeff[4];
    int32_t tdc[4];
    int dc_actual[4];
    float tdcf[4];
    float zeros[4] = { 0 };
    int idx;

    for (idx = 0; idx < 4; idx++) {
        int bx = (idx & 1) * 8;
        int by = (idx >> 1) * 8;

        fdct_from_plane(sp, sstride, cw, ch, cx + bx, cy + by, coeff);
        dc_coeff[idx] = (int)lround(coeff[0]);
        coeff[0] = 0.0f;
        bink2_quantise(coeff, bink2g_chroma_intra_qmat, q, 0, levels[idx]);
        clamp_levels(levels[idx]);
    }

    v->old_mb_pos = mb;
    v->old_comp = comp;
    bink2_video_predict_dc(v, 0, 0, 2047, flags, zeros);
    for (idx = 0; idx < 4; idx++) {
        int prediction = (int)lround(v->old_dc_cur[mb].dc[comp][idx]);
        int desired = (int)lround((dc_coeff[idx] - 32) / 8.0);

        desired = clip_int(desired, 0, 2047);
        tdc[idx] = quantise_dc(desired, prediction, q);
        tdcf[idx] = (float)tdc[idx];
    }
    bink2_video_predict_dc(v, 0, 0, 2047, flags, tdcf);
    for (idx = 0; idx < 4; idx++)
        dc_actual[idx] = (int)lround(v->old_dc_cur[mb].dc[comp][idx]);

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

int bink2_enc_encode_intra_frame(Bink2BitWriter *bw, Bink2Frame *frame,
                                 Bink2Video *v, int q, int slices,
                                 const int *heights)
{
    unsigned long offsets[8] = { 0 };
    int row_size;
    int s;

    v->frame_flags = FRAME_FLAGS_INTRA;
    bink2_bw_put(bw, FRAME_FLAGS_INTRA, 32);
    for (s = 1; s < slices; s++)
        bink2_bw_put(bw, 0, 32); /* slice offsets, patched below */

    row_size = (int)((((frame->height + 15) & ~15UL) >> 3) - 1);
    bink2_bw_put(bw, 0, 1);
    put_ones(bw, row_size);

    for (s = 0; s < slices; s++) {
        int y0 = s == 0 ? 0 : heights[s - 1];
        int y1 = heights[s];

        if (s > 0) {
            bink2_bw_align32(bw);
            offsets[s] = bink2_bw_bitpos(bw) / 8;
        }
        encode_slice(bw, v, frame, y0, y1, q);
    }
    bink2_bw_align32(bw);

    for (s = 1; s < slices; s++)
        put_le32(bw->data + 4 + 4 * (s - 1), offsets[s]);
    copy_reconstruction(frame, v);
    return 0;
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
