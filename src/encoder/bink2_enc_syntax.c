#include "bink2_enc_syntax.h"
#include "bink2_tables.h"

#include <stdlib.h>

static void put_vlc(Bink2BitWriter *bw, const uint16_t *codes,
                    const uint8_t *lengths, int index)
{
    bink2_bw_put(bw, codes[index], lengths[index]);
}

static void put_ones_then_zero(Bink2BitWriter *bw, int ones)
{
    if (ones > 0)
        bink2_bw_put(bw, (1u << ones) - 1u, ones);
    bink2_bw_put(bw, 0, 1);
}

void bink2_write_btype(Bink2BitWriter *bw, uint8_t lru[4], int type)
{
    int mode;

    if (type == lru[0]) {
        mode = 0;
    } else if (type == lru[1]) {
        mode = 1;
    } else if (type == lru[3]) {
        mode = 2;
    } else {
        mode = 3;
    }

    switch (mode) {
    case 0:
        bink2_bw_put(bw, 1, 1);
        break;
    case 1:
        bink2_bw_put(bw, 0, 1);
        bink2_bw_put(bw, 1, 1);
        lru[0] ^= lru[1];
        lru[1] ^= lru[0];
        lru[0] ^= lru[1];
        break;
    case 2:
        bink2_bw_put(bw, 0, 1);
        bink2_bw_put(bw, 0, 1);
        bink2_bw_put(bw, 1, 1);
        lru[2] ^= lru[3];
        lru[3] ^= lru[2];
        lru[2] ^= lru[3];
        break;
    default:
        bink2_bw_put(bw, 0, 3);
        lru[1] ^= lru[2];
        lru[2] ^= lru[1];
        lru[1] ^= lru[2];
        break;
    }
}

int bink2_btype_bits(const uint8_t lru[4], int type)
{
    if (type == lru[0])
        return 1;
    if (type == lru[1])
        return 2;
    return 3;
}

void bink2_write_quant(Bink2BitWriter *bw, int prev_q, int q)
{
    int delta = q - prev_q;
    int mag = delta < 0 ? -delta : delta;
    int negative = delta < 0;

    if (mag == 0) {
        bink2_bw_put(bw, 1, 1);
        return;
    }
    if (mag <= 3) {
        bink2_bw_put(bw, 0, mag);
        bink2_bw_put(bw, 1, 1);
        if (mag == 3)
            bink2_bw_put(bw, 0, 1);
    } else if (mag == 4) {
        bink2_bw_put(bw, 0, 3);
        bink2_bw_put(bw, 1, 1);
        bink2_bw_put(bw, 1, 1);
    } else {
        bink2_bw_put(bw, 0, 4);
        bink2_bw_put(bw, (unsigned)(mag - 5), 5);
    }
    bink2_bw_put(bw, negative ? 1u : 0u, 1);
}

static int popcount16(uint32_t value)
{
    int count = 0;

    value &= 0xFFFF;
    while (value) {
        count += (int)(value & 1);
        value >>= 1;
    }
    return count;
}

/* The format can represent cbp high halves of 0 or lo; callers only pass
 * those (the encoder always uses lo == 0xFFFF). */
void bink2_write_cbp_luma(Bink2BitWriter *bw, uint32_t prev_cbp,
                          uint32_t edge, uint32_t cbp)
{
    uint32_t prev_lo = prev_cbp & 0xFFFF;
    uint32_t hi = cbp >> 16;
    uint32_t lo = cbp & 0xFFFF;
    uint32_t mask = 0;
    uint32_t raw;
    int ones = popcount16(prev_lo);
    int replicate;

    if (hi == 0) {
        replicate = 0;
    } else {
        /* The format stores either hi == 0 or hi == lo. */
        replicate = 1;
    }
    if (ones > 7) {
        ones = 16 - ones;
        mask = 0xFFFF;
    }
    raw = lo ^ mask;

    if (raw == 0) {
        bink2_bw_put(bw, 1, 1);
    } else {
        bink2_bw_put(bw, 0, 1);
        if (ones < 4) {
            int i;

            for (i = 0; i < 4; i++) {
                uint32_t nibble = (raw >> (4 * i)) & 0xF;

                if (nibble == 0) {
                    bink2_bw_put(bw, 1, 1);
                } else {
                    bink2_bw_put(bw, 0, 1);
                    bink2_bw_put(bw, nibble, 4);
                }
            }
        } else {
            bink2_bw_put(bw, raw, 16);
        }
    }
    if (!edge || lo)
        bink2_bw_put(bw, replicate ? 1u : 0u, 1);
}

void bink2_write_cbp_chroma(Bink2BitWriter *bw, uint32_t prev_cbp,
                            uint32_t cbp)
{
    static const uint8_t pattern[16] = {
        0x0, 0x0, 0x0, 0xF,
        0x0, 0xF, 0xF, 0xF,
        0x0, 0xF, 0xF, 0xF,
        0xF, 0xF, 0xF, 0xF
    };
    uint32_t initial = (prev_cbp & 0xF0000u) | pattern[prev_cbp & 0xF];

    if (cbp == initial) {
        bink2_bw_put(bw, 1, 1);
        return;
    }
    bink2_bw_put(bw, 0, 1);
    bink2_bw_put(bw, cbp & 0xF, 4);
    bink2_bw_put(bw, (cbp >> 16) != 0 ? 1u : 0u, 1);
}

