#include "bink2_tables.h"
#include "bink2_video.h"
#include "bink2_dsp.h"

#include <string.h>

#define NUM_AC_SKIPS 14

static const uint16_t old_quant_codes[16] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x30, 0x50, 0x70,
    0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0,
};
static const uint8_t old_quant_bits[16] = {
    1, 2, 3, 4, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8,
};
static const uint8_t old_skips[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 62, 0, 0, 0,
};
#define next_skips bink2_next_skips
#define luma_repos bink2_luma_repos
static const uint8_t q_dc_bits[16] = {
    1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 6,
};

static const float dc_quant[16] = {
    4, 4, 4, 4, 4, 6, 7, 8, 10, 12, 16, 24, 32, 48, 64, 128
};
static const float ac_quant[16] = {
    1.0f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 6.0f, 7.0f,
    8.0f, 12.0f, 16.0f, 24.0f, 32.0f, 48.0f, 64.0f, 128.0f
};
static const float luma_intra_qmat[64] = {
    0.125f,    0.190718f, 0.16332f,  0.235175f, 0.3f,      0.392847f, 0.345013f, 0.210373f,
    0.208056f, 0.288582f, 0.317145f, 0.387359f, 0.450788f, 0.790098f, 0.562995f, 0.263095f,
    0.228649f, 0.294491f, 0.341421f, 0.460907f, 0.653281f, 0.731424f, 0.60988f,  0.252336f,
    0.205778f, 0.346585f, 0.422498f, 0.501223f, 0.749621f, 1.004719f, 0.636379f, 0.251428f,
    0.225f,    0.381436f, 0.604285f, 0.823113f, 0.85f,     1.070509f, 0.69679f,  0.265553f,
    0.235708f, 0.476783f, 0.70576f,  0.739104f, 0.795516f, 0.802512f, 0.600616f, 0.249289f,
    0.331483f, 0.600528f, 0.689429f, 0.692062f, 0.69679f,  0.643138f, 0.43934f,  0.188511f,
    0.248309f, 0.440086f, 0.42807f,  0.397419f, 0.386259f, 0.270966f, 0.192244f, 0.094199f,
};
static const float luma_inter_qmat[64] = {
    0.125f,    0.17338f,  0.16332f,  0.146984f, 0.128475f, 0.106393f, 0.077046f, 0.043109f,
    0.17338f,  0.240485f, 0.226532f, 0.203873f, 0.1782f,   0.147571f, 0.109474f, 0.062454f,
    0.16332f,  0.226532f, 0.219321f, 0.202722f, 0.181465f, 0.149711f, 0.112943f, 0.062584f,
    0.146984f, 0.203873f, 0.202722f, 0.201647f, 0.183731f, 0.153976f, 0.11711f,  0.065335f,
    0.128475f, 0.1782f,   0.181465f, 0.183731f, 0.177088f, 0.155499f, 0.120267f, 0.068016f,
    0.106393f, 0.147571f, 0.149711f, 0.153976f, 0.155499f, 0.145756f, 0.116636f, 0.068495f,
    0.077046f, 0.109474f, 0.112943f, 0.11711f,  0.120267f, 0.116636f, 0.098646f, 0.060141f,
    0.043109f, 0.062454f, 0.062584f, 0.065335f, 0.068016f, 0.068495f, 0.060141f, 0.038853f,
};
static const float chroma_qmat[64] = {
    0.125f,      0.17338f,    0.217761f,   0.383793f,   0.6875f,     0.54016501f, 0.37207201f, 0.18968099f,
    0.17338f,    0.28056601f, 0.32721299f, 0.74753499f, 0.95358998f, 0.74923098f, 0.51607901f, 0.26309499f,
    0.217761f,   0.32721299f, 0.66387498f, 1.056244f,   0.89826202f, 0.70576f,    0.48613599f, 0.24783f,
    0.383793f,   0.74753499f, 1.056244f,   0.95059502f, 0.80841398f, 0.635167f,   0.437511f,   0.223041f,
    0.6875f,     0.95358998f, 0.89826202f, 0.80841398f, 0.6875f,     0.54016501f, 0.37207201f, 0.18968099f,
    0.54016501f, 0.74923098f, 0.70576f,    0.635167f,   0.54016501f, 0.42440501f, 0.292335f,   0.149031f,
    0.37207201f, 0.51607901f, 0.48613599f, 0.437511f,   0.37207201f, 0.292335f,   0.201364f,   0.102655f,
    0.18968099f, 0.26309499f, 0.24783f,    0.223041f,   0.18968099f, 0.149031f,   0.102655f,   0.052333001f,
};
static const uint8_t luma_scan[64] = {
     0,  2,  1,  8,  9, 17, 10, 16,
    24,  3, 18, 25, 32, 11, 33, 26,
     4, 40, 19, 12, 27, 41, 34,  5,
    20, 48,  6, 28, 15, 42, 23, 35,
    21, 13, 14,  7, 31, 43, 49, 36,
    22, 56, 39, 50, 30, 44, 29, 51,
    57, 47, 58, 59, 63, 61, 55, 38,
    52, 62, 45, 37, 60, 46, 54, 53
};
static const uint8_t chroma_scan[64] = {
     0,  1,  8,  2,  9, 16, 10, 17,
     3, 24, 11, 18, 25, 13, 14,  4,
    15,  5,  6,  7, 12, 19, 20, 21,
    22, 23, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63
};

