/* Unit tests for the RAD post-reconstruction filter stages. Expected values
 * are the worked examples of the approved behaviour notes. */
#include "bink2_filter.h"
#include "test_common.h"

#define BUF_W 12
#define BUF_H 12
#define SENTINEL 0x5a

/* The edge sits between buffer columns (or rows) 5 and 6, so the footprint
 * spans 4..7 and every other byte must keep the sentinel. Lines run 2..9. */
#define EDGE_POS 6
#define LINE0 2

static void fill_edge(unsigned char *buf, int vertical,
                      const unsigned char in[4])
{
    int line, k;

    memset(buf, SENTINEL, BUF_W * BUF_H);
    for (line = 0; line < 8; line++)
        for (k = 0; k < 4; k++) {
            int across = EDGE_POS - 2 + k;
            int y = vertical ? LINE0 + line : across;
            int x = vertical ? across : LINE0 + line;

            buf[y * BUF_W + x] = in[k];
        }
}

/* Runs the primitive on an 8-line edge in both orientations and compares
 * every byte of the buffer with the expected taps and the sentinel. */
static int edge_case(const unsigned char in[4], unsigned mask,
                     const unsigned char out[4])
{
    unsigned char buf[BUF_W * BUF_H];
    unsigned char want[BUF_W * BUF_H];
    int vertical;

    for (vertical = 0; vertical < 2; vertical++) {
        unsigned char *q0;

        fill_edge(buf, vertical, in);
        fill_edge(want, vertical, out);
        if (vertical) {
            q0 = buf + LINE0 * BUF_W + EDGE_POS;
            bink2_filter_db_edge(q0, 1, BUF_W, mask);
        } else {
            q0 = buf + EDGE_POS * BUF_W + LINE0;
            bink2_filter_db_edge(q0, BUF_W, 1, mask);
        }
        CHECK(memcmp(buf, want, sizeof(buf)) == 0);
    }
    return 0;
}

static int deblock_example_1(void)
{
    static const unsigned char in[4] = { 100, 100, 120, 120 };
    static const unsigned char out[4] = { 103, 105, 115, 117 };

    CHECK(edge_case(in, BINK2_DB_TAPS_ALL, out) == 0);
    return 0;
}

static int deblock_example_2(void)
{
    static const unsigned char in[4] = { 120, 120, 100, 100 };
    static const unsigned char out[4] = { 118, 115, 105, 102 };

    CHECK(edge_case(in, BINK2_DB_TAPS_ALL, out) == 0);
    return 0;
}

static int deblock_example_3(void)
{
    static const unsigned char d1[4] = { 100, 100, 101, 101 };
    static const unsigned char dm1[4] = { 100, 100, 99, 99 };
    static const unsigned char dm2[4] = { 100, 100, 98, 98 };
    static const unsigned char d2[4] = { 100, 100, 102, 102 };
    static const unsigned char d2_out[4] = { 100, 101, 101, 102 };

    CHECK(edge_case(d1, BINK2_DB_TAPS_ALL, d1) == 0);
    CHECK(edge_case(dm1, BINK2_DB_TAPS_ALL, dm1) == 0);
    CHECK(edge_case(dm2, BINK2_DB_TAPS_ALL, dm2) == 0);
    CHECK(edge_case(d2, BINK2_DB_TAPS_ALL, d2_out) == 0);
    return 0;
}

static int deblock_example_4(void)
{
    static const unsigned char in[4] = { 255, 0, 200, 0 };
    static const unsigned char out[4] = { 255, 50, 150, 0 };

    CHECK(edge_case(in, BINK2_DB_TAPS_ALL, out) == 0);
    return 0;
}

static int deblock_example_5(void)
{
    static const unsigned char in[4] = { 100, 100, 120, 120 };
    static const unsigned char out[4] = { 100, 105, 115, 120 };
    unsigned mask = bink2_filter_db_class_mask(1, 1);

    CHECK(mask == (BINK2_DB_TAP_P0 | BINK2_DB_TAP_Q0));
    CHECK(edge_case(in, mask, out) == 0);
    return 0;
}

