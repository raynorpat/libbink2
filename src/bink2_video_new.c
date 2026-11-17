#include "bink2_video.h"
#include "bink2_dsp.h"

#include <string.h>

#define NUM_AC_SKIPS 14

static const uint8_t new_scan[64] = {
     0,   8,   1,   2,  9,  16,  24,  17,
    10,   3,   4,  11, 18,  25,  32,  40,
    33,  26,  19,  12,  5,   6,  13,  20,
    27,  34,  41,  48, 56,  49,  42,  35,
    28,  21,  14,   7, 15,  22,  29,  36,
    43,  50,  57,  58, 51,  44,  37,  30,
    23,  31,  38,  45, 52,  59,  60,  53,
    46,  39,  47,  54, 61,  62,  55,  63,
};
static const uint16_t new_ac_skip_codes[2][NUM_AC_SKIPS] = {
    {
        0x01, 0x00, 0x004, 0x02C, 0x06C, 0x0C, 0x4C,
        0xAC, 0xEC, 0x12C, 0x16C, 0x1AC, 0x02, 0x1C,
    },
    {
        0x01, 0x04, 0x00, 0x08, 0x02, 0x32, 0x0A,
        0x12, 0x3A, 0x7A, 0xFA, 0x72, 0x06, 0x1A,
    },
};
static const uint8_t new_ac_skip_bits[2][NUM_AC_SKIPS] = {
    { 1, 3, 4, 9, 9, 7, 7, 9, 8, 9, 9, 9, 2, 5 },
    { 1, 3, 4, 4, 5, 7, 5, 6, 7, 8, 8, 7, 3, 6 },
};
static const uint16_t new_mv_codes[] = {
    0x01, 0x06, 0x0C, 0x1C, 0x18, 0x38, 0x58, 0x78,
    0x68, 0x48, 0x28, 0x08, 0x14, 0x04, 0x02, 0x00,
};
static const uint8_t new_mv_bits[] = {
    1, 3, 5, 5, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 3, 4,
};
static const uint8_t new_skips[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 64, 0, 0, 0,
};
static const uint8_t new_chroma_cbp_pat[16] = {
    0x00, 0x00, 0x00, 0x0F,
    0x00, 0x0F, 0x0F, 0x0F,
    0x00, 0x0F, 0x0F, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F,
};
static const int32_t new_dc_pat[] = {
    1024, 1218, 1448, 1722, 2048,
    2435, 2896, 3444, 4096, 4871,
    5793, 6889, 8192, 9742, 11585, 13777, 16384,
    19484, 23170,  27555, 32768, 38968, 46341,
    55109, 65536, 77936, 92682, 110218, 131072,
    155872, 185364, 220436, 262144, 311744,
    370728, 440872, 524288,
};
static const uint16_t new_luma_intra_qmat[4][64] = {
    {
     1024,   1432,   1506,   1181,
     1843,   2025,   5271,   8592,
     1313,   1669,   1630,   1672,
     2625,   3442,   8023,  12794,
     1076,   1755,   1808,   1950,
     3980,   4875,   8813,  11909,
     1350,   1868,   2127,   2016,
     4725,   4450,   7712,   9637,
     2458,   3103,   4303,   4303,
     6963,   6835,  11079,  13365,
     3375,   5704,   5052,   6049,
     9198,   7232,  10725,   9834,
     5486,   7521,   7797,   7091,
    11079,  10016,  13559,  12912,
     7279,   7649,   7020,   6097,
     9189,   9047,  12661,  13768,
    },
    {
     1218,   1703,   1791,   1405,
     2192,   2408,   6268,  10218,
     1561,   1985,   1938,   1988,
     3122,   4093,   9541,  15215,
     1279,   2087,   2150,   2319,
     4733,   5798,  10481,  14162,
     1606,   2222,   2530,   2398,
     5619,   5292,   9171,  11460,
     2923,   3690,   5117,   5118,
     8281,   8128,  13176,  15894,
     4014,   6783,   6008,   7194,
    10938,   8600,  12755,  11694,
     6524,   8944,   9272,   8433,
    13176,  11911,  16125,  15354,
     8657,   9096,   8348,   7250,
    10927,  10759,  15056,  16373,
    },
    {
     1448,   2025,   2130,   1671,
     2607,   2864,   7454,  12151,
     1856,   2360,   2305,   2364,
     3713,   4867,  11346,  18094,
     1521,   2482,   2557,   2758,
     5628,   6894,  12464,  16841,
     1909,   2642,   3008,   2852,
     6683,   6293,  10906,  13629,
     3476,   4388,   6085,   6086,
     9847,   9666,  15668,  18901,
     4773,   8066,   7145,   8555,
    13007,  10227,  15168,  13907,
     7758,  10637,  11026,  10028,
    15668,  14165,  19175,  18259,
    10294,  10817,   9927,   8622,
    12995,  12794,  17905,  19470,
    },
    {
     1722,   2408,   2533,   1987,
     3100,   3406,   8864,  14450,
     2208,   2807,   2741,   2811,
     4415,   5788,  13493,  21517,
     1809,   2951,   3041,   3280,
     6693,   8199,  14822,  20028,
     2271,   3142,   3578,   3391,
     7947,   7484,  12969,  16207,
     4133,   5218,   7236,   7238,
    11711,  11495,  18633,  22478,
     5677,   9592,   8497,  10174,
    15469,  12162,  18038,  16538,
     9226,  12649,  13112,  11926,
    18633,  16845,  22804,  21715,
    12242,  12864,  11806,  10254,
    15454,  15215,  21293,  23155,
    },
};
static const uint16_t new_chroma_intra_qmat[4][64] = {
    {
     1024,   1193,   1434,   2203,
     5632,   4641,   5916,   6563,
     1193,   1622,   1811,   3606,
     6563,   5408,   6894,   7649,
     1434,   1811,   3515,   4875,
     5916,   4875,   6215,   6894,
     2203,   3606,   4875,   3824,
     4641,   3824,   4875,   5408,
     5632,   6563,   5916,   4641,
     5632,   4641,   5916,   6563,
     4641,   5408,   4875,   3824,
     4641,   3824,   4875,   5408,
     5916,   6894,   6215,   4875,
     5916,   4875,   6215,   6894,
     6563,   7649,   6894,   5408,
     6563,   5408,   6894,   7649,
    },
    {
     1218,   1419,   1706,   2620,
     6698,   5519,   7035,   7805,
     1419,   1929,   2153,   4288,
     7805,   6432,   8199,   9096,
     1706,   2153,   4180,   5798,
     7035,   5798,   7390,   8199,
     2620,   4288,   5798,   4548,
     5519,   4548,   5798,   6432,
     6698,   7805,   7035,   5519,
     6698,   5519,   7035,   7805,
     5519,   6432,   5798,   4548,
     5519,   4548,   5798,   6432,
     7035,   8199,   7390,   5798,
     7035,   5798,   7390,   8199,
     7805,   9096,   8199,   6432,
     7805,   6432,   8199,   9096,
    },
    {
     1448,   1688,   2028,   3116,
     7965,   6563,   8367,   9282,
     1688,   2294,   2561,   5099,
     9282,   7649,   9750,  10817,
     2028,   2561,   4971,   6894,
     8367,   6894,   8789,   9750,
     3116,   5099,   6894,   5408,
     6563,   5408,   6894,   7649,
     7965,   9282,   8367,   6563,
     7965,   6563,   8367,   9282,
     6563,   7649,   6894,   5408,
     6563,   5408,   6894,   7649,
     8367,   9750,   8789,   6894,
     8367,   6894,   8789,   9750,
     9282,  10817,   9750,   7649,
     9282,   7649,   9750,  10817,
    },
    {
     1722,   2007,   2412,   3706,
     9472,   7805,   9950,  11038,
     2007,   2729,   3045,   6064,
    11038,   9096,  11595,  12864,
     2412,   3045,   5912,   8199,
     9950,   8199,  10452,  11595,
     3706,   6064,   8199,   6432,
     7805,   6432,   8199,   9096,
     9472,  11038,   9950,   7805,
     9472,   7805,   9950,  11038,
     7805,   9096,   8199,   6432,
     7805,   6432,   8199,   9096,
     9950,  11595,  10452,   8199,
     9950,   8199,  10452,  11595,
    11038,  12864,  11595,   9096,
    11038,   9096,  11595,  12864,
    },
};
static const uint16_t new_inter_qmat[4][64] = {
    {
     1024,   1193,   1076,    844,
     1052,    914,   1225,   1492,
     1193,   1391,   1254,    983,
     1227,   1065,   1463,   1816,
     1076,   1254,   1161,    936,
     1195,   1034,   1444,   1741,
      844,    983,    936,    811,
     1055,    927,   1305,   1584,
     1052,   1227,   1195,   1055,
     1451,   1336,   1912,   2354,
      914,   1065,   1034,    927,
     1336,   1313,   1945,   2486,
     1225,   1463,   1444,   1305,
     1912,   1945,   3044,   4039,
     1492,   1816,   1741,   1584,
     2354,   2486,   4039,   5679,
    },
    {
     1218,   1419,   1279,   1003,
     1252,   1087,   1457,   1774,
     1419,   1654,   1491,   1169,
     1459,   1267,   1739,   2159,
     1279,   1491,   1381,   1113,
     1421,   1230,   1717,   2070,
     1003,   1169,   1113,    965,
     1254,   1103,   1552,   1884,
     1252,   1459,   1421,   1254,
     1725,   1589,   2274,   2799,
     1087,   1267,   1230,   1103,
     1589,   1562,   2313,   2956,
     1457,   1739,   1717,   1552,
     2274,   2313,   3620,   4803,
     1774,   2159,   2070,   1884,
     2799,   2956,   4803,   6753,
    },
    {
     1448,   1688,   1521,   1193,
     1488,   1293,   1732,   2110,
     1688,   1967,   1773,   1391,
     1735,   1507,   2068,   2568,
     1521,   1773,   1642,   1323,
     1690,   1462,   2042,   2462,
     1193,   1391,   1323,   1147,
     1492,   1311,   1845,   2241,
     1488,   1735,   1690,   1492,
     2052,   1889,   2704,   3328,
     1293,   1507,   1462,   1311,
     1889,   1857,   2751,   3515,
     1732,   2068,   2042,   1845,
     2704,   2751,   4306,   5712,
     2110,   2568,   2462,   2241,
     3328,   3515,   5712,   8031,
    },
    {
     1722,   2007,   1809,   1419,
     1770,   1537,   2060,   2509,
     2007,   2339,   2108,   1654,
     2063,   1792,   2460,   3054,
     1809,   2108,   1953,   1574,
     2010,   1739,   2428,   2928,
     1419,   1654,   1574,   1364,
     1774,   1559,   2195,   2664,
     1770,   2063,   2010,   1774,
     2440,   2247,   3216,   3958,
     1537,   1792,   1739,   1559,
     2247,   2209,   3271,   4181,
     2060,   2460,   2428,   2195,
     3216,   3271,   5120,   6793,
     2509,   3054,   2928,   2664,
     3958,   4181,   6793,   9550,
    },
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
    int dq = bink2_bits_read_unary(bits, 1, 4);

    if (dq < 0)
        return -1;
    if (dq == 3) {
        int bit = bink2_bits_read1(bits);

        if (bit < 0)
            return -1;
        dq += bit;
    } else if (dq == 4) {
        int extra = bink2_bits_read(bits, 5);

        if (extra < 0)
            return -1;
        dq += extra + 1;
    }
    if (dq) {
        int sign = bink2_bits_read1(bits);

        if (sign < 0)
            return -1;
        if (sign)
            dq = -dq;
    }
    return dq;
}