static int read_vlc(Bink2Bits *bits, const uint16_t *codes,
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

static int popcount4(unsigned value)
{
    value &= 0xF;
    return (int)((value & 1) + ((value >> 1) & 1) + ((value >> 2) & 1) +
                 ((value >> 3) & 1));
}

static int old_delta_q(Bink2Bits *bits)
{
    int dq = read_vlc(bits, old_quant_codes, old_quant_bits, 16);

    if (dq) {
        int sign = bink2_bits_read1(bits);

        if (sign < 0)
            return -1;
        if (sign)
            dq = -dq;
    }
    return dq;
}

static int old_cbp_luma(Bink2Bits *bits, unsigned prev_cbp,
                        unsigned *out)
{
    unsigned cbp, cbp4, cbplo, cbphi;
    int b, i, j;

    b = bink2_bits_read1(bits);
    if (b < 0)
        return -1;
    if (b) {
        b = bink2_bits_read1(bits);
        if (b < 0)
            return -1;
        if (b) {
            *out = prev_cbp;
            return 0;
        }
        cbplo = prev_cbp & 0xFFFF;
    } else {
        cbplo = 0;
        cbp4 = (prev_cbp >> 4) & 0xF;
        for (i = 0; i < 4; i++) {
            b = bink2_bits_read1(bits);
            if (b < 0)
                return -1;
            if (!b) {
                int v = bink2_bits_read(bits, 4);

                if (v < 0)
                    return -1;
                cbp4 = (unsigned)v;
            }
            cbplo |= cbp4 << (i * 4);
        }
    }
    cbphi = 0;
    cbp = cbplo;
    cbp4 = (prev_cbp >> 20) & 0xF;
    for (i = 0; i < 4; i++) {
        if (popcount4(cbp)) {
            if (popcount4(cbp) == 1) {
                cbp4 = 0;
                for (j = 1; j < 16; j <<= 1) {
                    if (j & cbp) {
                        b = bink2_bits_read1(bits);
                        if (b < 0)
                            return -1;
                        if (b)
                            cbp4 |= (unsigned)j;
                    }
                }
            } else {
                b = bink2_bits_read1(bits);
                if (b < 0)
                    return -1;
                if (!b) {
                    cbp4 = 0;
                    for (j = 1; j < 16; j <<= 1) {
                        if (j & cbp) {
                            int bit = bink2_bits_read1(bits);

                            if (bit < 0)
                                return -1;
                            if (bit)
                                cbp4 |= (unsigned)j;
                        }
                    }
                }
            }
        } else {
            cbp4 = 0;
        }
        cbp4 &= cbp;
        cbphi = (cbphi >> 4) | (cbp4 << 0x1c);
        cbp >>= 4;
    }
    *out = cbphi | cbplo;
    return 0;
}

static int old_cbp_chroma(Bink2Bits *bits, unsigned prev_cbp,
                          unsigned *out)
{
    unsigned cbplo, cbphi;
    int b, i;

    b = bink2_bits_read1(bits);
    if (b < 0)
        return -1;
    if (b) {
        b = bink2_bits_read1(bits);
        if (b < 0)
            return -1;
        if (b) {
            *out = prev_cbp;
            return 0;
        }
        cbplo = prev_cbp & 0xF;
    } else {
        int v = bink2_bits_read(bits, 4);

        if (v < 0)
            return -1;
        cbplo = (unsigned)v;
    }
    cbphi = 0;
    if (popcount4(cbplo)) {
        if (popcount4(cbplo) != 1) {
            cbphi = (prev_cbp >> 16) & cbplo;
            b = bink2_bits_read1(bits);
            if (b < 0)
                return -1;
            if (b) {
                *out = cbplo | (cbphi << 16);
                return 0;
            }
        }
        cbphi = 0;
        for (i = 1; i < 16; i <<= 1) {
            if (i & cbplo) {
                b = bink2_bits_read1(bits);
                if (b < 0)
                    return -1;
                if (b)
                    cbphi |= (unsigned)i;
            }
        }
    }
    *out = cbplo | (cbphi << 16);
    return 0;
}

static float clipf(float value, float min, float max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

static float min3f(float a, float b, float c)
{
    float m = a < b ? a : b;

    return m < c ? m : c;
}

static float max3f(float a, float b, float c)
{
    float m = a > b ? a : b;

    return m > c ? m : c;
}

static float dc_mpred(float a, float b, float c)
{
    float lo = min3f(a, b, c);
    float hi = max3f(a, b, c);
    float v = c + b - a;

    return v < lo ? lo : v > hi ? hi : v;
}

static float dc_mpred2(float a, float b)
{
    float lo = a < b ? a : b;
    float hi = a > b ? a : b;
    float v = 2.0f * a - b;

    return v < lo ? lo : v > hi ? hi : v;
}

void bink2_video_predict_dc(Bink2Video *v, int is_luma, float mindc,
                            float maxdc, int flags, float tdc[16])
{
    int mb = v->old_mb_pos;
    int comp = v->old_comp;
    float *LTdc = v->old_dc_prev[mb > 0 ? mb - 1 : 0].dc[comp];
    float *Tdc = v->old_dc_prev[mb].dc[comp];
    float *Ldc = v->old_dc_cur[mb > 0 ? mb - 1 : 0].dc[comp];
    float *dc = v->old_dc_cur[mb].dc[comp];

    if (is_luma && (flags & 0x20) && (flags & 0x80)) {
        dc[0] = clipf((mindc < 0 ? 0.0f : 1024.0f) + tdc[0], mindc, maxdc);
        dc[1] = clipf(dc[0] + tdc[1], mindc, maxdc);
        dc[2] = clipf(dc_mpred2(dc[0], dc[1]) + tdc[2], mindc, maxdc);
        dc[3] = clipf(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3], mindc, maxdc);
        dc[4] = clipf(dc_mpred2(dc[1], dc[3]) + tdc[4], mindc, maxdc);
        dc[5] = clipf(dc[4] + tdc[5], mindc, maxdc);
        dc[6] = clipf(dc_mpred(dc[1], dc[3], dc[4]) + tdc[6], mindc, maxdc);
        dc[7] = clipf(dc_mpred(dc[4], dc[6], dc[5]) + tdc[7], mindc, maxdc);
        dc[8] = clipf(dc_mpred2(dc[2], dc[3]) + tdc[8], mindc, maxdc);
        dc[9] = clipf(dc_mpred(dc[2], dc[8], dc[3]) + tdc[9], mindc, maxdc);
        dc[10] = clipf(dc_mpred2(dc[8], dc[9]) + tdc[10], mindc, maxdc);
        dc[11] = clipf(dc_mpred(dc[8], dc[10], dc[9]) + tdc[11], mindc, maxdc);
        dc[12] = clipf(dc_mpred(dc[3], dc[9], dc[6]) + tdc[12], mindc, maxdc);
        dc[13] = clipf(dc_mpred(dc[6], dc[12], dc[7]) + tdc[13], mindc, maxdc);
        dc[14] = clipf(dc_mpred(dc[9], dc[11], dc[12]) + tdc[14], mindc, maxdc);
        dc[15] = clipf(dc_mpred(dc[12], dc[14], dc[13]) + tdc[15], mindc, maxdc);
    } else if (is_luma && (flags & 0x80)) {
        dc[0] = clipf(dc_mpred2(Ldc[5], Ldc[7]) + tdc[0], mindc, maxdc);
        dc[1] = clipf(dc[0] + tdc[1], mindc, maxdc);
        dc[2] = clipf(dc_mpred(Ldc[5], Ldc[7], dc[0]) + tdc[2], mindc, maxdc);
        dc[3] = clipf(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3], mindc, maxdc);
        dc[4] = clipf(dc_mpred2(dc[1], dc[3]) + tdc[4], mindc, maxdc);
        dc[5] = clipf(dc[4] + tdc[5], mindc, maxdc);
        dc[6] = clipf(dc_mpred(dc[1], dc[3], dc[4]) + tdc[6], mindc, maxdc);
        dc[7] = clipf(dc_mpred(dc[4], dc[6], dc[5]) + tdc[7], mindc, maxdc);
        dc[8] = clipf(dc_mpred(Ldc[7], Ldc[13], dc[2]) + tdc[8], mindc, maxdc);
        dc[9] = clipf(dc_mpred(dc[2], dc[8], dc[3]) + tdc[9], mindc, maxdc);
        dc[10] = clipf(dc_mpred(Ldc[13], Ldc[15], dc[8]) + tdc[10], mindc, maxdc);
        dc[11] = clipf(dc_mpred(dc[8], dc[10], dc[9]) + tdc[11], mindc, maxdc);
        dc[12] = clipf(dc_mpred(dc[3], dc[9], dc[6]) + tdc[12], mindc, maxdc);
        dc[13] = clipf(dc_mpred(dc[6], dc[12], dc[7]) + tdc[13], mindc, maxdc);
        dc[14] = clipf(dc_mpred(dc[9], dc[11], dc[12]) + tdc[14], mindc, maxdc);
        dc[15] = clipf(dc_mpred(dc[12], dc[14], dc[13]) + tdc[15], mindc, maxdc);
    } else if (is_luma && (flags & 0x20)) {
        dc[0] = clipf(dc_mpred2(Tdc[10], Tdc[11]) + tdc[0], mindc, maxdc);
        dc[1] = clipf(dc_mpred(Tdc[10], dc[0], Tdc[11]) + tdc[1], mindc, maxdc);
        dc[2] = clipf(dc_mpred2(dc[0], dc[1]) + tdc[2], mindc, maxdc);
        dc[3] = clipf(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3], mindc, maxdc);
        dc[4] = clipf(dc_mpred(Tdc[11], dc[1], Tdc[14]) + tdc[4], mindc, maxdc);
        dc[5] = clipf(dc_mpred(Tdc[14], dc[4], Tdc[15]) + tdc[5], mindc, maxdc);
        dc[6] = clipf(dc_mpred(dc[1], dc[3], dc[4]) + tdc[6], mindc, maxdc);
        dc[7] = clipf(dc_mpred(dc[4], dc[6], dc[5]) + tdc[7], mindc, maxdc);
        dc[8] = clipf(dc_mpred2(dc[2], dc[3]) + tdc[8], mindc, maxdc);
        dc[9] = clipf(dc_mpred(dc[2], dc[8], dc[3]) + tdc[9], mindc, maxdc);
        dc[10] = clipf(dc_mpred2(dc[8], dc[9]) + tdc[10], mindc, maxdc);
        dc[11] = clipf(dc_mpred(dc[8], dc[10], dc[9]) + tdc[11], mindc, maxdc);
        dc[12] = clipf(dc_mpred(dc[3], dc[9], dc[6]) + tdc[12], mindc, maxdc);
        dc[13] = clipf(dc_mpred(dc[6], dc[12], dc[7]) + tdc[13], mindc, maxdc);
        dc[14] = clipf(dc_mpred(dc[9], dc[11], dc[12]) + tdc[14], mindc, maxdc);
        dc[15] = clipf(dc_mpred(dc[12], dc[14], dc[13]) + tdc[15], mindc, maxdc);
    } else if (is_luma) {
        dc[0] = clipf(dc_mpred(LTdc[15], Ldc[5], Tdc[10]) + tdc[0], mindc, maxdc);
        dc[1] = clipf(dc_mpred(Tdc[10], dc[0], Tdc[11]) + tdc[1], mindc, maxdc);
        dc[2] = clipf(dc_mpred(Ldc[5], Ldc[7], dc[0]) + tdc[2], mindc, maxdc);
        dc[3] = clipf(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3], mindc, maxdc);
        dc[4] = clipf(dc_mpred(Tdc[11], dc[1], Tdc[14]) + tdc[4], mindc, maxdc);
        dc[5] = clipf(dc_mpred(Tdc[14], dc[4], Tdc[15]) + tdc[5], mindc, maxdc);
        dc[6] = clipf(dc_mpred(dc[1], dc[3], dc[4]) + tdc[6], mindc, maxdc);
        dc[7] = clipf(dc_mpred(dc[4], dc[6], dc[5]) + tdc[7], mindc, maxdc);
        dc[8] = clipf(dc_mpred(Ldc[7], Ldc[13], dc[2]) + tdc[8], mindc, maxdc);
        dc[9] = clipf(dc_mpred(dc[2], dc[8], dc[3]) + tdc[9], mindc, maxdc);
        dc[10] = clipf(dc_mpred(Ldc[13], Ldc[15], dc[8]) + tdc[10], mindc, maxdc);
        dc[11] = clipf(dc_mpred(dc[8], dc[10], dc[9]) + tdc[11], mindc, maxdc);
        dc[12] = clipf(dc_mpred(dc[3], dc[9], dc[6]) + tdc[12], mindc, maxdc);
        dc[13] = clipf(dc_mpred(dc[6], dc[12], dc[7]) + tdc[13], mindc, maxdc);
        dc[14] = clipf(dc_mpred(dc[9], dc[11], dc[12]) + tdc[14], mindc, maxdc);
        dc[15] = clipf(dc_mpred(dc[12], dc[14], dc[13]) + tdc[15], mindc, maxdc);
    } else if (!is_luma && (flags & 0x20) && (flags & 0x80)) {
        dc[0] = clipf((mindc < 0 ? 0.0f : 1024.0f) + tdc[0], mindc, maxdc);
        dc[1] = clipf(dc[0] + tdc[1], mindc, maxdc);
        dc[2] = clipf(dc_mpred2(dc[0], dc[1]) + tdc[2], mindc, maxdc);
        dc[3] = clipf(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3], mindc, maxdc);
    } else if (!is_luma && (flags & 0x80)) {
        dc[0] = clipf(dc_mpred2(Ldc[1], Ldc[3]) + tdc[0], mindc, maxdc);
        dc[1] = clipf(dc[0] + tdc[1], mindc, maxdc);
        dc[2] = clipf(dc_mpred(Ldc[1], Ldc[3], dc[0]) + tdc[2], mindc, maxdc);
        dc[3] = clipf(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3], mindc, maxdc);
    } else if (!is_luma && (flags & 0x20)) {
        dc[0] = clipf(dc_mpred2(Tdc[2], Tdc[3]) + tdc[0], mindc, maxdc);
        dc[1] = clipf(dc_mpred(Tdc[2], dc[0], Tdc[3]) + tdc[1], mindc, maxdc);
        dc[2] = clipf(dc_mpred2(dc[0], dc[1]) + tdc[2], mindc, maxdc);
        dc[3] = clipf(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3], mindc, maxdc);
    } else {
        dc[0] = clipf(dc_mpred(LTdc[3], Ldc[1], Tdc[2]) + tdc[0], mindc, maxdc);
        dc[1] = clipf(dc_mpred(Tdc[2], dc[0], Tdc[3]) + tdc[1], mindc, maxdc);
        dc[2] = clipf(dc_mpred(Ldc[1], Ldc[3], dc[0]) + tdc[2], mindc, maxdc);
        dc[3] = clipf(dc_mpred(dc[0], dc[2], dc[1]) + tdc[3], mindc, maxdc);
    }
}

