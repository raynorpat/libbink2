#include "bink2_tables.h"
#include "bink2_video_new.h"
#include "bink2_video.h"
#include "bink2_dsp.h"

#include <string.h>

#define NUM_AC_SKIPS 14

static const uint8_t new_skips[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 64, 0, 0, 0,
};
static const uint8_t new_chroma_cbp_pat[16] = {
    0x00, 0x00, 0x00, 0x0F,
    0x00, 0x0F, 0x0F, 0x0F,
    0x00, 0x0F, 0x0F, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F,
};


static int new_mid_pred(int a, int b, int c)
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

static int popcount16(unsigned value)
{
    int count = 0;

    value &= 0xFFFF;
    while (value) {
        count += (int)(value & 1);
        value >>= 1;
    }
    return count;
}

static int new_read_vlc(Bink2Bits *bits, const uint16_t *codes,
                        const uint8_t *lengths, int count)
{
    unsigned code = 0;
    int len, i;

    for (len = 1; len <= 12; len++) {
        int bit = bink2_bits_read1(bits);

        if (bit < 0)
            return -1;
        code |= (unsigned)bit << (len - 1);
        for (i = 0; i < count; i++) {
            if (lengths[i] == len && codes[i] == code)
                return i;
        }
    }
    return -1;
}

static int new_decode_dq(Bink2Bits *bits)
{
    return bink2_new_read_quant(bits, 0);
}

static void mark_failed(Bink2Bits *bits)
{
    bits->bitpos = (size_t)-1;
}

int bink2_new_read_quant(Bink2Bits *bits, int prev_q)
{
    int dq = bink2_bits_read_unary(bits, 1, 4);

    if (dq < 0) {
        mark_failed(bits);
        return -1;
    }
    if (dq == 3) {
        int bit = bink2_bits_read1(bits);

        if (bit < 0) {
            mark_failed(bits);
            return -1;
        }
        dq += bit;
    } else if (dq == 4) {
        int extra = bink2_bits_read(bits, 5);

        if (extra < 0) {
            mark_failed(bits);
            return -1;
        }
        dq += extra + 1;
    }
    if (dq) {
        int sign = bink2_bits_read1(bits);

        if (sign < 0) {
            mark_failed(bits);
            return -1;
        }
        if (sign)
            dq = -dq;
    }
    return prev_q + dq;
}

uint32_t bink2_new_read_cbp_luma(Bink2Bits *bits, uint32_t prev_cbp,
                                 uint32_t edge)
{
    uint32_t cbp = 0, mask = 0;
    int ones = popcount16(prev_cbp);
    int bit, i;

    if (ones > 7) {
        ones = 16 - ones;
        mask = 0xFFFF;
    }
    bit = bink2_bits_read1(bits);
    if (bit < 0) {
        mark_failed(bits);
        return 0;
    }
    if (bit == 0) {
        if (ones < 4) {
            for (i = 0; i < 16; i += 4) {
                bit = bink2_bits_read1(bits);
                if (bit < 0) {
                    mark_failed(bits);
                    return 0;
                }
                if (!bit) {
                    int value = bink2_bits_read(bits, 4);

                    if (value < 0) {
                        mark_failed(bits);
                        return 0;
                    }
                    cbp |= (uint32_t)value << i;
                }
            }
        } else {
            int value = bink2_bits_read(bits, 16);

            if (value < 0) {
                mark_failed(bits);
                return 0;
            }
            cbp = (uint32_t)value;
        }
    }
    cbp ^= mask;
    if (!edge || cbp) {
        bit = bink2_bits_read1(bits);
        if (bit < 0) {
            mark_failed(bits);
            return 0;
        }
        if (bit)
            cbp |= cbp << 16;
    }
    return cbp;
}

uint32_t bink2_new_read_cbp_chroma(Bink2Bits *bits, uint32_t prev_cbp)
{
    uint32_t cbp = (prev_cbp & 0xF0000u) |
                   new_chroma_cbp_pat[prev_cbp & 0xF];
    int bit = bink2_bits_read1(bits);

    if (bit < 0) {
        mark_failed(bits);
        return 0;
    }
    if (bit == 0) {
        int value = bink2_bits_read(bits, 4);

        if (value < 0) {
            mark_failed(bits);
            return 0;
        }
        cbp = (uint32_t)value;
        bit = bink2_bits_read1(bits);
        if (bit < 0) {
            mark_failed(bits);
            return 0;
        }
        if (bit)
            cbp |= cbp << 16;
    }
    return cbp;
}