void bink2_write_dcs(Bink2BitWriter *bw, const int32_t dcs[16], int count,
                     int q)
{
    int pat;
    int any = 0;
    int i;

    for (i = 0; i < count; i++)
        if (dcs[i] != 0)
            any = 1;
    if (!any) {
        bink2_bw_put(bw, 0, 1);
        return;
    }
    bink2_bw_put(bw, 1, 1);
    q = q < 8 ? 8 : q;
    pat = bink2g_dc_pat[q];
    for (i = 0; i < count; i++) {
        int cnt = (int)(((int64_t)dcs[i] * 1024 + (dcs[i] >= 0 ? pat / 2
                                                               : -pat / 2))
                        / pat);
        int mag = cnt < 0 ? -cnt : cnt;
        int negative = cnt < 0;

        if (mag <= 3) {
            put_ones_then_zero(bw, mag);
        } else {
            int nbits = 0;
            int extra;

            while ((1 << (nbits + 1)) <= mag - 2)
                nbits++;
            put_ones_then_zero(bw, nbits + 3);
            extra = mag - 2 - (1 << nbits);
            bink2_bw_put(bw, (unsigned)extra, nbits);
        }
        if (mag)
            bink2_bw_put(bw, negative ? 1u : 0u, 1);
    }
}

static void write_skip(Bink2BitWriter *bw, const uint16_t *codes,
                       const uint8_t *lengths, int delta)
{
    if (delta >= 64) {
        put_vlc(bw, codes, lengths, 12);
        return;
    }
    if (delta <= 10) {
        put_vlc(bw, codes, lengths, delta);
        return;
    }
    put_vlc(bw, codes, lengths, 11);
    bink2_bw_put(bw, (unsigned)delta, 6);
}

static void write_ac_value(Bink2BitWriter *bw, int value)
{
    int mag = value < 0 ? -value : value;
    int negative = value < 0;

    if (mag <= 3) {
        put_ones_then_zero(bw, mag - 1);
    } else {
        int nbits = 0;
        int extra;

        while ((1 << (nbits + 1)) <= mag - 2)
            nbits++;
        put_ones_then_zero(bw, nbits + 2);
        extra = mag - 2 - (1 << nbits);
        bink2_bw_put(bw, (unsigned)extra, nbits);
    }
    bink2_bw_put(bw, negative ? 1u : 0u, 1);
}

void bink2_write_acs(Bink2BitWriter *bw, const int32_t levels[64],
                     const uint16_t qmat[4][64], int q, uint32_t cbp)
{
    const uint16_t *skip_codes;
    const uint8_t *skip_bits;
    int idx = 1;
    int s;

    (void)qmat;
    (void)q;
    if (!(cbp & 1))
        return;

    if (cbp & 0xFFFF0000u) {
        skip_codes = bink2g_ac_skip_codes[1];
        skip_bits = bink2g_ac_skip_bits[1];
    } else {
        skip_codes = bink2g_ac_skip_codes[0];
        skip_bits = bink2g_ac_skip_bits[0];
    }

    for (s = 1; s < 64; s++) {
        int value = levels[bink2g_scan[s]];

        if (value == 0)
            continue;
        write_skip(bw, skip_codes, skip_bits, s - idx);
        write_ac_value(bw, value);
        idx = s + 1;
    }
    /* If the last coded coefficient is scan position 63 the reader's loop
     * ends without consuming a terminator. */
    if (idx < 64)
        write_skip(bw, skip_codes, skip_bits, 64);
}

static int small_mv_index(int value)
{
    if (value >= 0 && value <= 7)
        return value;
    if (value >= -7 && value <= -1)
        return value + 15;
    return -1;
}

static void write_mv_value(Bink2BitWriter *bw, int value)
{
    int index = small_mv_index(value);
    int nbits;
    int raw = 0;
    int found = 0;

    if (index >= 0) {
        put_vlc(bw, bink2g_mv_codes, bink2g_mv_bits, index);
        return;
    }
    put_vlc(bw, bink2g_mv_codes, bink2g_mv_bits, 15);
    for (nbits = 4; nbits <= 16; nbits++) {
        if (value >= 0) {
            raw = 2 * value - (1 << nbits) + 1;
        } else {
            raw = -2 * value - (1 << nbits);
        }
        if (raw >= 0 && raw < (1 << nbits)) {
            found = 1;
            break;
        }
    }
    if (!found) {
        /* Out-of-range vectors do not occur in the encoder. */
        raw = 0;
        nbits = 4;
    }
    bink2_bw_put(bw, 0, nbits - 4);
    bink2_bw_put(bw, 1, 1);
    bink2_bw_put(bw, (unsigned)raw, nbits);
}

void bink2_write_mv1(Bink2BitWriter *bw, int x, int y)
{
    bink2_bw_put(bw, 1, 1);
    write_mv_value(bw, x);
    write_mv_value(bw, y);
}

void bink2_write_mv4(Bink2BitWriter *bw, const int mv[4][2])
{
    int i;

    bink2_bw_put(bw, 0, 1);
    for (i = 0; i < 4; i++)
        write_mv_value(bw, mv[i][0]);
    for (i = 0; i < 4; i++)
        write_mv_value(bw, mv[i][1]);
}