static int old_decode_dc(Bink2Video *v, Bink2Bits *bits, int is_luma, int q,
                         int mindc, int maxdc, int flags)
{
    float tdc[16] = { 0 };
    int num_dc = is_luma ? 16 : 4;
    int dc_bits, i, j;

    dc_bits = bink2_bits_read(bits, 3);
    if (dc_bits < 0)
        return -1;
    if (dc_bits == 7) {
        int extra = bink2_bits_read(bits, 2);

        if (extra < 0)
            return -1;
        dc_bits += extra;
    }
    if (dc_bits == 0) {
        memset(tdc, 0, sizeof(tdc));
    } else {
        for (j = 0; j < num_dc; j += 4) {
            for (i = 0; i < 4; i++) {
                int value = bink2_bits_read(bits, dc_bits);

                if (value < 0)
                    return -1;
                tdc[i + j] = (float)value;
            }
            for (i = 0; i < 4; i++) {
                if (tdc[i + j] != 0.0f) {
                    int sign = bink2_bits_read1(bits);

                    if (sign < 0)
                        return -1;
                    if (sign)
                        tdc[i + j] = -tdc[i + j];
                }
            }
        }
    }

    if ((flags & 0x20) && (flags & 0x80) && mindc >= 0) {
        int nbits = q_dc_bits[q] + dc_bits;

        if (nbits < 10) {
            int dc_val = bink2_bits_read(bits, 10 - nbits);

            if (dc_val < 0)
                return -1;
            if (dc_val) {
                int sign;

                dc_val <<= dc_bits;
                sign = bink2_bits_read1(bits);
                if (sign < 0)
                    return -1;
                if (sign)
                    dc_val = -dc_val;
            }
            tdc[0] += (float)dc_val;
        }
    }

    for (i = 0; i < num_dc; i++)
        tdc[i] *= dc_quant[q];

    bink2_video_predict_dc(v, is_luma, (float)mindc, (float)maxdc, flags, tdc);
    return 0;
}