void bink2_new_read_dcs(Bink2Bits *bits, int32_t dcs[16], int count, int q)
{
    int pat, i;

    q = q < 8 ? 8 : q;
    pat = bink2g_dc_pat[q];
    {
        int has_dc = bink2_bits_read1(bits);

        if (has_dc < 0) {
            mark_failed(bits);
            memset(dcs, 0, (size_t)count * sizeof(int32_t));
            return;
        }
        if (has_dc != 1) {
            memset(dcs, 0, (size_t)count * sizeof(int32_t));
            return;
        }
    }
    for (i = 0; i < count; i++) {
        int cnt = bink2_bits_read_unary(bits, 0, 12);

        if (cnt < 0) {
            mark_failed(bits);
            memset(dcs, 0, (size_t)count * sizeof(int32_t));
            return;
        }
        if (cnt > 3) {
            int extra = bink2_bits_read(bits, cnt - 3);

            if (extra < 0) {
                mark_failed(bits);
                memset(dcs, 0, (size_t)count * sizeof(int32_t));
                return;
            }
            cnt = (1 << (cnt - 3)) + extra + 2;
        }
        if (cnt) {
            int sign = bink2_bits_read1(bits);

            if (sign < 0) {
                mark_failed(bits);
                memset(dcs, 0, (size_t)count * sizeof(int32_t));
                return;
            }
            if (sign)
                cnt = -cnt;
        }
        dcs[i] = (int32_t)(((int64_t)cnt * pat + 0x200) >> 10);
    }
}

void bink2_new_read_acs(Bink2Bits *bits, int32_t levels[64],
                        const uint16_t qmat[4][64], int q, uint32_t cbp)
{
    const uint16_t *skip_codes;
    const uint8_t *skip_bits;
    int idx = 1;
    int next = 0;

    (void)qmat;
    (void)q;
    memset(levels, 0, 64 * sizeof(int32_t));
    if (!(cbp & 1))
        return;

    if (cbp & 0xFFFF0000u) {
        skip_codes = bink2g_ac_skip_codes[1];
        skip_bits = bink2g_ac_skip_bits[1];
    } else {
        skip_codes = bink2g_ac_skip_codes[0];
        skip_bits = bink2g_ac_skip_bits[0];
    }

    while (idx < 64) {
        int val;

        next--;
        if (next < 1) {
            int skip = new_read_vlc(bits, skip_codes, skip_bits, 14);

            if (skip < 0) {
                mark_failed(bits);
                return;
            }
            next = bink2_next_skips[skip];
            skip = new_skips[skip];
            if (skip == 11) {
                skip = bink2_bits_read(bits, 6);
                if (skip < 0) {
                    mark_failed(bits);
                    return;
                }
            }
            idx += skip;
            if (idx >= 64)
                break;
        }

        val = bink2_bits_read_unary(bits, 0, 12) + 1;
        if (val < 1) {
            mark_failed(bits);
            return;
        }
        if (val > 3) {
            int nbits = val - 3;
            int extra = bink2_bits_read(bits, nbits);

            if (extra < 0) {
                mark_failed(bits);
                return;
            }
            val = extra + (1 << nbits) + 2;
        }
        {
            int sign = bink2_bits_read1(bits);

            if (sign < 0) {
                mark_failed(bits);
                return;
            }
            if (sign)
                val = -val;
        }
        levels[bink2g_scan[idx]] = val;
        idx++;
    }
}

static int new_decode_dc(Bink2Video *v, Bink2Bits *bits, int is_luma,
                         int q, int mindc, int maxdc, int flags)
{
    float tdc[16];
    int32_t raw[16];
    int num_dc = is_luma ? 16 : 4;
    int i;

    bink2_new_read_dcs(bits, raw, num_dc, q);
    if (bink2_bits_failed(bits))
        return -1;
    for (i = 0; i < num_dc; i++)
        tdc[i] = (float)raw[i];
    bink2_video_predict_dc(v, is_luma, (float)mindc, (float)maxdc, flags,
                           tdc);
    return 0;
}

