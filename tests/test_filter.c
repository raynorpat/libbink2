/* Unit tests for the RAD post-reconstruction filter stages. Expected values
 * are the worked examples of the approved behaviour notes. */
#include "bink2_filter.h"
#include "bink2_video.h"
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

/* ---- stage R: residual edge filter ---- */

#define RES_W 12
#define RES_SENTINEL 0x7777

/* Word of an inter block for stage R: class << 13 | 13-bit DC. */
static unsigned res_word(int cls, int dc)
{
    return (unsigned)cls << 13 | ((unsigned)dc & 0x1FFFu);
}

/* As edge_case, for the residual primitive on an int16 buffer: the edge
 * lies between columns (or rows) 5 and 6 and lines run 2..9. */
static int res_edge_case(const int in[4], unsigned wa, unsigned wb,
                         const int out[4])
{
    int16_t buf[RES_W * RES_W];
    int16_t want[RES_W * RES_W];
    int vertical, line, k;

    for (vertical = 0; vertical < 2; vertical++) {
        for (k = 0; k < RES_W * RES_W; k++)
            buf[k] = want[k] = RES_SENTINEL;
        for (line = 0; line < 8; line++)
            for (k = 0; k < 4; k++) {
                int across = EDGE_POS - 2 + k;
                int y = vertical ? LINE0 + line : across;
                int x = vertical ? across : LINE0 + line;

                buf[y * RES_W + x] = (int16_t)in[k];
                want[y * RES_W + x] = (int16_t)out[k];
            }
        if (vertical)
            bink2_filter_residual_edge(buf + LINE0 * RES_W + EDGE_POS, 1,
                                       RES_W, wa, wb);
        else
            bink2_filter_residual_edge(buf + EDGE_POS * RES_W + LINE0,
                                       RES_W, 1, wa, wb);
        CHECK(memcmp(buf, want, sizeof(buf)) == 0);
    }
    return 0;
}

static int residual_example_1(void)
{
    static const int in[4] = { -3, -3, 6, 6 };
    static const int out[4] = { -2, -1, 4, 5 };

    CHECK(res_edge_case(in, res_word(2, 0), res_word(2, 0), out) == 0);
    return 0;
}

/* Two class-3 blocks filter only when their signed DCs differ by more
 * than 32. */
static int residual_example_2(void)
{
    static const int in[4] = { -3, -3, 6, 6 };
    static const int out[4] = { -2, -1, 4, 5 };

    CHECK(res_edge_case(in, res_word(3, 10), res_word(3, 40), in) == 0);
    CHECK(res_edge_case(in, res_word(3, 10), res_word(3, 42), in) == 0);
    CHECK(res_edge_case(in, res_word(3, 10), res_word(3, 43), out) == 0);
    CHECK(res_edge_case(in, res_word(3, 10), res_word(3, 50), out) == 0);
    /* The DC is signed 13-bit: -20 and 20 are 40 apart, -10 and 20 are
     * 30 apart. */
    CHECK(res_edge_case(in, res_word(3, -20), res_word(3, 20), out) == 0);
    CHECK(res_edge_case(in, res_word(3, -10), res_word(3, 20), in) == 0);
    CHECK(res_edge_case(in, res_word(3, 50), res_word(3, 10), out) == 0);
    /* The DC test applies only when both blocks are class 3. */
    CHECK(res_edge_case(in, res_word(2, 10), res_word(3, 10), out) == 0);
    return 0;
}

static int residual_example_3(void)
{
    static const int in[4] = { -300, 0, -20, 0 };
    static const int out[4] = { -302, -5, -15, 2 };

    CHECK(res_edge_case(in, res_word(2, 0), res_word(2, 0), out) == 0);
    return 0;
}

static int residual_example_4(void)
{
    static const int in[4] = { -3, -3, 6, 6 };
    static const int out[4] = { -3, -3, 4, 6 };
    static const int swapped[4] = { -3, -1, 6, 6 };

    CHECK(res_edge_case(in, res_word(0, 0), res_word(1, 0), out) == 0);
    CHECK(res_edge_case(in, res_word(1, 0), res_word(0, 0), swapped) == 0);
    /* Both class 0: the edge is skipped. */
    CHECK(res_edge_case(in, res_word(0, 0), res_word(0, 99), in) == 0);
    return 0;
}

static int residual_word_layout(void)
{
    CHECK(bink2_filter_residual_word(10, 0) == 0x6000u + 10);
    CHECK(bink2_filter_residual_word(-1, 8) == 0x1FFFu);
    CHECK(bink2_filter_residual_word(-1023, 3) ==
          (0x4000u | ((unsigned)-1023 & 0x1FFFu)));
    CHECK(bink2_filter_residual_word(1023, 4) == (0x2000u | 1023));
    CHECK(bink2_filter_residual_word(5, 63) == 5);
    return 0;
}