static int deblock_example_6(void)
{
    static const unsigned char in[4] = { 100, 100, 120, 120 };
    unsigned mask = bink2_filter_db_class_mask(3, 3);

    CHECK(mask == 0);
    CHECK(edge_case(in, mask, in) == 0);
    return 0;
}

static int deblock_example_7(void)
{
    static const unsigned char in[4] = { 100, 100, 120, 120 };
    static const unsigned char full[4] = { 103, 105, 115, 117 };

    CHECK(!bink2_filter_mv_differ(4, 0, 5, 1));
    CHECK(bink2_filter_mv_differ(4, 0, 6, 0));
    CHECK(bink2_filter_mv_differ(0, 4, 0, 6));
    CHECK(bink2_filter_mv_differ(6, 0, 4, 0));
    CHECK(edge_case(in, bink2_filter_mv_differ(4, 0, 5, 1)
                    ? BINK2_DB_TAPS_ALL : 0, in) == 0);
    CHECK(edge_case(in, bink2_filter_mv_differ(4, 0, 6, 0)
                    ? BINK2_DB_TAPS_ALL : 0, full) == 0);
    return 0;
}

/* The notes' mask table, (p1, p0, q0, q1) for n = class(P) | class(Q) << 2.
 * Entry 15 is listed as 1111, but rule C leaves an n == 15 edge alone. */
static int class_mask_matches_table(void)
{
    static const char *const table[16] = {
        "0000", "0100", "1100", "1100", "0010", "0110", "1110", "1110",
        "0011", "0111", "1111", "1111", "0011", "0111", "1111", "1111"
    };
    static const unsigned bits[4] = {
        BINK2_DB_TAP_P1, BINK2_DB_TAP_P0, BINK2_DB_TAP_Q0, BINK2_DB_TAP_Q1
    };
    int n, k;

    for (n = 0; n < 16; n++) {
        unsigned want = 0;

        for (k = 0; k < 4; k++)
            if (table[n][k] == '1')
                want |= bits[k];
        if (n == 15)
            want = 0;
        CHECK(bink2_filter_db_class_mask(n & 3, n >> 2) == want);
    }
    CHECK(BINK2_DB_TAPS_ALL == (BINK2_DB_TAP_P1 | BINK2_DB_TAP_P0 |
                                BINK2_DB_TAP_Q0 | BINK2_DB_TAP_Q1));
    return 0;
}

/* Coverage table of the notes: KB2f, g, h, i, j, k, m covered for DC and
 * DB; l and n uncovered. Stage R is covered for g..k and m, and KB2f has
 * no such stage. */
static int covers_matches_notes(void)
{
    static const char *const revisions = "fghijklmn";
    static const int deblock[9]  = { 1, 1, 1, 1, 1, 1, 0, 1, 0 };
    static const int dcrecon[9]  = { 1, 1, 1, 1, 1, 1, 0, 1, 0 };
    static const int residual[9] = { 0, 1, 1, 1, 1, 1, 0, 1, 0 };
    int i;

    for (i = 0; i < 9; i++) {
        int rev = revisions[i];

        CHECK(bink2_filter_covers(rev, BINK2_STAGE_DEBLOCK) == deblock[i]);
        CHECK(bink2_filter_covers(rev, BINK2_STAGE_DCRECON) == dcrecon[i]);
        CHECK(bink2_filter_covers(rev, BINK2_STAGE_RESIDUAL) ==
              residual[i]);
    }
    CHECK(bink2_filter_covers('e', BINK2_STAGE_DEBLOCK) == 0);
    CHECK(bink2_filter_covers('o', BINK2_STAGE_DEBLOCK) == 0);
    CHECK(bink2_filter_covers('g', 3) == 0);
    CHECK(bink2_filter_covers('g', -1) == 0);
    return 0;
}

int main(void)
{
    CHECK(deblock_example_1() == 0);
    CHECK(deblock_example_2() == 0);
    CHECK(deblock_example_3() == 0);
    CHECK(deblock_example_4() == 0);
    CHECK(deblock_example_5() == 0);
    CHECK(deblock_example_6() == 0);
    CHECK(deblock_example_7() == 0);
    CHECK(class_mask_matches_table() == 0);
    CHECK(covers_matches_notes() == 0);
    return 0;
}