static int new_decode_ac(Bink2Bits *bits, int16_t block[4][64],
                         unsigned cbp, int q,
                         const uint16_t qmat[4][64])
{
    int i, j;

    for (i = 0; i < 4; i++) {
        int32_t vals[64];
        uint32_t block_cbp = (cbp & 0xFFFF0000u) | ((cbp >> i) & 1u);

        bink2_new_read_acs(bits, vals, qmat, q, block_cbp);
        if (bink2_bits_failed(bits))
            return -1;
        for (j = 0; j < 64; j++) {
            int64_t level = (int64_t)vals[j] * qmat[q & 3][j] *
                            ((int64_t)1 << (q >> 2));

            block[i][j] = (int16_t)((level + 64) >> 7);
        }
    }
    return 0;
}

static void new_idct_put(unsigned char *dst, int stride,
                         const int16_t block[64])
{
    int32_t tmp[64];
    int i;

    for (i = 0; i < 64; i++)
        tmp[i] = block[i];
    bink2_idct_new(tmp);
    bink2_put_block(dst, stride, tmp);
}

static int new_decode_intra_luma(Bink2Video *v, Bink2Bits *bits,
                                 unsigned *prev_cbp, int q,
                                 unsigned char *dst, int stride, int flags)
{
    int16_t block[4][64];
    float *dc = v->old_dc_cur[v->old_mb_pos].dc[v->old_comp];
    unsigned cbp;
    int i, j;

    cbp = bink2_new_read_cbp_luma(bits, *prev_cbp,
                                  (uint32_t)(v->frame_flags & 0x40000));
    if (bink2_bits_failed(bits))
        return -1;
    *prev_cbp = cbp;
    if (new_decode_dc(v, bits, 1, q, 0, 2047, flags) != 0) {
        return -1;
    }

    for (i = 0; i < 4; i++) {
        if (new_decode_ac(bits, block, cbp >> (4 * i), q,
                          bink2g_luma_intra_qmat) != 0) {
            return -1;
        }
        for (j = 0; j < 4; j++) {
            int pos = bink2_luma_repos[i * 4 + j];
            int off = (pos & 3) * 8 + (pos >> 2) * 8 * stride;

            block[j][0] = (int16_t)(dc[i * 4 + j] * 8.0f + 32.0f);
            new_idct_put(dst + off, stride, block[j]);
        }
    }
    return 0;
}

static int new_decode_intra_chroma(Bink2Video *v, Bink2Bits *bits,
                                   unsigned *prev_cbp, int q,
                                   unsigned char *dst, int stride, int flags)
{
    int16_t block[4][64];
    float *dc = v->old_dc_cur[v->old_mb_pos].dc[v->old_comp];
    unsigned cbp;
    int i;

    cbp = bink2_new_read_cbp_chroma(bits, *prev_cbp);
    if (bink2_bits_failed(bits))
        return -1;
    *prev_cbp = cbp;
    if (new_decode_dc(v, bits, 0, q, 0, 2047, flags) != 0)
        return -1;
    if (new_decode_ac(bits, block, cbp, q, bink2g_chroma_intra_qmat) != 0)
        return -1;

    for (i = 0; i < 4; i++) {
        int off = (i & 1) * 8 + (i >> 1) * 8 * stride;

        block[i][0] = (int16_t)(dc[i] * 8.0f + 32.0f);
        new_idct_put(dst + off, stride, block[i]);
    }
    return 0;
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
        *q = (int8_t)(new_mid_pred(v->new_q_prev[mb].intra_q,
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
        *q = (int8_t)(new_mid_pred(v->new_q_prev[mb].inter_q,
                                   v->new_q_cur[mb - 1].inter_q,
                                   v->new_q_prev[mb - 1].inter_q) + dq);
    }
}

uint8_t bink2_new_read_btype(Bink2Bits *bits, uint8_t lru[4])
{
    int mode = bink2_bits_read_unary(bits, 1, 3);
    uint8_t value;

    if (mode < 0) {
        bits->bitpos = (size_t)-1;
        return 0xFF;
    }
    switch (mode) {
    case 0:
        value = lru[0];
        break;
    case 1:
        value = lru[1];
        lru[0] ^= lru[1];
        lru[1] ^= lru[0];
        lru[0] ^= lru[1];
        break;
    case 2:
        value = lru[3];
        lru[2] ^= lru[3];
        lru[3] ^= lru[2];
        lru[2] ^= lru[3];
        break;
    default:
        value = lru[2];
        lru[1] ^= lru[2];
        lru[2] ^= lru[1];
        lru[1] ^= lru[2];
        break;
    }
    return value;
}

int bink2_new_read_mv(Bink2Bits *bits, int mv[4][2], int *nb)
{
    int num = bink2_bits_read1(bits);
    int i, j;

    if (num < 0) {
        bits->bitpos = (size_t)-1;
        return -1;
    }
    num = num ? 1 : 4;
    *nb = num;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < num; j++) {
            int val = new_read_vlc(bits, bink2g_mv_codes, bink2g_mv_bits, 16);

            if (val < 0) {
                bits->bitpos = (size_t)-1;
                return -1;
            }
            if (val >= 8 && val != 15)
                val -= 15;
            if (val == 15) {
                int nbits = bink2_bits_read_unary(bits, 1, 12) + 4;
                int raw;

                if (nbits < 0) {
                    bits->bitpos = (size_t)-1;
                    return -1;
                }
                raw = bink2_bits_read(bits, nbits);
                if (raw < 0) {
                    bits->bitpos = (size_t)-1;
                    return -1;
                }
                val = raw + (1 << nbits) - 1;
                if (val & 1)
                    val = -(val >> 1) - 1;
                else
                    val >>= 1;
            }
            mv[j][i] = val;
        }
    }
    return 0;
}