static unsigned new_cbp_luma(Bink2Video *v, Bink2Bits *bits,
                             unsigned prev_cbp)
{
    unsigned cbp = 0, mask = 0;
    int ones = popcount16(prev_cbp);
    int bit, i;

    if (ones > 7) {
        ones = 16 - ones;
        mask = 0xFFFF;
    }
    bit = bink2_bits_read1(bits);
    if (bit < 0)
        return 0;
    if (bit == 0) {
        if (ones < 4) {
            for (i = 0; i < 16; i += 4) {
                bit = bink2_bits_read1(bits);
                if (bit < 0)
                    return 0;
                if (!bit) {
                    int value = bink2_bits_read(bits, 4);

                    if (value < 0)
                        return 0;
                    cbp |= (unsigned)value << i;
                }
            }
        } else {
            int value = bink2_bits_read(bits, 16);

            if (value < 0)
                return 0;
            cbp = (unsigned)value;
        }
    }
    cbp ^= mask;
    if (!(v->frame_flags & 0x40000) || cbp) {
        bit = bink2_bits_read1(bits);
        if (bit < 0)
            return 0;
        if (bit)
            cbp |= cbp << 16;
    }
    return cbp;
}

static unsigned new_cbp_chroma(Bink2Bits *bits, unsigned prev_cbp)
{
    unsigned cbp = (prev_cbp & 0xF0000u) |
                   new_chroma_cbp_pat[prev_cbp & 0xF];
    int bit = bink2_bits_read1(bits);

    if (bit < 0)
        return 0;
    if (bit == 0) {
        int value = bink2_bits_read(bits, 4);

        if (value < 0)
            return 0;
        cbp = (unsigned)value;
        bit = bink2_bits_read1(bits);
        if (bit < 0)
            return 0;
        if (bit)
            cbp |= cbp << 16;
    }
    return cbp;
}