static int old_decode_ac(Bink2Bits *bits, const uint8_t *scan,
                         float block[4][64], unsigned cbp, float q,
                         const float qmat[64])
{
    int i;

    for (i = 0; i < 4; i++, cbp >>= 1) {
        const uint16_t *val_codes;
        const uint8_t *val_bits;
        const uint16_t *skip_codes;
        const uint8_t *skip_bits;
        int next = 0;
        int idx = 1;

        memset(block[i], 0, 64 * sizeof(float));
        if (!(cbp & 1))
            continue;
        if (cbp & 0x10000) {
            val_codes = bink2f_ac_val_codes[1];
            val_bits = bink2f_ac_val_bits[1];
            skip_codes = bink2f_ac_skip_codes[1];
            skip_bits = bink2f_ac_skip_bits[1];
        } else {
            val_codes = bink2f_ac_val_codes[0];
            val_bits = bink2f_ac_val_bits[0];
            skip_codes = bink2f_ac_skip_codes[0];
            skip_bits = bink2f_ac_skip_bits[0];
        }

        while (idx < 64) {
            int val = read_vlc(bits, val_codes, val_bits, 13);

            if (val < 0)
                return -1;
            if (val) {
                if (val >= 4) {
                    int nbits;

                    val -= 3;
                    nbits = bink2_bits_read(bits, val);
                    if (nbits < 0)
                        return -1;
                    val = nbits + (1 << val) + 2;
                }
                if (val) {
                    int sign = bink2_bits_read1(bits);

                    if (sign < 0)
                        return -1;
                    if (sign)
                        val = -val;
                }
            }
            block[i][scan[idx]] = (float)val * q * qmat[scan[idx]];
            if (idx > 62)
                break;
            idx++;
            next--;
            if (next < 1) {
                int skip = read_vlc(bits, skip_codes, skip_bits,
                                    NUM_AC_SKIPS);

                if (skip < 0)
                    return -1;
                next = next_skips[skip];
                skip = old_skips[skip];
                if (skip == 11) {
                    int extra = bink2_bits_read(bits, 6);

                    if (extra < 0)
                        return -1;
                    skip = extra;
                }
                idx += skip;
            }
        }
    }
    return 0;
}