#define MV(arr, mb, k, c) ((arr)[(mb) * 8 + (k) * 2 + (c)])

static void new_predict_mv(Bink2Video *v, int flags, const int raw[4][2],
                           int nb)
{
    int mb = v->old_mb_pos;
    int lm = mb > 0 ? mb - 1 : 0;
    int32_t *c = v->old_mv_cur;
    int32_t *l = v->old_mv_cur;
    int32_t *lt = v->old_mv_prev;
    int32_t *t = v->old_mv_prev;
    int k;

    if (nb == 1) {
        int vx = raw[0][0];
        int vy = raw[0][1];

        if (flags & 0x80) {
            if (!(flags & 0x20)) {
                vx += new_mid_pred(MV(l, lm, 0, 0), MV(l, lm, 1, 0),
                                   MV(l, lm, 3, 0));
                vy += new_mid_pred(MV(l, lm, 0, 1), MV(l, lm, 1, 1),
                                   MV(l, lm, 3, 1));
            }
        } else {
            if (!(flags & 0x20)) {
                vx += new_mid_pred(MV(lt, lm, 3, 0), MV(t, mb, 2, 0),
                                   MV(l, lm, 1, 0));
                vy += new_mid_pred(MV(lt, lm, 3, 1), MV(t, mb, 2, 1),
                                   MV(l, lm, 1, 1));
            } else {
                vx += new_mid_pred(MV(t, mb, 0, 0), MV(t, mb, 2, 0),
                                   MV(t, mb, 3, 0));
                vy += new_mid_pred(MV(t, mb, 0, 1), MV(t, mb, 2, 1),
                                   MV(t, mb, 3, 1));
            }
        }
        for (k = 0; k < 4; k++) {
            MV(c, mb, k, 0) = vx;
            MV(c, mb, k, 1) = vy;
        }
        return;
    }

    if (!(flags & 0x80)) {
        if (flags & 0x20) {
            MV(c, mb, 0, 0) = raw[0][0] + new_mid_pred(MV(t, mb, 0, 0),
                                                       MV(t, mb, 2, 0),
                                                       MV(t, mb, 3, 0));
            MV(c, mb, 0, 1) = raw[0][1] + new_mid_pred(MV(t, mb, 0, 1),
                                                       MV(t, mb, 2, 1),
                                                       MV(t, mb, 3, 1));
            MV(c, mb, 1, 0) = raw[1][0] + new_mid_pred(MV(t, mb, 2, 0),
                                                       MV(t, mb, 3, 0),
                                                       MV(c, mb, 0, 0));
            MV(c, mb, 1, 1) = raw[1][1] + new_mid_pred(MV(t, mb, 2, 1),
                                                       MV(t, mb, 3, 1),
                                                       MV(c, mb, 0, 1));
            MV(c, mb, 2, 0) = raw[2][0] + new_mid_pred(MV(t, mb, 2, 0),
                                                       MV(c, mb, 0, 0),
                                                       MV(c, mb, 1, 0));
            MV(c, mb, 2, 1) = raw[2][1] + new_mid_pred(MV(t, mb, 2, 1),
                                                       MV(c, mb, 0, 1),
                                                       MV(c, mb, 1, 1));
            MV(c, mb, 3, 0) = raw[3][0] + new_mid_pred(MV(c, mb, 0, 0),
                                                       MV(c, mb, 1, 0),
                                                       MV(c, mb, 2, 0));
            MV(c, mb, 3, 1) = raw[3][1] + new_mid_pred(MV(c, mb, 0, 1),
                                                       MV(c, mb, 1, 1),
                                                       MV(c, mb, 2, 1));
        } else {
            MV(c, mb, 0, 0) = raw[0][0] + new_mid_pred(MV(t, mb, 2, 0),
                                                       MV(lt, lm, 3, 0),
                                                       MV(l, lm, 1, 0));
            MV(c, mb, 0, 1) = raw[0][1] + new_mid_pred(MV(t, mb, 2, 1),
                                                       MV(lt, lm, 3, 1),
                                                       MV(l, lm, 1, 1));
            MV(c, mb, 1, 0) = raw[1][0] + new_mid_pred(MV(t, mb, 2, 0),
                                                       MV(t, mb, 3, 0),
                                                       MV(c, mb, 0, 0));
            MV(c, mb, 1, 1) = raw[1][1] + new_mid_pred(MV(t, mb, 2, 1),
                                                       MV(t, mb, 3, 1),
                                                       MV(c, mb, 0, 1));
            MV(c, mb, 2, 0) = raw[2][0] + new_mid_pred(MV(l, lm, 1, 0),
                                                       MV(l, lm, 3, 0),
                                                       MV(c, mb, 0, 0));
            MV(c, mb, 2, 1) = raw[2][1] + new_mid_pred(MV(l, lm, 1, 1),
                                                       MV(l, lm, 3, 1),
                                                       MV(c, mb, 0, 1));
            MV(c, mb, 3, 0) = raw[3][0] + new_mid_pred(MV(c, mb, 0, 0),
                                                       MV(c, mb, 1, 0),
                                                       MV(c, mb, 2, 0));
            MV(c, mb, 3, 1) = raw[3][1] + new_mid_pred(MV(c, mb, 0, 1),
                                                       MV(c, mb, 1, 1),
                                                       MV(c, mb, 2, 1));
        }
    } else {
        if (flags & 0x20) {
            MV(c, mb, 0, 0) = raw[0][0];
            MV(c, mb, 0, 1) = raw[0][1];
            MV(c, mb, 1, 0) = raw[1][0] + MV(c, mb, 0, 0);
            MV(c, mb, 1, 1) = raw[1][1] + MV(c, mb, 0, 1);
            MV(c, mb, 2, 0) = raw[2][0] + MV(c, mb, 0, 0);
            MV(c, mb, 2, 1) = raw[2][1] + MV(c, mb, 0, 1);
            MV(c, mb, 3, 0) = raw[3][0] + new_mid_pred(MV(c, mb, 0, 0),
                                                       MV(c, mb, 1, 0),
                                                       MV(c, mb, 2, 0));
            MV(c, mb, 3, 1) = raw[3][1] + new_mid_pred(MV(c, mb, 0, 1),
                                                       MV(c, mb, 1, 1),
                                                       MV(c, mb, 2, 1));
        } else {
            MV(c, mb, 0, 0) = raw[0][0] + new_mid_pred(MV(l, lm, 0, 0),
                                                       MV(l, lm, 1, 0),
                                                       MV(l, lm, 3, 0));
            MV(c, mb, 0, 1) = raw[0][1] + new_mid_pred(MV(l, lm, 0, 1),
                                                       MV(l, lm, 1, 1),
                                                       MV(l, lm, 3, 1));
            MV(c, mb, 2, 0) = raw[2][0] + new_mid_pred(MV(l, lm, 1, 0),
                                                       MV(l, lm, 3, 0),
                                                       MV(c, mb, 0, 0));
            MV(c, mb, 2, 1) = raw[2][1] + new_mid_pred(MV(l, lm, 1, 1),
                                                       MV(l, lm, 3, 1),
                                                       MV(c, mb, 0, 1));
            MV(c, mb, 1, 0) = raw[1][0] + new_mid_pred(MV(l, lm, 1, 0),
                                                       MV(c, mb, 0, 0),
                                                       MV(c, mb, 2, 0));
            MV(c, mb, 1, 1) = raw[1][1] + new_mid_pred(MV(l, lm, 1, 1),
                                                       MV(c, mb, 0, 1),
                                                       MV(c, mb, 2, 1));
            MV(c, mb, 3, 0) = raw[3][0] + new_mid_pred(MV(c, mb, 0, 0),
                                                       MV(c, mb, 1, 0),
                                                       MV(c, mb, 2, 0));
            MV(c, mb, 3, 1) = raw[3][1] + new_mid_pred(MV(c, mb, 0, 1),
                                                       MV(c, mb, 1, 1),
                                                       MV(c, mb, 2, 1));
        }
    }
}