/* The gates of the notes: filters on, a covered revision, frame flag
 * 0x2000 set and 0x40000 clear; 0x4000 drops the vertical edges and 0x8000
 * the horizontal ones. The 0x10000 edge arrays are not consulted. */
static int residual_dirs_gate(void)
{
    static const struct {
        int on;
        int version;
        uint32_t flags;
        unsigned dirs;
    } cases[] = {
        { 1, 'g', 0x2000, BINK2_RES_VERTICAL | BINK2_RES_HORIZONTAL },
        { 1, 'j', 0x2018, BINK2_RES_VERTICAL | BINK2_RES_HORIZONTAL },
        { 1, 'm', 0x12000, BINK2_RES_VERTICAL | BINK2_RES_HORIZONTAL },
        { 1, 'g', 0x6000, BINK2_RES_HORIZONTAL },
        { 1, 'g', 0xA000, BINK2_RES_VERTICAL },
        { 1, 'g', 0xE000, 0 },
        { 1, 'g', 0x42000, 0 },
        { 1, 'j', 0x6E018, 0 },     /* the encoder's inter frames */
        { 1, 'g', 0x0000, 0 },
        { 0, 'g', 0x2000, 0 },
        { 1, 'f', 0x2000, 0 },
        { 1, 'l', 0x2000, 0 },
        { 1, 'n', 0x2000, 0 },
    };
    Bink2Video v;
    size_t i;

    memset(&v, 0, sizeof(v));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        v.filters_on = cases[i].on;
        v.version = cases[i].version;
        v.frame_flags = cases[i].flags;
        CHECK(bink2_filter_residual_dirs(&v) == cases[i].dirs);
    }
    return 0;
}

/* A 16x16 chroma residual inside a sentinel border: TR is 16, the other
 * blocks 0, all class 2. The vertical x=8 edge runs first, then the
 * horizontal y=8 edge sees its output; the outer edges stay untouched. */
#define CR_STRIDE 20
#define CR_OFF (2 * CR_STRIDE + 2)

static int residual_chroma_order(void)
{
    int16_t buf[CR_STRIDE * CR_STRIDE];
    int16_t want[CR_STRIDE * CR_STRIDE];
    unsigned words[4];
    int x, y, k;

    for (k = 0; k < 4; k++)
        words[k] = res_word(2, 0);
    for (k = 0; k < CR_STRIDE * CR_STRIDE; k++)
        buf[k] = RES_SENTINEL;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            buf[CR_OFF + y * CR_STRIDE + x] =
                (int16_t)(y < 8 && x >= 8 ? 16 : 0);
    memcpy(want, buf, sizeof(buf));
    /* Vertical edge, top band: (0, 0 | 16, 16) -> (2, 4 | 12, 14). */
    for (y = 0; y < 8; y++) {
        want[CR_OFF + y * CR_STRIDE + 6] = 2;
        want[CR_OFF + y * CR_STRIDE + 7] = 4;
        want[CR_OFF + y * CR_STRIDE + 8] = 12;
        want[CR_OFF + y * CR_STRIDE + 9] = 14;
    }
    /* Horizontal edge, rows 6..9, per column after the vertical pass. */
    {
        static const int cols[5] = { 7, 8, 9, 10, 15 };
        static const int rows[5][4] = {
            { 4, 3, 1, 0 },         /* (4, 4 | 0, 0): d -4, a 0, b -1 */
            { 11, 9, 3, 1 },        /* (12, 12 | 0, 0): a -1, b -3 */
            { 12, 11, 3, 2 },       /* (14, 14 | 0, 0): a -2, b -3 */
            { 14, 12, 4, 2 },       /* (16, 16 | 0, 0): a -2, b -4 */
            { 14, 12, 4, 2 },
        };

        for (k = 0; k < 5; k++)
            for (y = 0; y < 4; y++)
                want[CR_OFF + (6 + y) * CR_STRIDE + cols[k]] =
                    (int16_t)rows[k][y];
        for (x = 11; x < 15; x++)
            for (y = 0; y < 4; y++)
                want[CR_OFF + (6 + y) * CR_STRIDE + x] =
                    (int16_t)rows[3][y];
    }
    bink2_filter_residual(buf + CR_OFF, CR_STRIDE, 2, words,
                          BINK2_RES_VERTICAL | BINK2_RES_HORIZONTAL);
    CHECK(memcmp(buf, want, sizeof(buf)) == 0);
    return 0;
}

/* A 32x32 luma residual whose block columns (or rows) step by 16, so each
 * internal edge at 8, 16 and 24 gets up to (+2, +4 | -4, -2). Words are
 * raster order, by * 4 + bx: blocks 1 and 2 are class 0, the rest class 2.
 * The outer edges never change. The other direction alone changes nothing,
 * since it sees no step. */