static int old_decode_intra_luma(Bink2Video *v, Bink2Bits *bits,
                                 unsigned *prev_cbp, int *prev_q,
                                 unsigned char *dst, int stride, int flags)
{
    float block[4][64];
    float *dc = v->old_dc_cur[v->old_mb_pos].dc[v->old_comp];
    unsigned cbp;
    int q, dq, i, j;

    if (old_cbp_luma(bits, *prev_cbp, &cbp) != 0)
        return -1;
    *prev_cbp = cbp;
    dq = old_delta_q(bits);
    q = *prev_q + dq;
    if (q < 0 || q >= 16)
        return -1;
    *prev_q = q;

    if (old_decode_dc(v, bits, 1, q, 0, 2047, flags) != 0)
        return -1;

    for (i = 0; i < 4; i++) {
        if (old_decode_ac(bits, luma_scan, block, cbp >> (4 * i),
                          ac_quant[q], luma_intra_qmat) != 0)
            return -1;
        for (j = 0; j < 4; j++) {
            int pos = luma_repos[i * 4 + j];
            int off = (pos & 3) * 8 + (pos >> 2) * 8 * stride;

            block[j][0] = dc[i * 4 + j] * 0.125f;
            bink2_idct_old(block[j]);
            bink2_put_block_f(dst + off, stride, block[j]);
        }
    }
    return 0;
}