static int new_average_block(const unsigned char *src, int stride)
{
    int sum = 0;
    int i;

    for (i = 0; i < 8; i++) {
        int a = (src[i + 0 * stride] + src[i + 1 * stride] + 1) >> 1;
        int b = (src[i + 2 * stride] + src[i + 3 * stride] + 1) >> 1;
        int c = (src[i + 4 * stride] + src[i + 5 * stride] + 1) >> 1;
        int d = (src[i + 6 * stride] + src[i + 7 * stride] + 1) >> 1;
        int e = (a + b + 1) >> 1;
        int f = (c + d + 1) >> 1;

        sum += (e + f + 1) >> 1;
    }
    return sum;
}

static void new_average_luma(const unsigned char *src, int stride, float *dc)
{
    int i;

    for (i = 0; i < 16; i++) {
        int pos = bink2_luma_repos[i];
        int x = pos & 3;
        int y = pos >> 2;

        dc[i] = (float)new_average_block(src + x * 8 + y * 8 * stride, stride);
    }
}

static void new_average_chroma(const unsigned char *src, int stride, float *dc)
{
    int i;

    for (i = 0; i < 4; i++) {
        int x = i & 1;
        int y = i >> 1;

        dc[i] = (float)new_average_block(src + x * 8 + y * 8 * stride, stride);
    }
}