#define LU_STRIDE 36
#define LU_OFF (2 * LU_STRIDE + 2)

static int luma_expected(const unsigned words[16], int rows, int x, int y)
{
    int a = rows ? y : x;           /* position across the edges */
    int line = (rows ? x : y) >> 3; /* band or strip */
    int val = 16 * (a >> 3);
    int k;

    for (k = 1; k < 4; k++) {
        int ia = rows ? (k - 1) * 4 + line : line * 4 + k - 1;
        int ib = rows ? k * 4 + line : line * 4 + k;
        int ca = (int)(words[ia] >> 13) & 3;
        int cb = (int)(words[ib] >> 13) & 3;

        if (ca == 0 && cb == 0)
            continue;
        if (a == 8 * k - 2 && ca >= 2)
            val += 2;
        if (a == 8 * k - 1 && ca >= 1)
            val += 4;
        if (a == 8 * k && cb >= 1)
            val -= 4;
        if (a == 8 * k + 1 && cb >= 2)
            val -= 2;
    }
    return val;
}

static int residual_luma_edges(void)
{
    int16_t buf[LU_STRIDE * LU_STRIDE];
    int16_t want[LU_STRIDE * LU_STRIDE];
    unsigned words[16];
    int rows, x, y, k;

    for (k = 0; k < 16; k++)
        words[k] = res_word(k == 1 || k == 2 ? 0 : 2, 0);
    for (rows = 0; rows < 2; rows++) {
        unsigned dir = rows ? BINK2_RES_HORIZONTAL : BINK2_RES_VERTICAL;
        unsigned other = rows ? BINK2_RES_VERTICAL : BINK2_RES_HORIZONTAL;

        for (k = 0; k < LU_STRIDE * LU_STRIDE; k++)
            buf[k] = RES_SENTINEL;
        for (y = 0; y < 32; y++)
            for (x = 0; x < 32; x++)
                buf[LU_OFF + y * LU_STRIDE + x] =
                    (int16_t)(16 * ((rows ? y : x) >> 3));
        memcpy(want, buf, sizeof(buf));
        bink2_filter_residual(buf + LU_OFF, LU_STRIDE, 4, words, other);
        CHECK(memcmp(buf, want, sizeof(buf)) == 0);
        bink2_filter_residual(buf + LU_OFF, LU_STRIDE, 4, words, 0);
        CHECK(memcmp(buf, want, sizeof(buf)) == 0);

        for (y = 0; y < 32; y++)
            for (x = 0; x < 32; x++)
                want[LU_OFF + y * LU_STRIDE + x] =
                    (int16_t)luma_expected(words, rows, x, y);
        bink2_filter_residual(buf + LU_OFF, LU_STRIDE, 4, words, dir);
        CHECK(memcmp(buf, want, sizeof(buf)) == 0);
    }
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

/* ---- Stage DC: DC-only intra block reconstruction ---- */

#define DC_STRIDE 12
#define DC_ORIGIN (2 * DC_STRIDE + 2)

/* The info word of a DC-only intra block with DC `dc`. */
#define DC_ONLY(dc) (BINK2_INFO_DC_ONLY | (unsigned)(dc))

/* Runs the primitive on an 8x8 block at (2, 2) of a 12x12 buffer and
 * checks that nothing outside the block was written. words[] is
 * UL U UR / L X R / DL D DR; 0 is an inter block, never usable. */
static int dc_run(const unsigned words[9], int threshold, int integer_form,
                  unsigned char out[64])
{
    unsigned char buf[DC_STRIDE * DC_STRIDE];
    int x, y;

    memset(buf, SENTINEL, sizeof(buf));
    bink2_filter_dc_block(buf + DC_ORIGIN, DC_STRIDE, words, threshold,
                          integer_form);
    for (y = 0; y < DC_STRIDE; y++)
        for (x = 0; x < DC_STRIDE; x++) {
            int inside = x >= 2 && x < 10 && y >= 2 && y < 10;

            if (inside)
                out[(y - 2) * 8 + (x - 2)] = buf[y * DC_STRIDE + x];
            else
                CHECK(buf[y * DC_STRIDE + x] == SENTINEL);
        }
    return 0;
}

static int dc_flat(const unsigned char out[64], int value)
{
    int i;

    for (i = 0; i < 64; i++)
        CHECK(out[i] == value);
    return 0;
}

/* DC1: flat, with threshold 0 or no usable neighbours. */
static int dcrecon_example_1(void)
{
    unsigned words[9] = { 0 };
    unsigned char out[64];

    words[4] = DC_ONLY(800);
    CHECK(dc_run(words, 0, 1, out) == 0);
    CHECK(dc_flat(out, 100) == 0);
    CHECK(dc_run(words, 192, 1, out) == 0);
    CHECK(dc_flat(out, 100) == 0);
    words[4] = DC_ONLY(804);
    CHECK(dc_run(words, 0, 1, out) == 0);
    CHECK(dc_flat(out, 101) == 0);
    return 0;
}

/* DC2: 2047 gives 256, clamped to 255, in both forms. */
static int dcrecon_example_2(void)
{
    unsigned words[9] = { 0 };
    unsigned char out[64];

    words[4] = DC_ONLY(2047);
    CHECK(dc_run(words, 0, 1, out) == 0);
    CHECK(dc_flat(out, 255) == 0);
    CHECK(dc_run(words, 0, 0, out) == 0);
    CHECK(out[0] == 255);
    return 0;
}

/* DC3: L = 720 and R = 880 usable at T = 192; U, D and the diagonals are
 * not. Every row is the same horizontal ramp. */
static int dcrecon_example_3(void)
{
    static const unsigned char row[8] = {
        96, 97, 98, 100, 100, 102, 103, 104
    };
    unsigned words[9] = { 0 };
    unsigned char out[64];
    int i;

    words[3] = DC_ONLY(720);
    words[4] = DC_ONLY(800);
    words[5] = DC_ONLY(880);
    CHECK(dc_run(words, 192, 1, out) == 0);
    for (i = 0; i < 8; i++)
        CHECK(memcmp(out + 8 * i, row, 8) == 0);
    return 0;
}

/* DC4: U = 880 and D = 720 usable; L, R and the diagonals are not. */
static int dcrecon_example_4(void)
{
    static const unsigned char col0[8] = {
        102, 102, 101, 100, 100, 99, 98, 98
    };
    static const unsigned char col3[8] = {
        103, 102, 101, 100, 100, 99, 98, 97
    };
    unsigned words[9] = { 0 };
    unsigned char out[64];
    int i;

    words[1] = DC_ONLY(880);
    words[4] = DC_ONLY(800);
    words[7] = DC_ONLY(720);
    CHECK(dc_run(words, 192, 1, out) == 0);
    for (i = 0; i < 8; i++) {
        CHECK(out[8 * i] == col0[i]);
        CHECK(out[8 * i + 3] == col3[i]);
    }
    return 0;
}

/* DC5: the float form, flat. */
static int dcrecon_example_5(void)
{
    static const int dc[3] = { 800, 804, 4 };
    static const int want[3] = { 100, 101, 1 };
    const float bias = 0.49999991f;
    unsigned words[9] = { 0 };
    unsigned char out[64];
    uint32_t bits;
    int i;

    /* The rounding bias the primitive adds is the float 0x3EFFFFFD. */
    memcpy(&bits, &bias, sizeof(bits));
    CHECK(bits == 0x3EFFFFFDu);
    for (i = 0; i < 3; i++) {
        words[4] = DC_ONLY(dc[i]);
        CHECK(dc_run(words, 0, 0, out) == 0);
        CHECK(out[0] == want[i]);
    }
    return 0;
}

/* DC6: the float form, with every neighbour usable: the left column 720,
 * the right column 880, U = D = C = 800. */
static int dcrecon_example_6(void)
{
    static const unsigned dc[9] = {
        720, 800, 880, 720, 800, 880, 720, 800, 880
    };
    unsigned words[9];
    unsigned char out[64];
    int i;

    for (i = 0; i < 9; i++)
        words[i] = DC_ONLY(dc[i]);
    CHECK(dc_run(words, 192, 0, out) == 0);
    CHECK(out[0] == 96);
    CHECK(out[3 * 8 + 3] == 99);
    return 0;
}

/* DC7: DC3 with T = 64; L and R differ by 80, so the block is flat. */
static int dcrecon_example_7(void)
{
    unsigned words[9] = { 0 };
    unsigned char out[64];

    words[3] = DC_ONLY(720);
    words[4] = DC_ONLY(800);
    words[5] = DC_ONLY(880);
    CHECK(dc_run(words, 64, 1, out) == 0);
    CHECK(dc_flat(out, 100) == 0);
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
    CHECK(residual_example_1() == 0);
    CHECK(residual_example_2() == 0);
    CHECK(residual_example_3() == 0);
    CHECK(residual_example_4() == 0);
    CHECK(residual_word_layout() == 0);
    CHECK(residual_dirs_gate() == 0);
    CHECK(residual_chroma_order() == 0);
    CHECK(residual_luma_edges() == 0);
    CHECK(dcrecon_example_1() == 0);
    CHECK(dcrecon_example_2() == 0);
    CHECK(dcrecon_example_3() == 0);
    CHECK(dcrecon_example_4() == 0);
    CHECK(dcrecon_example_5() == 0);
    CHECK(dcrecon_example_6() == 0);
    CHECK(dcrecon_example_7() == 0);
    return 0;
}