static int old_decode_intra_chroma(Bink2Video *v, Bink2Bits *bits,
                                   unsigned *prev_cbp, int *prev_q,
                                   unsigned char *dst, int stride, int flags)
{
    float block[4][64];
    float *dc = v->old_dc_cur[v->old_mb_pos].dc[v->old_comp];
    unsigned cbp;
    int q, dq, i;

    if (old_cbp_chroma(bits, *prev_cbp, &cbp) != 0)
        return -1;
    *prev_cbp = cbp;
    dq = old_delta_q(bits);
    q = *prev_q + dq;
    if (q < 0 || q >= 16)
        return -1;
    *prev_q = q;

    if (old_decode_dc(v, bits, 0, q, 0, 2047, flags) != 0)
        return -1;
    if (old_decode_ac(bits, chroma_scan, block, cbp, ac_quant[q],
                      chroma_qmat) != 0)
        return -1;

    for (i = 0; i < 4; i++) {
        int off = (i & 1) * 8 + (i >> 1) * 8 * stride;

        block[i][0] = dc[i] * 0.125f;
        bink2_idct_old(block[i]);
        bink2_put_block_f(dst + off, stride, block[i]);
    }
    return 0;
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

#define MV(arr, mb, k, c) ((arr)[(mb) * 8 + (k) * 2 + (c)])

static void old_predict_mv(Bink2Video *v, int flags, const int raw[4][2])
{
    int mb = v->old_mb_pos;
    int lm = mb > 0 ? mb - 1 : 0;
    int32_t *c = v->old_mv_cur;
    int32_t *l = v->old_mv_cur;
    int32_t *lt = v->old_mv_prev;
    int32_t *t = v->old_mv_prev;
    int k;

    if (!(flags & 0x80)) {
        if (flags & 0x20) {
            MV(c, mb, 0, 0) = raw[0][0] + mid_pred(MV(t, mb, 0, 0), MV(t, mb, 2, 0), MV(t, mb, 3, 0));
            MV(c, mb, 0, 1) = raw[0][1] + mid_pred(MV(t, mb, 0, 1), MV(t, mb, 2, 1), MV(t, mb, 3, 1));
            MV(c, mb, 1, 0) = raw[1][0] + mid_pred(MV(t, mb, 2, 0), MV(t, mb, 3, 0), MV(c, mb, 0, 0));
            MV(c, mb, 1, 1) = raw[1][1] + mid_pred(MV(t, mb, 2, 1), MV(t, mb, 3, 1), MV(c, mb, 0, 1));
            MV(c, mb, 2, 0) = raw[2][0] + mid_pred(MV(t, mb, 2, 0), MV(c, mb, 0, 0), MV(c, mb, 1, 0));
            MV(c, mb, 2, 1) = raw[2][1] + mid_pred(MV(t, mb, 2, 1), MV(c, mb, 0, 1), MV(c, mb, 1, 1));
            MV(c, mb, 3, 0) = raw[3][0] + mid_pred(MV(c, mb, 0, 0), MV(c, mb, 1, 0), MV(c, mb, 2, 0));
            MV(c, mb, 3, 1) = raw[3][1] + mid_pred(MV(c, mb, 0, 1), MV(c, mb, 1, 1), MV(c, mb, 2, 1));
        } else {
            MV(c, mb, 0, 0) = raw[0][0] + mid_pred(MV(lt, lm, 3, 0), MV(t, mb, 2, 0), MV(l, lm, 1, 0));
            MV(c, mb, 0, 1) = raw[0][1] + mid_pred(MV(lt, lm, 3, 1), MV(t, mb, 2, 1), MV(l, lm, 1, 1));
            MV(c, mb, 1, 0) = raw[1][0] + mid_pred(MV(t, mb, 2, 0), MV(t, mb, 3, 0), MV(c, mb, 0, 0));
            MV(c, mb, 1, 1) = raw[1][1] + mid_pred(MV(t, mb, 2, 1), MV(t, mb, 3, 1), MV(c, mb, 0, 1));
            MV(c, mb, 2, 0) = raw[2][0] + mid_pred(MV(t, mb, 2, 0), MV(c, mb, 0, 0), MV(c, mb, 1, 0));
            MV(c, mb, 2, 1) = raw[2][1] + mid_pred(MV(t, mb, 2, 1), MV(c, mb, 0, 1), MV(c, mb, 1, 1));
            MV(c, mb, 3, 0) = raw[3][0] + mid_pred(MV(c, mb, 0, 0), MV(c, mb, 1, 0), MV(c, mb, 2, 0));
            MV(c, mb, 3, 1) = raw[3][1] + mid_pred(MV(c, mb, 0, 1), MV(c, mb, 1, 1), MV(c, mb, 2, 1));
        }
    } else {
        if (flags & 0x20) {
            for (k = 0; k < 4; k++) {
                MV(c, mb, k, 0) = raw[k][0];
                MV(c, mb, k, 1) = raw[k][1];
            }
        } else {
            MV(c, mb, 0, 0) = raw[0][0] + mid_pred(MV(l, lm, 0, 0), MV(l, lm, 1, 0), MV(l, lm, 3, 0));
            MV(c, mb, 0, 1) = raw[0][1] + mid_pred(MV(l, lm, 0, 1), MV(l, lm, 1, 1), MV(l, lm, 3, 1));
            MV(c, mb, 2, 0) = raw[2][0] + mid_pred(MV(l, lm, 1, 0), MV(l, lm, 3, 0), MV(c, mb, 0, 0));
            MV(c, mb, 2, 1) = raw[2][1] + mid_pred(MV(l, lm, 1, 1), MV(l, lm, 3, 1), MV(c, mb, 0, 1));
            MV(c, mb, 1, 0) = raw[1][0] + mid_pred(MV(l, lm, 1, 0), MV(c, mb, 0, 0), MV(c, mb, 2, 0));
            MV(c, mb, 1, 1) = raw[1][1] + mid_pred(MV(l, lm, 1, 1), MV(c, mb, 0, 1), MV(c, mb, 2, 1));
            MV(c, mb, 3, 0) = raw[3][0] + mid_pred(MV(c, mb, 0, 0), MV(c, mb, 1, 0), MV(c, mb, 2, 0));
            MV(c, mb, 3, 1) = raw[3][1] + mid_pred(MV(c, mb, 0, 1), MV(c, mb, 1, 1), MV(c, mb, 2, 1));
        }
    }
}

static int old_decode_mv(Bink2Bits *bits, int flags, int mv[4][2])
{
    int i, j;

    memset(mv, 0, 8 * sizeof(int));
    for (i = 0; i < 2; i++) {
        int nbits = bink2_bits_read(bits, 3);

        if (nbits < 0)
            return -1;
        if (nbits == 7) {
            int extra = bink2_bits_read(bits, 2);

            if (extra < 0)
                return -1;
            nbits += extra;
        }
        if (nbits) {
            for (j = 0; j < 4; j++) {
                int value = bink2_bits_read(bits, nbits);

                if (value < 0)
                    return -1;
                mv[j][i] = value;
            }
            for (j = 0; j < 4; j++) {
                if (mv[j][i]) {
                    int sign = bink2_bits_read1(bits);

                    if (sign < 0)
                        return -1;
                    if (sign)
                        mv[j][i] = -mv[j][i];
                }
            }
        }
        if ((flags & 0x80) && (flags & 0x20)) {
            int val = bink2_bits_read(bits, 5);

            if (val < 0)
                return -1;
            val *= 16;
            if (val) {
                int sign = bink2_bits_read1(bits);

                if (sign < 0)
                    return -1;
                if (sign)
                    val = -val;
            }
            for (j = 0; j < 4; j++)
                mv[j][i] += val;
        }
    }
    return 0;
}

void bink2_video_mc_luma(Bink2Video *v, int x, int y)
{
    int32_t *mv = v->old_mv_cur + v->old_mb_pos * 8;
    int k;

    for (k = 0; k < 4; k++) {
        int bx = x + (k & 1) * 16;
        int by = y + (k >> 1) * 16;

        bink2_mc_luma(v->cur[0] + (size_t)by * v->stride[0] + bx,
                      v->stride[0], v->ref[0], v->stride[0],
                      bx, by, (int)v->width, (int)v->height,
                      mv[k * 2], mv[k * 2 + 1]);
    }
}

void bink2_video_mc_chroma(Bink2Video *v, int x, int y)
{
    int32_t *mv = v->old_mv_cur + v->old_mb_pos * 8;
    /* The pinned reference passes width/2 and height/2. Clamp only the
     * degenerate 1-pixel case, which used to build a zero-size edge buffer. */
    int cw = (int)v->width / 2;
    int ch = (int)v->height / 2;
    int k;

    if (cw < 1)
        cw = 1;
    if (ch < 1)
        ch = 1;
    for (k = 0; k < 4; k++) {
        int bx = x / 2 + (k & 1) * 8;
        int by = y / 2 + (k >> 1) * 8;

        bink2_mc_chroma(v->cur[2] + (size_t)by * v->stride[2] + bx,
                        v->stride[2], v->ref[2], v->stride[2],
                        bx, by, cw, ch, mv[k * 2], mv[k * 2 + 1]);
        bink2_mc_chroma(v->cur[1] + (size_t)by * v->stride[1] + bx,
                        v->stride[1], v->ref[1], v->stride[1],
                        bx, by, cw, ch, mv[k * 2], mv[k * 2 + 1]);
    }
}

static float old_average_block(const unsigned char *src, int stride)
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
    return (float)sum;
}

static void old_average_luma(const unsigned char *src, int stride, float *dc)
{
    int i;

    for (i = 0; i < 16; i++) {
        int pos = luma_repos[i];
        int x = pos & 3;
        int y = pos >> 2;

        dc[i] = old_average_block(src + x * 8 + y * 8 * stride, stride);
    }
}

static void old_average_chroma(const unsigned char *src, int stride, float *dc)
{
    int i;

    for (i = 0; i < 4; i++) {
        int x = i & 1;
        int y = i >> 1;

        dc[i] = old_average_block(src + x * 8 + y * 8 * stride, stride);
    }
}

