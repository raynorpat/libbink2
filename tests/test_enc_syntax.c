#include "bink2_bits.h"
#include "bink2_tables.h"
#include "bink2_enc_syntax.h"
#include "bink2_video_new.h"

#include "test_common.h"

#include <string.h>

static Bink2Bits rewind_bits(Bink2BitWriter *bw)
{
    Bink2Bits bits;

    bink2_bits_init(&bits, bink2_bw_data(bw), bink2_bw_bytes(bw));
    return bits;
}

static int test_btype(void)
{
    static const int order[4] = { 2, 3, 1, 0 };
    int k;

    for (k = 0; k < 4; k++) {
        Bink2BitWriter bw;
        Bink2Bits bits;
        uint8_t writer_lru[4] = { 2, 3, 1, 0 };
        uint8_t reader_lru[4] = { 2, 3, 1, 0 };
        int type = order[k];

        bink2_bw_init(&bw);
        CHECK(bink2_btype_bits(writer_lru, type) > 0);
        bink2_write_btype(&bw, writer_lru, type);
        CHECK(bink2_bw_bitpos(&bw) ==
              (size_t)bink2_btype_bits((const uint8_t[4]){ 2, 3, 1, 0 },
                                       type));
        bits = rewind_bits(&bw);
        CHECK(bink2_new_read_btype(&bits, reader_lru) == type);
        CHECK(bink2_bits_failed(&bits) == 0);
        CHECK(memcmp(writer_lru, reader_lru, 4) == 0);
        bink2_bw_free(&bw);
    }
    return 0;
}

static int test_quant(void)
{
    static const int prevs[3] = { 8, 16, 28 };
    int p, d;

    for (p = 0; p < 3; p++) {
        for (d = -8; d <= 8; d++) {
            Bink2BitWriter bw;
            Bink2Bits bits;
            int q = prevs[p] + d;

            bink2_bw_init(&bw);
            bink2_write_quant(&bw, prevs[p], q);
            bits = rewind_bits(&bw);
            CHECK(bink2_new_read_quant(&bits, prevs[p]) == q);
            CHECK(bink2_bits_failed(&bits) == 0);
            bink2_bw_free(&bw);
        }
    }
    return 0;
}

static int test_cbp_luma(void)
{
    static const uint32_t patterns[5] = {
        0x0000, 0xFFFF, 0x00FF, 0x1234, 0x8001
    };
    static const uint32_t prevs[2] = { 0xFFFF, 0x00FF };
    int p, i;

    for (p = 0; p < 2; p++) {
        for (i = 0; i < 5; i++) {
            Bink2BitWriter bw;
            Bink2Bits bits;

            bink2_bw_init(&bw);
            bink2_write_cbp_luma(&bw, prevs[p], 0, patterns[i]);
            bits = rewind_bits(&bw);
            CHECK(bink2_new_read_cbp_luma(&bits, prevs[p], 0) ==
                  patterns[i]);
            CHECK(bink2_bits_failed(&bits) == 0);
            bink2_bw_free(&bw);
        }
        {
            Bink2BitWriter bw;
            Bink2Bits bits;

            /* With edge set, a zero pattern omits the repeat bit. */
            bink2_bw_init(&bw);
            bink2_write_cbp_luma(&bw, prevs[p], 1, 0);
            bits = rewind_bits(&bw);
            CHECK(bink2_new_read_cbp_luma(&bits, prevs[p], 1) == 0);
            CHECK(bink2_bits_failed(&bits) == 0);
            bink2_bw_free(&bw);
        }
    }
    return 0;
}

static int test_cbp_chroma(void)
{
    static const uint32_t prevs[2] = { 0x0, 0xF };
    int p;
    uint32_t cbp;

    for (p = 0; p < 2; p++) {
        for (cbp = 0; cbp < 16; cbp++) {
            Bink2BitWriter bw;
            Bink2Bits bits;

            bink2_bw_init(&bw);
            bink2_write_cbp_chroma(&bw, prevs[p], cbp);
            bits = rewind_bits(&bw);
            CHECK(bink2_new_read_cbp_chroma(&bits, prevs[p]) == cbp);
            CHECK(bink2_bits_failed(&bits) == 0);
            bink2_bw_free(&bw);
        }
    }
    return 0;
}

static int test_dcs(void)
{
    /* The DC reader clamps q to 8, so its step is bink2g_dc_pat[8]/1024
     * = 4. Values that are multiples of 4 round-trip exactly. */
    static const int32_t values[16] = {
        0, 4, -4, 8, -8, 12, -12, 16,
        -16, 20, -20, 36, -36, 100, -100, 512
    };
    Bink2BitWriter bw;
    Bink2Bits bits;
    int32_t out[16];

    bink2_bw_init(&bw);
    bink2_write_dcs(&bw, values, 16, 0);
    bits = rewind_bits(&bw);
    bink2_new_read_dcs(&bits, out, 16, 0);
    CHECK(bink2_bits_failed(&bits) == 0);
    CHECK(memcmp(out, values, sizeof(values)) == 0);
    bink2_bw_free(&bw);

    /* All-zero DCs take the single-bit shortcut. */
    memset(out, 0, sizeof(out));
    bink2_bw_init(&bw);
    bink2_write_dcs(&bw, out, 16, 0);
    CHECK(bink2_bw_bitpos(&bw) == 1);
    bits = rewind_bits(&bw);
    bink2_new_read_dcs(&bits, out, 16, 0);
    CHECK(bink2_bits_failed(&bits) == 0);
    CHECK(out[0] == 0 && out[15] == 0);
    bink2_bw_free(&bw);
    return 0;
}