static int new_decode_dc(Bink2Video *v, Bink2Bits *bits, int is_luma,
                         int q, int mindc, int maxdc, int flags)
{
    float tdc[16] = { 0 };
    int num_dc = is_luma ? 16 : 4;
    int pat, i;

    q = q < 8 ? 8 : q;
    pat = new_dc_pat[q];

    if (bink2_bits_read1(bits) == 1) {
        for (i = 0; i < num_dc; i++) {
            int cnt = bink2_bits_read_unary(bits, 0, 12);

            if (cnt < 0)
                return -1;
            if (cnt > 3)
                cnt = (1 << (cnt - 3)) +
                      bink2_bits_read(bits, cnt - 3) + 2;
            if (cnt) {
                int sign = bink2_bits_read1(bits);

                if (sign < 0)
                    return -1;
                if (sign)
                    cnt = -cnt;
            }
            tdc[i] = (float)((cnt * pat + 0x200) >> 10);
        }
    }
    bink2_video_predict_dc(v, is_luma, (float)mindc, (float)maxdc, flags,
                           tdc);
    return 0;
}

static int new_decode_ac(Bink2Bits *bits, int16_t block[4][64],
                         unsigned cbp, int q, const uint16_t qmat[4][64])
{
    const uint16_t *skip_codes;
    const uint8_t *skip_bits;
    int i;

    for (i = 0; i < 4; i++)
        memset(block[i], 0, 64 * sizeof(int16_t));
    if ((cbp & 0xF) == 0)
        return 0;

    if (cbp & 0xFFFF0000u) {
        skip_codes = new_ac_skip_codes[1];
        skip_bits = new_ac_skip_bits[1];
    } else {
        skip_codes = new_ac_skip_codes[0];
        skip_bits = new_ac_skip_bits[0];
    }

    for (i = 0; i < 4; i++, cbp >>= 1) {
        int next = 0;
        int idx = 1;

        if (!(cbp & 1))
            continue;
        while (idx < 64) {
            int val;

            next--;
            if (next < 1) {
                int skip = new_read_vlc(bits, skip_codes, skip_bits, 14);

                if (skip < 0)
                    return -1;
                next = bink2_next_skips[skip];
                skip = new_skips[skip];
                if (skip == 11) {
                    skip = bink2_bits_read(bits, 6);
                    if (skip < 0)
                        return -1;
                }
                idx += skip;
                if (idx >= 64)
                    break;
            }

            val = bink2_bits_read_unary(bits, 0, 12) + 1;
            if (val < 1)
                return -1;
            if (val > 3) {
                int nbits = val - 3;

                val = bink2_bits_read(bits, nbits) + (1 << nbits) + 2;
                if (val < 0)
                    return -1;
            }
            {
                int sign = bink2_bits_read1(bits);
                int64_t level;

                if (sign < 0)
                    return -1;
                if (sign)
                    val = -val;
                level = (int64_t)val * qmat[q & 3][new_scan[idx]] *
                        (1 << (q >> 2));
                block[i][new_scan[idx]] =
                    (int16_t)((level + 64) >> 7);
            }
            idx++;
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

    cbp = new_cbp_luma(v, bits, *prev_cbp);
    *prev_cbp = cbp;
    if (new_decode_dc(v, bits, 1, q, 0, 2047, flags) != 0)
        return -1;

    for (i = 0; i < 4; i++) {
        if (new_decode_ac(bits, block, cbp >> (4 * i), q,
                          new_luma_intra_qmat) != 0)
            return -1;
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

    cbp = new_cbp_chroma(bits, *prev_cbp);
    *prev_cbp = cbp;
    if (new_decode_dc(v, bits, 0, q, 0, 2047, flags) != 0)
        return -1;
    if (new_decode_ac(bits, block, cbp, q, new_chroma_intra_qmat) != 0)
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

static int new_get_type(Bink2Bits *bits, int lru[4])
{
    int mode = bink2_bits_read_unary(bits, 1, 3);
    int value;

    if (mode < 0)
        return -1;
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

static int new_decode_mv(Bink2Bits *bits, int mv[4][2], int *nb)
{
    int num = bink2_bits_read1(bits);
    int i, j;

    if (num < 0)
        return -1;
    num = num ? 1 : 4;
    *nb = num;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < num; j++) {
            int val = new_read_vlc(bits, new_mv_codes, new_mv_bits, 16);

            if (val < 0)
                return -1;
            if (val >= 8 && val != 15)
                val -= 15;
            if (val == 15) {
                int nbits = bink2_bits_read_unary(bits, 1, 12) + 4;
                int raw;

                if (nbits < 0)
                    return -1;
                raw = bink2_bits_read(bits, nbits);
                if (raw < 0)
                    return -1;
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

    cbp = new_cbp_luma(v, bits, *prev_cbp);
    *prev_cbp = cbp;
    (void)flags;
    if (new_decode_dc(v, bits, 1, q, -1023, 1023, 0xA8) != 0)
        return -1;

    for (i = 0; i < 4; i++) {
        if (new_decode_ac(bits, block, cbp >> (4 * i), q,
                          new_inter_qmat) != 0)
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

    cbp = new_cbp_chroma(bits, *prev_cbp);
    *prev_cbp = cbp;
    if (new_decode_dc(v, bits, 0, q, -1023, 1023, 0xA8) != 0)
        return -1;
    if (new_decode_ac(bits, block, cbp, q, new_inter_qmat) != 0)
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

    if (!is_keyframe && !v->has_prev)
        return BINK2_ERROR;

    memset(v->new_q_prev, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2Quant));
    memset(v->old_mv_prev, 0, ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

    for (y = y0; y < y1; y += 32) {
        int types_lru[4] = { 2, 3, 1, 0 };
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
                type = new_get_type(bits, types_lru);
                if (type < 0)
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
                update_intra_q(v, intra_q, dq, flags);
                q = *intra_q;
                if (q < 0 || q >= 37)
                    return BINK2_ERROR;

                v->old_comp = 0;
                if (new_decode_intra_luma(v, bits, &y_cbp_intra, q,
                                          v->cur[0] +
                                          (size_t)y * v->stride[0] + x,
                                          v->stride[0], flags) != 0)
                    return BINK2_ERROR;
                v->old_comp = 1;
                if (new_decode_intra_chroma(v, bits, &u_cbp_intra, q,
                                            v->cur[2] +
                                            (size_t)(y / 2) * v->stride[2] + x / 2,
                                            v->stride[2], flags) != 0)
                    return BINK2_ERROR;
                v->old_comp = 2;
                if (new_decode_intra_chroma(v, bits, &v_cbp_intra, q,
                                            v->cur[1] +
                                            (size_t)(y / 2) * v->stride[1] + x / 2,
                                            v->stride[1], flags) != 0)
                    return BINK2_ERROR;
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
                if (new_decode_mv(bits, raw, &nb) != 0)
                    return BINK2_ERROR;
                new_predict_mv(v, flags, raw, nb);
                bink2_video_mc_luma(v, x, y);
                bink2_video_mc_chroma(v, x, y);
                bink2_video_store_motion(v, x, y, nb);
                break;
            case 3:
                update_intra_q(v, intra_q, 0, flags);
                if (new_decode_mv(bits, raw, &nb) != 0)
                    return BINK2_ERROR;
                new_predict_mv(v, flags, raw, nb);
                dq = new_decode_dq(bits);
                update_inter_q(v, inter_q, dq, flags);
                q = *inter_q;
                if (q < 0 || q >= 37)
                    return BINK2_ERROR;
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