static int old_decode_inter_luma(Bink2Video *v, Bink2Bits *bits,
                                 unsigned *prev_cbp, int *prev_q,
                                 unsigned char *dst, int stride, int flags)
{
    float block[4][64];
    float *dc = v->old_dc_cur[v->old_mb_pos].dc[v->old_comp];
    unsigned cbp;
    int q, dq, i, j;

    (void)flags;
    if (old_cbp_luma(bits, *prev_cbp, &cbp) != 0)
        return -1;
    *prev_cbp = cbp;
    dq = old_delta_q(bits);
    q = *prev_q + dq;
    if (q < 0 || q >= 16)
        return -1;
    *prev_q = q;

    if (old_decode_dc(v, bits, 1, q, -1023, 1023, 0xA8) != 0)
        return -1;

    for (i = 0; i < 4; i++) {
        if (old_decode_ac(bits, luma_scan, block, cbp >> (4 * i),
                          ac_quant[q], luma_inter_qmat) != 0)
            return -1;
        for (j = 0; j < 4; j++) {
            int pos = luma_repos[i * 4 + j];
            int off = (pos & 3) * 8 + (pos >> 2) * 8 * stride;

            block[j][0] = dc[i * 4 + j] * 0.125f;
            bink2_idct_old(block[j]);
            bink2_add_block_f(dst + off, stride, block[j]);
        }
    }
    return 0;
}

static int old_decode_inter_chroma(Bink2Video *v, Bink2Bits *bits,
                                   unsigned *prev_cbp, int *prev_q,
                                   unsigned char *dst, int stride)
{
    float block[4][64];
    float *dc = v->old_dc_cur[v->old_mb_pos].dc[v->old_comp];
    unsigned cbp;
    int q, dq, i;

    if (old_cbp_chroma(bits, *prev_cbp, &cbp) != 0)
        return -1;
    *prev_cbp = cbp;
    dq = old_delta_q(bits);
    q = *prev_q + dq;
    if (q < 0 || q >= 16)
        return -1;
    *prev_q = q;

    if (old_decode_dc(v, bits, 0, q, -1023, 1023, 0xA8) != 0)
        return -1;
    if (old_decode_ac(bits, chroma_scan, block, cbp, ac_quant[q],
                      chroma_qmat) != 0)
        return -1;

    for (i = 0; i < 4; i++) {
        int off = (i & 1) * 8 + (i >> 1) * 8 * stride;

        block[i][0] = dc[i] * 0.125f;
        bink2_idct_old(block[i]);
        bink2_add_block_f(dst + off, stride, block[i]);
    }
    return 0;
}

void bink2_video_copy_mb(Bink2Video *v, int x, int y)
{
    int r;

    for (r = 0; r < 32; r++)
        memcpy(v->cur[0] + (size_t)(y + r) * v->stride[0] + x,
               v->ref[0] + (size_t)(y + r) * v->stride[0] + x, 32);
    for (r = 0; r < 16; r++) {
        memcpy(v->cur[2] + (size_t)(y / 2 + r) * v->stride[2] + x / 2,
               v->ref[2] + (size_t)(y / 2 + r) * v->stride[2] + x / 2, 16);
        memcpy(v->cur[1] + (size_t)(y / 2 + r) * v->stride[1] + x / 2,
               v->ref[1] + (size_t)(y / 2 + r) * v->stride[1] + x / 2, 16);
    }
    if (v->has_alpha) {
        for (r = 0; r < 32; r++)
            memcpy(v->cur[3] + (size_t)(y + r) * v->stride[3] + x,
                   v->ref[3] + (size_t)(y + r) * v->stride[3] + x, 32);
    }
}

static int16_t clamp_motion(int32_t value)
{
    if (value < -32768)
        return -32768;
    if (value > 32767)
        return 32767;
    return (int16_t)value;
}

void bink2_video_store_motion(Bink2Video *v, int x, int y, int nb)
{
    size_t mb = (size_t)(y / 32) * v->mb_w + x / 32;
    int32_t *mv = v->old_mv_cur + v->old_mb_pos * 8;
    int k;

    for (k = 0; k < 4; k++) {
        v->mv[0][mb * 4 + k] = clamp_motion(mv[k * 2]);
        v->mv[1][mb * 4 + k] = clamp_motion(mv[k * 2 + 1]);
    }
    v->mv_count[mb] = (unsigned char)nb;
}