static int ac_roundtrip(const int32_t in[64], uint32_t cbp)
{
    Bink2BitWriter bw;
    Bink2Bits bits;
    int32_t out[64];

    bink2_bw_init(&bw);
    bink2_write_acs(&bw, in, bink2g_luma_intra_qmat, 0, cbp);
    bits = rewind_bits(&bw);
    memset(out, 0, sizeof(out));
    bink2_new_read_acs(&bits, out, bink2g_luma_intra_qmat, 0, cbp);
    CHECK(bink2_bits_failed(&bits) == 0);
    CHECK(memcmp(out, in, sizeof(out)) == 0);
    bink2_bw_free(&bw);
    return 0;
}

static int test_acs(void)
{
    int32_t levels[64];

    memset(levels, 0, sizeof(levels));
    CHECK(ac_roundtrip(levels, 1) == 0);

    memset(levels, 0, sizeof(levels));
    levels[bink2g_scan[1]] = 5;
    CHECK(ac_roundtrip(levels, 1) == 0);

    memset(levels, 0, sizeof(levels));
    levels[bink2g_scan[1]] = -3;
    levels[bink2g_scan[9]] = 7;
    levels[bink2g_scan[10]] = -1;
    CHECK(ac_roundtrip(levels, 1) == 0);

    memset(levels, 0, sizeof(levels));
    levels[bink2g_scan[1]] = 1;
    levels[bink2g_scan[63]] = 100;
    CHECK(ac_roundtrip(levels, 1) == 0);

    /* A coefficient at scan position 63 ends the reader's loop without a
     * terminator; the writer must not emit one. */
    {
        Bink2BitWriter bw;
        Bink2Bits bits;
        int32_t out[64];

        bink2_bw_init(&bw);
        bink2_write_acs(&bw, levels, bink2g_luma_intra_qmat, 0, 1);
        bits = rewind_bits(&bw);
        bink2_new_read_acs(&bits, out, bink2g_luma_intra_qmat, 0, 1);
        CHECK(bink2_bits_failed(&bits) == 0);
        CHECK(bits.bitpos == bink2_bw_bitpos(&bw));
        bink2_bw_free(&bw);
    }

    memset(levels, 0, sizeof(levels));
    levels[bink2g_scan[1]] = 1025;
    CHECK(ac_roundtrip(levels, 1) == 0);

    /* The high bit selects the second skip table. */
    memset(levels, 0, sizeof(levels));
    levels[bink2g_scan[2]] = 4;
    levels[bink2g_scan[20]] = -6;
    CHECK(ac_roundtrip(levels, 0x10001u) == 0);

    /* An uncoded block consumes no bits. */
    {
        Bink2BitWriter bw;

        bink2_bw_init(&bw);
        bink2_write_acs(&bw, levels, bink2g_luma_intra_qmat, 0, 0);
        CHECK(bink2_bw_bitpos(&bw) == 0);
        bink2_bw_free(&bw);
    }
    return 0;
}

static int test_mv(void)
{
    static const int pairs[3][2] = { { -64, 63 }, { 0, 0 }, { 255, -255 } };
    int i;

    for (i = 0; i < 3; i++) {
        Bink2BitWriter bw;
        Bink2Bits bits;
        int mv[4][2] = { { 0 } };
        int nb = 0;

        bink2_bw_init(&bw);
        bink2_write_mv1(&bw, pairs[i][0], pairs[i][1]);
        bits = rewind_bits(&bw);
        CHECK(bink2_new_read_mv(&bits, mv, &nb) == 0);
        CHECK(bink2_bits_failed(&bits) == 0);
        CHECK(nb == 1);
        CHECK(mv[0][0] == pairs[i][0] && mv[0][1] == pairs[i][1]);
        bink2_bw_free(&bw);
    }

    {
        Bink2BitWriter bw;
        Bink2Bits bits;
        int in[4][2] = { { -64, 63 }, { 0, 0 }, { 255, -255 }, { -1, 7 } };
        int out[4][2];
        int nb = 0;

        bink2_bw_init(&bw);
        bink2_write_mv4(&bw, in);
        bits = rewind_bits(&bw);
        CHECK(bink2_new_read_mv(&bits, out, &nb) == 0);
        CHECK(bink2_bits_failed(&bits) == 0);
        CHECK(nb == 4);
        CHECK(memcmp(in, out, sizeof(in)) == 0);
        bink2_bw_free(&bw);
    }
    return 0;
}

int main(void)
{
    CHECK(test_btype() == 0);
    CHECK(test_quant() == 0);
    CHECK(test_cbp_luma() == 0);
    CHECK(test_cbp_chroma() == 0);
    CHECK(test_dcs() == 0);
    CHECK(test_acs() == 0);
    CHECK(test_mv() == 0);
    return 0;
}