static void new_idct_add(unsigned char *dst, int stride,
                         const int16_t block[64])
{
    int32_t tmp[64];
    int i;

    for (i = 0; i < 64; i++)
        tmp[i] = block[i];
    bink2_idct_new(tmp);
    bink2_add_block(dst, stride, tmp);
}

static int new_decode_inter_luma(Bink2Video *v, Bink2Bits *bits,
                                 unsigned *prev_cbp, int q,
                                 unsigned char *dst, int stride, int flags)
{
    int16_t block[4][64];
    float *dc = v->old_dc_cur[v->old_mb_pos].dc[v->old_comp];
    unsigned cbp;
    int i, j;

    cbp = bink2_new_read_cbp_luma(bits, *prev_cbp,
                                  (uint32_t)(v->frame_flags & 0x40000));
    if (bink2_bits_failed(bits))
        return -1;
    *prev_cbp = cbp;
    (void)flags;
    if (new_decode_dc(v, bits, 1, q, -1023, 1023, 0xA8) != 0)
        return -1;

    for (i = 0; i < 4; i++) {
        if (new_decode_ac(bits, block, cbp >> (4 * i), q,
                          bink2g_inter_qmat) != 0)
            return -1;
        for (j = 0; j < 4; j++) {
            int pos = bink2_luma_repos[i * 4 + j];
            int off = (pos & 3) * 8 + (pos >> 2) * 8 * stride;

            block[j][0] = (int16_t)(dc[i * 4 + j] * 8.0f + 32.0f);
            new_idct_add(dst + off, stride, block[j]);
        }
    }
    return 0;
}

static int new_decode_inter_chroma(Bink2Video *v, Bink2Bits *bits,
                                   unsigned *prev_cbp, int q,
                                   unsigned char *dst, int stride)
{
    int16_t block[4][64];
    float *dc = v->old_dc_cur[v->old_mb_pos].dc[v->old_comp];
    unsigned cbp;
    int i;

    cbp = bink2_new_read_cbp_chroma(bits, *prev_cbp);
    if (bink2_bits_failed(bits))
        return -1;
    *prev_cbp = cbp;
    if (new_decode_dc(v, bits, 0, q, -1023, 1023, 0xA8) != 0)
        return -1;
    if (new_decode_ac(bits, block, cbp, q, bink2g_inter_qmat) != 0)
        return -1;

    for (i = 0; i < 4; i++) {
        int off = (i & 1) * 8 + (i >> 1) * 8 * stride;

        block[i][0] = (int16_t)(dc[i] * 8.0f + 32.0f);
        new_idct_add(dst + off, stride, block[i]);
    }
    return 0;
}