int bink2_video_decode_slice_old(Bink2Video *v, Bink2Bits *bits,
                                 int is_keyframe, int y0, int y1)
{
    int y, x;

    if (!is_keyframe && !v->has_prev)
        return BINK2_ERROR;

    memset(v->old_mv_prev, 0,
           ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

    for (y = y0; y < y1; y += 32) {
        unsigned y_cbp_intra = 0, u_cbp_intra = 0, v_cbp_intra = 0;
        unsigned a_cbp_intra = 0;
        unsigned y_cbp_inter = 0, u_cbp_inter = 0, v_cbp_inter = 0;
        unsigned a_cbp_inter = 0;
        int y_q_intra = 8, u_q_intra = 8, v_q_intra = 8, a_q_intra = 8;
        int y_q_inter = 8, u_q_inter = 8, v_q_inter = 8, a_q_inter = 8;

        memset(v->old_mv_cur, 0,
               ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

        for (x = 0; x < (int)v->width; x += 32) {
            int flags = 0;
            int mb = x / 32;
            int type;
            int raw[4][2];

            if (y == y0)
                flags |= 0x80;
            if (x == 0)
                flags |= 0x20;
            if (x == 32)
                flags |= 0x200;
            if (x + 32 >= (int)v->width)
                flags |= 0x40;
            if (is_keyframe) {
                type = 0;
            } else {
                type = bink2_bits_read(bits, 2);
                if (type < 0)
                    return BINK2_ERROR;
            }
            v->old_mb_pos = mb;
            v->old_dc_cur[mb].block_type = type;
            switch (type) {
            case 0:
                if (!is_keyframe) {
                    if (!(flags & 0xA0) &&
                        v->old_dc_prev[mb > 0 ? mb - 1 : 0].block_type != 0) {
                        int px = x - 32;
                        int py = y - 32;

                        old_average_luma(v->cur[0] +
                                         (size_t)py * v->stride[0] + px,
                                         v->stride[0],
                                         v->old_dc_prev[mb - 1].dc[0]);
                        old_average_chroma(v->cur[2] +
                                           (size_t)(py / 2) * v->stride[2] + px / 2,
                                           v->stride[2],
                                           v->old_dc_prev[mb - 1].dc[1]);
                        old_average_chroma(v->cur[1] +
                                           (size_t)(py / 2) * v->stride[1] + px / 2,
                                           v->stride[1],
                                           v->old_dc_prev[mb - 1].dc[2]);
                    }
                    if (!(flags & 0x20) &&
                        v->old_dc_cur[mb > 0 ? mb - 1 : 0].block_type != 0) {
                        old_average_luma(v->cur[0] +
                                         (size_t)y * v->stride[0] + x - 32,
                                         v->stride[0],
                                         v->old_dc_cur[mb - 1].dc[0]);
                        old_average_chroma(v->cur[2] +
                                           (size_t)(y / 2) * v->stride[2] + x / 2 - 16,
                                           v->stride[2],
                                           v->old_dc_cur[mb - 1].dc[1]);
                        old_average_chroma(v->cur[1] +
                                           (size_t)(y / 2) * v->stride[1] + x / 2 - 16,
                                           v->stride[1],
                                           v->old_dc_cur[mb - 1].dc[2]);
                    }
                    if ((flags & 0x20) && !(flags & 0x80) &&
                        v->old_dc_prev[mb + 1].block_type != 0) {
                        old_average_luma(v->cur[0] +
                                         (size_t)(y - 32) * v->stride[0] + x + 32,
                                         v->stride[0],
                                         v->old_dc_prev[mb + 1].dc[0]);
                        old_average_chroma(v->cur[2] +
                                           (size_t)((y - 32) / 2) * v->stride[2] + x / 2 + 16,
                                           v->stride[2],
                                           v->old_dc_prev[mb + 1].dc[1]);
                        old_average_chroma(v->cur[1] +
                                           (size_t)((y - 32) / 2) * v->stride[1] + x / 2 + 16,
                                           v->stride[1],
                                           v->old_dc_prev[mb + 1].dc[2]);
                    }
                    if (!(flags & 0x80) &&
                        v->old_dc_prev[mb].block_type != 0) {
                        old_average_luma(v->cur[0] +
                                         (size_t)(y - 32) * v->stride[0] + x,
                                         v->stride[0],
                                         v->old_dc_prev[mb].dc[0]);
                        old_average_chroma(v->cur[2] +
                                           (size_t)((y - 32) / 2) * v->stride[2] + x / 2,
                                           v->stride[2],
                                           v->old_dc_prev[mb].dc[1]);
                        old_average_chroma(v->cur[1] +
                                           (size_t)((y - 32) / 2) * v->stride[1] + x / 2,
                                           v->stride[1],
                                           v->old_dc_prev[mb].dc[2]);
                    }
                }
                memset(raw, 0, sizeof(raw));
                old_predict_mv(v, flags, raw);
                v->old_comp = 0;
                if (old_decode_intra_luma(v, bits, &y_cbp_intra,
                                          &y_q_intra,
                                          v->cur[0] +
                                          (size_t)y * v->stride[0] + x,
                                          v->stride[0], flags) != 0)
                    return BINK2_ERROR;
                v->old_comp = 1;
                if (old_decode_intra_chroma(v, bits, &u_cbp_intra,
                                            &u_q_intra,
                                            v->cur[2] +
                                            (size_t)(y / 2) * v->stride[2] + x / 2,
                                            v->stride[2], flags) != 0)
                    return BINK2_ERROR;
                v->old_comp = 2;
                if (old_decode_intra_chroma(v, bits, &v_cbp_intra,
                                            &v_q_intra,
                                            v->cur[1] +
                                            (size_t)(y / 2) * v->stride[1] + x / 2,
                                            v->stride[1], flags) != 0)
                    return BINK2_ERROR;
                if (v->has_alpha) {
                    v->old_comp = 3;
                    if (old_decode_intra_luma(v, bits, &a_cbp_intra,
                                              &a_q_intra,
                                              v->cur[3] +
                                              (size_t)y * v->stride[3] + x,
                                              v->stride[3], flags) != 0)
                        return BINK2_ERROR;
                }
                break;
            case 1:
                bink2_video_copy_mb(v, x, y);
                break;
            case 2:
            case 3:
                if (old_decode_mv(bits, flags, raw) != 0)
                    return BINK2_ERROR;
                old_predict_mv(v, flags, raw);
                bink2_video_mc_luma(v, x, y);
                bink2_video_mc_chroma(v, x, y);
                bink2_video_store_motion(v, x, y, 4);
                if (type == 3) {
                    v->old_comp = 0;
                    if (old_decode_inter_luma(v, bits, &y_cbp_inter,
                                              &y_q_inter,
                                              v->cur[0] +
                                              (size_t)y * v->stride[0] + x,
                                              v->stride[0], flags) != 0) {
                        return BINK2_ERROR;
                    }
                    v->old_comp = 1;
                    if (old_decode_inter_chroma(v, bits, &u_cbp_inter,
                                                &u_q_inter,
                                                v->cur[2] +
                                                (size_t)(y / 2) * v->stride[2] + x / 2,
                                                v->stride[2]) != 0)
                        return BINK2_ERROR;
                    v->old_comp = 2;
                    if (old_decode_inter_chroma(v, bits, &v_cbp_inter,
                                                &v_q_inter,
                                                v->cur[1] +
                                                (size_t)(y / 2) * v->stride[1] + x / 2,
                                                v->stride[1]) != 0)
                        return BINK2_ERROR;
                    if (v->has_alpha) {
                        v->old_comp = 3;
                        if (old_decode_inter_luma(v, bits, &a_cbp_inter,
                                                  &a_q_inter,
                                                  v->cur[3] +
                                                  (size_t)y * v->stride[3] + x,
                                                  v->stride[3], flags) != 0)
                            return BINK2_ERROR;
                    }
                }
                break;
            default:
                return BINK2_ERROR;
            }
        }

        {
            Bink2OldDc *dc_swap = v->old_dc_cur;
            int32_t *mv_swap = v->old_mv_cur;

            v->old_dc_cur = v->old_dc_prev;
            v->old_dc_prev = dc_swap;
            v->old_mv_cur = v->old_mv_prev;
            v->old_mv_prev = mv_swap;
        }
    }
    return BINK2_OK;
}