int bink2_video_decode_slice_new(Bink2Video *v, Bink2Bits *bits,
                                 int is_keyframe, int y0, int y1)
{
    int y, x;

    if (!is_keyframe && !v->has_prev) {
        return BINK2_ERROR;
    }

    memset(v->new_q_prev, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2Quant));
    memset(v->old_mv_prev, 0, ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

    for (y = y0; y < y1; y += 32) {
        uint8_t types_lru[4] = { 2, 3, 1, 0 };
        unsigned y_cbp_intra = 0, u_cbp_intra = 0, v_cbp_intra = 0;
        unsigned a_cbp_intra = 0;
        unsigned y_cbp_inter = 0, u_cbp_inter = 0, v_cbp_inter = 0;

        memset(v->new_q_cur, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2Quant));
        memset(v->old_mv_cur, 0, ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

        for (x = 0; x < (int)v->width; x += 32) {
            int flags = 0;
            int mb = x / 32;
            int type, dq, q, nb = 0;
            int raw[4][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
            int8_t *intra_q, *inter_q;

            if (is_keyframe) {
                type = 0;
            } else {
                type = bink2_new_read_btype(bits, types_lru);
                if (type > 3)
                    return BINK2_ERROR;
            }
            if (y == y0)
                flags |= 0x80;
            if (x == 0)
                flags |= 0x20;
            if (x == 32)
                flags |= 0x200;
            if (x + 32 >= (int)v->width)
                flags |= 0x40;

            v->old_mb_pos = mb;
            v->old_dc_cur[mb].block_type = type;
            intra_q = &v->new_q_cur[mb].intra_q;
            inter_q = &v->new_q_cur[mb].inter_q;

            switch (type) {
            case 0:
                if (!is_keyframe) {
                    if (!(flags & 0xA0) &&
                        v->old_dc_prev[mb > 0 ? mb - 1 : 0].block_type != 0) {
                        int px = x - 32, py = y - 32;

                        new_average_luma(v->cur[0] +
                                         (size_t)py * v->stride[0] + px,
                                         v->stride[0],
                                         v->old_dc_prev[mb - 1].dc[0]);
                        new_average_chroma(v->cur[2] +
                                           (size_t)(py / 2) * v->stride[2] + px / 2,
                                           v->stride[2],
                                           v->old_dc_prev[mb - 1].dc[1]);
                        new_average_chroma(v->cur[1] +
                                           (size_t)(py / 2) * v->stride[1] + px / 2,
                                           v->stride[1],
                                           v->old_dc_prev[mb - 1].dc[2]);
                    }
                    if (!(flags & 0x20) &&
                        v->old_dc_cur[mb > 0 ? mb - 1 : 0].block_type != 0) {
                        new_average_luma(v->cur[0] +
                                         (size_t)y * v->stride[0] + x - 32,
                                         v->stride[0],
                                         v->old_dc_cur[mb - 1].dc[0]);
                        new_average_chroma(v->cur[2] +
                                           (size_t)(y / 2) * v->stride[2] + x / 2 - 16,
                                           v->stride[2],
                                           v->old_dc_cur[mb - 1].dc[1]);
                        new_average_chroma(v->cur[1] +
                                           (size_t)(y / 2) * v->stride[1] + x / 2 - 16,
                                           v->stride[1],
                                           v->old_dc_cur[mb - 1].dc[2]);
                    }
                    if ((flags & 0x20) && !(flags & 0x80) &&
                        v->old_dc_prev[mb + 1].block_type != 0) {
                        new_average_luma(v->cur[0] +
                                         (size_t)(y - 32) * v->stride[0] + x + 32,
                                         v->stride[0],
                                         v->old_dc_prev[mb + 1].dc[0]);
                        new_average_chroma(v->cur[2] +
                                           (size_t)((y - 32) / 2) * v->stride[2] + x / 2 + 16,
                                           v->stride[2],
                                           v->old_dc_prev[mb + 1].dc[1]);
                        new_average_chroma(v->cur[1] +
                                           (size_t)((y - 32) / 2) * v->stride[1] + x / 2 + 16,
                                           v->stride[1],
                                           v->old_dc_prev[mb + 1].dc[2]);
                    }
                    if (!(flags & 0x80) &&
                        v->old_dc_prev[mb].block_type != 0) {
                        new_average_luma(v->cur[0] +
                                         (size_t)(y - 32) * v->stride[0] + x,
                                         v->stride[0],
                                         v->old_dc_prev[mb].dc[0]);
                        new_average_chroma(v->cur[2] +
                                           (size_t)((y - 32) / 2) * v->stride[2] + x / 2,
                                           v->stride[2],
                                           v->old_dc_prev[mb].dc[1]);
                        new_average_chroma(v->cur[1] +
                                           (size_t)((y - 32) / 2) * v->stride[1] + x / 2,
                                           v->stride[1],
                                           v->old_dc_prev[mb].dc[2]);
                    }
                }
                new_predict_mv(v, flags, raw, 0);
                update_inter_q(v, inter_q, 0, flags);
                dq = new_decode_dq(bits);
                if (bink2_bits_failed(bits))
                    return BINK2_ERROR;
                update_intra_q(v, intra_q, dq, flags);
                q = *intra_q;
                if (q < 0 || q >= 37) {
                    return BINK2_ERROR;
                }

                v->old_comp = 0;
                if (new_decode_intra_luma(v, bits, &y_cbp_intra, q,
                                          v->cur[0] +
                                          (size_t)y * v->stride[0] + x,
                                          v->stride[0], flags) != 0) {
                    return BINK2_ERROR;
                }
                v->old_comp = 1;
                if (new_decode_intra_chroma(v, bits, &u_cbp_intra, q,
                                            v->cur[2] +
                                            (size_t)(y / 2) * v->stride[2] + x / 2,
                                            v->stride[2], flags) != 0) {
                    return BINK2_ERROR;
                }
                v->old_comp = 2;
                if (new_decode_intra_chroma(v, bits, &v_cbp_intra, q,
                                            v->cur[1] +
                                            (size_t)(y / 2) * v->stride[1] + x / 2,
                                            v->stride[1], flags) != 0) {
                    return BINK2_ERROR;
                }
                if (v->has_alpha) {
                    v->old_comp = 3;
                    if (new_decode_intra_luma(v, bits, &a_cbp_intra, q,
                                              v->cur[3] +
                                              (size_t)y * v->stride[3] + x,
                                              v->stride[3], flags) != 0)
                        return BINK2_ERROR;
                }
                break;
            case 1:
                update_inter_q(v, inter_q, 0, flags);
                update_intra_q(v, intra_q, 0, flags);
                bink2_video_copy_mb(v, x, y);
                break;
            case 2:
                update_intra_q(v, intra_q, 0, flags);
                update_inter_q(v, inter_q, 0, flags);
                if (bink2_new_read_mv(bits, raw, &nb) != 0)
                    return BINK2_ERROR;
                new_predict_mv(v, flags, raw, nb);
                bink2_video_mc_luma(v, x, y);
                bink2_video_mc_chroma(v, x, y);
                bink2_video_store_motion(v, x, y, nb);
                break;
            case 3:
                update_intra_q(v, intra_q, 0, flags);
                if (bink2_new_read_mv(bits, raw, &nb) != 0)
                    return BINK2_ERROR;
                new_predict_mv(v, flags, raw, nb);
                dq = new_decode_dq(bits);
                if (bink2_bits_failed(bits))
                    return BINK2_ERROR;
                update_inter_q(v, inter_q, dq, flags);
                q = *inter_q;
                if (q < 0 || q >= 37) {
                    return BINK2_ERROR;
                }
                bink2_video_mc_luma(v, x, y);
                bink2_video_mc_chroma(v, x, y);
                v->old_comp = 0;
                if (new_decode_inter_luma(v, bits, &y_cbp_inter, q,
                                          v->cur[0] +
                                          (size_t)y * v->stride[0] + x,
                                          v->stride[0], flags) != 0)
                    return BINK2_ERROR;
                {
                    int chroma = bink2_bits_read1(bits);

                    if (chroma < 0)
                        return BINK2_ERROR;
                    if (chroma) {
                        v->old_comp = 1;
                        if (new_decode_inter_chroma(v, bits, &u_cbp_inter, q,
                                                    v->cur[2] +
                                                    (size_t)(y / 2) * v->stride[2] + x / 2,
                                                    v->stride[2]) != 0)
                            return BINK2_ERROR;
                        v->old_comp = 2;
                        if (new_decode_inter_chroma(v, bits, &v_cbp_inter, q,
                                                    v->cur[1] +
                                                    (size_t)(y / 2) * v->stride[1] + x / 2,
                                                    v->stride[1]) != 0)
                            return BINK2_ERROR;
                    } else {
                        u_cbp_inter = 0;
                        v_cbp_inter = 0;
                    }
                }
                bink2_video_store_motion(v, x, y, nb);
                break;
            default:
                return BINK2_ERROR;
            }
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
    return BINK2_OK;
}
