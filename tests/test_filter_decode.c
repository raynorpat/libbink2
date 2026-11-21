#include "bink2.h"
#include "bink2_filter.h"
#include "bink2_internal.h"
#include "bink2_video.h"
#include "test_common.h"

/* Block info words for the synthetic tests (see bink2_video.h). */
#define WORD_INTRA_CLASS1 0xA000u
#define WORD_INTRA_CLASS2 0xC000u

/* ---- synthetic Bink2Video tests: no media needed ---- */

static unsigned char *luma_at(Bink2Video *v, int p, int x, int y)
{
    return v->cur[p] + (size_t)y * v->stride[p] + x;
}

static int plane_rows(const Bink2Video *v, int p)
{
    return (int)(p == 1 || p == 2 ? v->aligned_h / 2 : v->aligned_h);
}

static size_t plane_bytes(const Bink2Video *v, int p)
{
    return (size_t)v->stride[p] * (size_t)plane_rows(v, p);
}

static void fill_words(Bink2Video *v, unsigned word)
{
    int p;

    for (p = 0; p < 4; p++) {
        int rows = plane_rows(v, p) / 8;
        int i;

        if (v->blk_info[p] == NULL)
            continue;
        for (i = 0; i < rows * v->blk_stride[p]; i++)
            v->blk_info[p][i] = (uint16_t)word;
    }
}

/* Columns before `step` are `lo`, the rest `hi`; or rows, when `rows`. */
static void fill_step(Bink2Video *v, int p, int step, int lo, int hi,
                      int rows)
{
    int x, y;

    for (y = 0; y < plane_rows(v, p); y++)
        for (x = 0; x < v->stride[p]; x++)
            *luma_at(v, p, x, y) =
                (unsigned char)(((rows ? y : x) < step) ? lo : hi);
}

static void set_mv(Bink2Video *v, int mb, int quadrant, int mx, int my)
{
    v->mv[0][mb * 4 + quadrant] = (int16_t)mx;
    v->mv[1][mb * 4 + quadrant] = (int16_t)my;
}

/* Every line across the step at `step` reads p1 p0 q0 q1 = e[0..3], over
 * the first `lines` lines; every other pixel is still lo or hi. */
static int check_step(Bink2Video *v, int p, int step, int lo, int hi,
                      int rows, int lines, const int e[4])
{
    int x, y;

    for (y = 0; y < plane_rows(v, p); y++) {
        for (x = 0; x < v->stride[p]; x++) {
            int along = rows ? x : y;
            int across = rows ? y : x;
            int want = across < step ? lo : hi;
            int got = *luma_at(v, p, x, y);

            if (along < lines && across >= step - 2 && across < step + 2)
                want = e[across - step + 2];
            if (got != want) {
                fprintf(stderr, "plane %d (%d,%d): %d, want %d\n", p, x, y,
                        got, want);
                return 1;
            }
        }
    }
    return 0;
}

static const int DB1_FULL[4] = { 103, 105, 115, 117 };
static const int DB1_NONE[4] = { 100, 100, 120, 120 };
static const int DB1_P0[4] = { 100, 105, 120, 120 };

/* Vertical edges: the strength rules, the frame flag and the column
 * array. 64x16 has two macroblocks and two processed block rows. */
static int test_vertical_rules(void)
{
    Bink2Video v;

    CHECK(bink2_video_init(&v, 64, 16, 0, 'g') == 0);
    v.frame_flags = 0x2000;

    /* Rule C, both class 2: all four taps. */
    fill_words(&v, WORD_INTRA_CLASS2);
    fill_step(&v, 0, 8, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 8, 100, 120, 0, 16, DB1_FULL) == 0);

    /* 0x4000 turns every vertical edge off. */
    fill_step(&v, 0, 8, 100, 120, 0);
    v.frame_flags = 0x2000 | 0x4000;
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 8, 100, 120, 0, 16, DB1_NONE) == 0);
    v.frame_flags = 0x2000;

    /* Column bit 1 disables the luma edge at x = 8 only. */
    fill_step(&v, 0, 8, 100, 120, 0);
    v.col_flags[1] = 1;
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 8, 100, 120, 0, 16, DB1_NONE) == 0);
    fill_step(&v, 0, 16, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 16, 100, 120, 0, 16, DB1_FULL) == 0);
    v.col_flags[1] = 0;

    /* Rule C, intra class 1 against an inter macroblock: p0 only. */
    fill_words(&v, 0);
    {
        int r, c;

        for (r = 0; r < 4; r++)
            for (c = 0; c < 4; c++)
                v.blk_info[0][r * v.blk_stride[0] + c] = WORD_INTRA_CLASS1;
    }
    fill_step(&v, 0, 32, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 32, 100, 120, 0, 16, DB1_P0) == 0);

    /* Rule M: two inter blocks off a 16-pixel boundary never filter. */
    fill_words(&v, 0);
    set_mv(&v, 0, 0, 0, 0);
    set_mv(&v, 0, 1, 8, 0);
    fill_step(&v, 0, 8, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 8, 100, 120, 0, 16, DB1_NONE) == 0);

    /* On a 16-pixel boundary, quadrant vectors that differ by 2. */
    fill_step(&v, 0, 16, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 16, 100, 120, 0, 16, DB1_FULL) == 0);
    set_mv(&v, 0, 1, 1, 1);
    fill_step(&v, 0, 16, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 16, 100, 120, 0, 16, DB1_NONE) == 0);

    /* Across the macroblock edge: MB 0 TR against MB 1 TL. */
    set_mv(&v, 0, 1, 0, 0);
    set_mv(&v, 1, 0, 0, -2);
    fill_step(&v, 0, 32, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 32, 100, 120, 0, 16, DB1_FULL) == 0);
    set_mv(&v, 1, 0, 0, 0);

    /* Chroma: every 8-pixel edge counts as a 16-pixel luma boundary, and
     * it reads the luma quadrants (MB 0 TL against TR). */
    set_mv(&v, 0, 1, -4, 0);
    fill_step(&v, 1, 8, 100, 120, 0);
    fill_step(&v, 2, 8, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 1, 8, 100, 120, 0, 8, DB1_FULL) == 0);
    CHECK(check_step(&v, 2, 8, 100, 120, 0, 8, DB1_FULL) == 0);
    set_mv(&v, 0, 1, 0, 0);

    /* Chroma edge x = 8 sits on luma column 16: bit 2, not bit 1. */
    fill_words(&v, WORD_INTRA_CLASS2);
    v.col_flags[1] = 1;
    fill_step(&v, 1, 8, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 1, 8, 100, 120, 0, 8, DB1_FULL) == 0);
    v.col_flags[1] = 0;
    v.col_flags[2] = 1;
    fill_step(&v, 1, 8, 100, 120, 0);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 1, 8, 100, 120, 0, 8, DB1_NONE) == 0);
    v.col_flags[2] = 0;

    bink2_video_uninit(&v);
    return 0;
}

/* Horizontal edges, mirrored: 32x64, one macroblock column. */
static int test_horizontal_rules(void)
{
    Bink2Video v;

    CHECK(bink2_video_init(&v, 32, 64, 0, 'g') == 0);
    v.frame_flags = 0x2000;

    fill_words(&v, WORD_INTRA_CLASS2);
    fill_step(&v, 0, 8, 100, 120, 1);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 8, 100, 120, 1, 32, DB1_FULL) == 0);

    fill_step(&v, 0, 8, 100, 120, 1);
    v.frame_flags = 0x2000 | 0x8000;
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 8, 100, 120, 1, 32, DB1_NONE) == 0);
    v.frame_flags = 0x2000;

    fill_step(&v, 0, 24, 100, 120, 1);
    v.row_flags[3] = 1;
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 0, 24, 100, 120, 1, 32, DB1_NONE) == 0);
    v.row_flags[3] = 0;

    /* Rule M between TL and BL of MB 0, at y = 16. */
    fill_words(&v, 0);
    set_mv(&v, 0, 2, 0, 2);
    fill_step(&v, 0, 16, 100, 120, 1);
    bink2_filter_deblock(&v);
    /* Only the left quadrant column has a differing vector. */
    {
        int x;

        for (x = 0; x < 32; x++) {
            int top = *luma_at(&v, 0, x, 14);
            int bottom = *luma_at(&v, 0, x, 17);

            CHECK(top == (x < 16 ? 103 : 100));
            CHECK(bottom == (x < 16 ? 117 : 120));
        }
    }
    set_mv(&v, 0, 2, 0, 0);

    /* Row bit 2 disables chroma row 8 (luma row 16). */
    fill_words(&v, WORD_INTRA_CLASS2);
    v.row_flags[2] = 1;
    fill_step(&v, 1, 8, 100, 120, 1);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 1, 8, 100, 120, 1, 16, DB1_NONE) == 0);
    v.row_flags[2] = 0;
    fill_step(&v, 1, 8, 100, 120, 1);
    bink2_filter_deblock(&v);
    CHECK(check_step(&v, 1, 8, 100, 120, 1, 16, DB1_FULL) == 0);

    bink2_video_uninit(&v);
    return 0;
}

static unsigned long lcg(unsigned long *seed)
{
    *seed = *seed * 1103515245UL + 12345UL;
    return (*seed >> 16) & 0x7FFF;
}

/* 40x40 pads to 64x64, 16-aligned height 48. Rows from 48 on (the last
 * macroblock row's lower half) and alpha are never written, vertical edges
 * leave the left border alone and horizontal edges the top border. */
static int test_deblock_bounds(void)
{
    static const unsigned long frame_flags[3] = { 0x20, 0x8000, 0x4000 };
    Bink2Video v;
    unsigned char *before[4];
    unsigned long seed = 7;
    int f, p, x, y;

    CHECK(bink2_video_init(&v, 40, 40, BINK2_FLAG_ALPHA, 'f') == 0);
    fill_words(&v, WORD_INTRA_CLASS2);
    for (p = 0; p < 4; p++) {
        before[p] = (unsigned char *)malloc(plane_bytes(&v, p));
        CHECK(before[p] != NULL);
    }
    for (f = 0; f < 3; f++) {
        int changed = 0, padding = 0;

        for (p = 0; p < 4; p++) {
            size_t i;

            for (i = 0; i < plane_bytes(&v, p); i++)
                v.cur[p][i] = (unsigned char)lcg(&seed);
            memcpy(before[p], v.cur[p], plane_bytes(&v, p));
        }
        v.frame_flags = frame_flags[f];
        bink2_filter_deblock(&v);
        for (p = 0; p < 3; p++) {
            int h16 = p ? 24 : 48;

            for (y = 0; y < plane_rows(&v, p); y++) {
                for (x = 0; x < v.stride[p]; x++) {
                    size_t i = (size_t)y * v.stride[p] + x;
                    int same = v.cur[p][i] == before[p][i];

                    if (y >= h16 || (f == 1 && x < 6) || (f == 2 && y < 6))
                        CHECK(same);
                    changed |= !same;
                    /* Vertical edge x = 48 lies in the padding columns
                     * past the 40-pixel width and is still filtered. */
                    if (p == 0 && f == 1 && y < h16 && x >= 46 && x < 50)
                        padding |= !same;
                }
            }
        }
        CHECK(memcmp(v.cur[3], before[3], plane_bytes(&v, 3)) == 0);
        CHECK(changed);
        CHECK(f != 1 || padding);
    }
    for (p = 0; p < 4; p++)
        free(before[p]);
    bink2_video_uninit(&v);
    return 0;
}

/* KB2l and KB2n are outside the notes' coverage: no stage runs, and
 * filters_on = 0 also leaves the picture alone. */
static int test_uncovered_revision(void)
{
    static const int versions[2] = { 'l', 'n' };
    Bink2Video v;
    int i;

    for (i = 0; i < 2; i++) {
        CHECK(bink2_filter_covers(versions[i], BINK2_STAGE_DEBLOCK) == 0);
        CHECK(bink2_video_init(&v, 64, 16, 0, versions[i]) == 0);
        v.frame_flags = 0x2000;
        fill_words(&v, WORD_INTRA_CLASS2);
        fill_step(&v, 0, 8, 100, 120, 0);
        bink2_video_apply_filters(&v);
        CHECK(check_step(&v, 0, 8, 100, 120, 0, 16, DB1_NONE) == 0);
        bink2_video_uninit(&v);
    }

    CHECK(bink2_video_init(&v, 64, 16, 0, 'g') == 0);
    v.frame_flags = 0x2000;
    fill_words(&v, WORD_INTRA_CLASS2);
    fill_step(&v, 0, 8, 100, 120, 0);
    v.filters_on = 0;
    bink2_video_apply_filters(&v);
    CHECK(check_step(&v, 0, 8, 100, 120, 0, 16, DB1_NONE) == 0);
    v.filters_on = 1;
    bink2_video_apply_filters(&v);
    CHECK(check_step(&v, 0, 8, 100, 120, 0, 16, DB1_FULL) == 0);
    bink2_video_uninit(&v);
    return 0;
}

/* ---- stage DC over a whole frame ---- */

#define WORD_DC_ONLY(dc) (BINK2_INFO_DC_ONLY | (unsigned)(dc))

static int info_rows(const Bink2Video *v, int p)
{
    return plane_rows(v, p) / 8;
}

/* The word of block (c, r) of plane p as stage DC sees it: 0 left of the
 * first and right of the last column; above the top row and below the
 * bottom block row the row itself. */
static unsigned dc_neighbour(const Bink2Video *v, int p, int c, int r)
{
    if (c < 0 || c >= v->blk_stride[p])
        return 0;
    if (r < 0)
        r = 0;
    if (r >= info_rows(v, p))
        r = info_rows(v, p) - 1;
    return v->blk_info[p][r * v->blk_stride[p] + c];
}

/* What stage DC should leave in `want` (a copy of every plane of v), built
 * block by block from the primitive. Only block rows above the 16-aligned
 * height are processed. */
static void dc_expected(const Bink2Video *v, unsigned char *want[4])
{
    int threshold = 8 * (int)(v->frame_flags & 0xFF);
    int integer_form = (v->frame_flags & 0x2000) != 0;
    int p, r, c, k;

    for (p = 0; p < 4; p++) {
        int s = p == 1 || p == 2;
        int rows = (int)((v->height + 15) & ~15UL) / 8 >> s;

        if (v->blk_info[p] == NULL)
            continue;
        for (r = 0; r < rows; r++)
            for (c = 0; c < v->blk_stride[p]; c++) {
                unsigned words[9];

                if (dc_neighbour(v, p, c, r) < BINK2_INFO_DC_ONLY)
                    continue;
                for (k = 0; k < 9; k++)
                    words[k] = dc_neighbour(v, p, c + k % 3 - 1,
                                            r + k / 3 - 1);
                bink2_filter_dc_block(want[p] + (size_t)(8 * r) *
                                      v->stride[p] + 8 * c, v->stride[p],
                                      words, threshold, integer_form);
            }
    }
}

/* Random words, mostly DC-only blocks whose DCs are close enough to
 * interpolate, over random pixels; checks every byte of every plane,
 * padding included. 80x40 pads to 96x64 with a 16-aligned height of 48,
 * so the last macroblock row's lower half is left alone. KB2f takes the
 * float form and has alpha; KB2g the integer form. */
static int test_dcrecon_frame(void)
{
    static const int versions[2] = { 'g', 'f' };
    static const unsigned long flags[2] = { 0, BINK2_FLAG_ALPHA };
    static const uint32_t frame_flags[2] = { 0x2000 | 24, 0x20 };
    Bink2Video v;
    unsigned char *want[4];
    unsigned long seed = 11;
    int t, p, i, changed;

    for (t = 0; t < 2; t++) {
        CHECK(bink2_video_init(&v, 80, 40, flags[t], versions[t]) == 0);
        v.frame_flags = frame_flags[t];
        changed = 0;
        for (p = 0; p < 4; p++) {
            size_t n;

            want[p] = NULL;
            if (v.blk_info[p] == NULL)
                continue;
            for (i = 0; i < info_rows(&v, p) * v.blk_stride[p]; i++) {
                unsigned kind = (unsigned)lcg(&seed) % 8;
                unsigned dc = 700 + (unsigned)lcg(&seed) % 300;

                v.blk_info[p][i] = (uint16_t)(kind < 5 ? WORD_DC_ONLY(dc) :
                                              kind < 6 ? 0xC000u | dc :
                                              kind < 7 ? 0u :
                                              WORD_DC_ONLY(dc + 1200));
            }
            n = plane_bytes(&v, p);
            for (i = 0; i < (int)n; i++)
                v.cur[p][i] = (unsigned char)lcg(&seed);
            want[p] = (unsigned char *)malloc(n);
            CHECK(want[p] != NULL);
            memcpy(want[p], v.cur[p], n);
        }
        CHECK(v.blk_info[3] != NULL || t == 0);
        dc_expected(&v, want);
        for (p = 0; p < 4; p++)
            if (want[p] != NULL)
                changed |= memcmp(v.cur[p], want[p],
                                  plane_bytes(&v, p)) != 0;
        CHECK(changed);
        bink2_filter_dcrecon(&v);
        for (p = 0; p < 4; p++) {
            if (want[p] == NULL)
                continue;
            CHECK(memcmp(v.cur[p], want[p], plane_bytes(&v, p)) == 0);
            free(want[p]);
        }
        bink2_video_uninit(&v);
    }
    return 0;
}

/* Fills every plane with `value` and every word with `word`. */
static void fill_all(Bink2Video *v, unsigned word, int value)
{
    int p;

    fill_words(v, word);
    for (p = 0; p < 4; p++)
        if (v->cur[p] != NULL)
            memset(v->cur[p], value, plane_bytes(v, p));
}

static int plane_is(const Bink2Video *v, int p, int value)
{
    size_t i;

    for (i = 0; i < plane_bytes(v, p); i++)
        CHECK(v->cur[p][i] == value);
    return 0;
}

/* Rows 0..15 of Y (the processed rows of a 64x16 frame) are `value`, the
 * rest 0. */
static int luma_processed_is(const Bink2Video *v, int value)
{
    int x, y;

    for (y = 0; y < plane_rows(v, 0); y++)
        for (x = 0; x < v->stride[0]; x++)
            CHECK(v->cur[0][(size_t)y * v->stride[0] + x] ==
                  (y < 16 ? value : 0));
    return 0;
}

/* apply_filters runs stage DC unless filters are off or the revision is
 * uncovered; the DB flags and edge arrays never stop it. Every block is
 * DC-only with DC 800, so DC1 gives flat 100. */
static int test_dcrecon_gates(void)
{
    static const int uncovered[2] = { 'l', 'n' };
    Bink2Video v;
    int i;

    for (i = 0; i < 2; i++) {
        CHECK(bink2_filter_covers(uncovered[i], BINK2_STAGE_DCRECON) == 0);
        CHECK(bink2_video_init(&v, 64, 16, 0, uncovered[i]) == 0);
        v.frame_flags = 0x2000;
        fill_all(&v, WORD_DC_ONLY(800), 0);
        bink2_video_apply_filters(&v);
        CHECK(plane_is(&v, 0, 0) == 0);
        bink2_video_uninit(&v);
    }

    CHECK(bink2_video_init(&v, 64, 16, 0, 'g') == 0);
    v.frame_flags = 0x2000 | 0x4000 | 0x8000 | 0x10000;
    fill_all(&v, WORD_DC_ONLY(800), 0);
    v.filters_on = 0;
    bink2_video_apply_filters(&v);
    CHECK(plane_is(&v, 0, 0) == 0);
    v.filters_on = 1;
    for (i = 1; i <= v.row_size; i++)
        v.row_flags[i] = 1;
    for (i = 1; i <= v.col_size; i++)
        v.col_flags[i] = 1;
    bink2_video_apply_filters(&v);
    CHECK(luma_processed_is(&v, 100) == 0);
    bink2_video_uninit(&v);

    /* KB2f: 0x2000 clear selects the float form, which DC5 also puts at
     * 100 for DC 800. */
    CHECK(bink2_video_init(&v, 64, 16, 0, 'f') == 0);
    v.frame_flags = 0;
    fill_all(&v, WORD_DC_ONLY(800), 0);
    bink2_video_apply_filters(&v);
    CHECK(v.cur[0][0] == 100);
    bink2_video_uninit(&v);
    return 0;
}

/* DC runs before DB: a DC-only block next to a class 2 block, with a
 * step at the edge between them. */
static int test_dc_before_db(void)
{
    Bink2Video v;
    unsigned char *start, *want;
    size_t n;
    int r;

    CHECK(bink2_video_init(&v, 64, 16, 0, 'g') == 0);
    v.frame_flags = 0x2000;
    n = plane_bytes(&v, 0);
    start = (unsigned char *)malloc(n);
    want = (unsigned char *)malloc(n);
    CHECK(start != NULL && want != NULL);
    fill_words(&v, WORD_INTRA_CLASS2);
    for (r = 0; r < 2; r++)
        v.blk_info[0][r * v.blk_stride[0]] = (uint16_t)WORD_DC_ONLY(800);
    fill_step(&v, 0, 8, 60, 120, 0);
    memcpy(start, v.cur[0], n);

    bink2_filter_dcrecon(&v);
    bink2_filter_deblock(&v);
    memcpy(want, v.cur[0], n);

    memcpy(v.cur[0], start, n);
    bink2_video_apply_filters(&v);
    CHECK(memcmp(v.cur[0], want, n) == 0);
    /* DB then moved the DC block's pixels next to the edge. */
    CHECK(*luma_at(&v, 0, 7, 0) != 100);

    memcpy(v.cur[0], start, n);
    bink2_filter_deblock(&v);
    bink2_filter_dcrecon(&v);
    CHECK(memcmp(v.cur[0], want, n) != 0);

    free(start);
    free(want);
    bink2_video_uninit(&v);
    return 0;
}

/* ---- the 0x10000 row/column arrays ---- */

static int parse_flags(const char *bits, int size, unsigned char *flags,
                       size_t *end)
{
    unsigned char buf[16];
    Bink2Bits r;
    int i, ret;

    memset(buf, 0, sizeof(buf));
    for (i = 0; bits[i] != '\0'; i++)
        if (bits[i] == '1')
            buf[i >> 3] |= (unsigned char)(1u << (i & 7));
    memset(flags, 0xAA, 64);
    bink2_bits_init(&r, buf, (strlen(bits) + 7) / 8);
    ret = bink2_video_read_edge_flags(&r, flags, size);
    *end = r.bitpos;
    return ret;
}

/* flags[1..] hold want, and flags[0] and the entry after are untouched. */
static int flags_are(const unsigned char *flags, const char *want)
{
    int i;

    if (flags[0] != 0xAA)
        return 0;
    for (i = 0; want[i] != '\0'; i++)
        if (flags[i + 1] != (unsigned char)(want[i] - '0'))
            return 0;
    return flags[i + 1] == 0xAA;
}

static int test_edge_flags(void)
{
    unsigned char flags[64];
    size_t end;

    /* Raw: the flags follow in order. */
    CHECK(parse_flags("01011001", 7, flags, &end) == 0);
    CHECK(flags_are(flags, "1011001") && end == 8);

    /* A run: v = 1, length nb + 2 = 6, then r = 1 (4 bits). */
    CHECK(parse_flags("1111000", 7, flags, &end) == 0);
    CHECK(flags_are(flags, "1111111") && end == 7);

    /* A literal group (v, 4 raw bits), then a run clamped to the 4 left,
     * which reads no r. */
    CHECK(parse_flags("101011010", 9, flags, &end) == 0);
    CHECK(flags_are(flags, "101100000") && end == 9);

    /* r != 2^nb - 1 sets mode 3, so the next run flips v without reading
     * it and is nb + 1 long. */
    CHECK(parse_flags("1110100010110", 20, flags, &end) == 0);
    CHECK(flags_are(flags, "11111111100000000000") && end == 13);

    /* Mode 3 before a literal group flips v too; the final group of one
     * flag reads v and no raw bits. */
    CHECK(parse_flags("11100000110101", 12, flags, &end) == 0);
    CHECK(flags_are(flags, "111111011011") && end == 14);

    /* r = 15 runs 21 flags into an array of 7. */
    CHECK(parse_flags("1101111", 7, flags, &end) != 0);
    /* The packet ends inside the raw flags. */
    CHECK(parse_flags("0101", 9, flags, &end) != 0);
    return 0;
}

/* libbink2's KB2j keyframes carry a raw row array of ones (every
 * horizontal edge off) and no column array; inter frames carry neither. */
static int test_fixture_arrays(void)
{
    bink2 b = bink2_open_file(BINK2_FIXTURE_DIR "/synthetic_kb2j.bk2",
                              BINK2_MODE_DISK);
    Bink2Video *v;
    int i;

    CHECK(b != NULL);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_first(b) != BINK2_ERROR);
    v = b->video;
    CHECK(v->row_size == 15 && v->col_size == 19);
    CHECK(v->row_flags[0] == 0);
    for (i = 1; i <= v->row_size; i++)
        CHECK(v->row_flags[i] == 1);
    for (i = 0; i <= v->col_size; i++)
        CHECK(v->col_flags[i] == 0);
    CHECK(bink2_next(b) != BINK2_ERROR);
    for (i = 0; i <= v->row_size; i++)
        CHECK(v->row_flags[i] == 0);
    bink2_close(b);
    return 0;
}

/* ---- decoding whole files ---- */

typedef struct {
    unsigned char *data;
    size_t size;
} Blob;

static int load_blob(const char *path, Blob *out)
{
    FILE *f = fopen(path, "rb");
    long end;

    out->data = NULL;
    out->size = 0;
    if (f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, 0, SEEK_SET);
    out->data = (unsigned char *)malloc((size_t)end);
    if (out->data == NULL ||
        fread(out->data, 1, (size_t)end, f) != (size_t)end) {
        fclose(f);
        free(out->data);
        out->data = NULL;
        return -1;
    }
    fclose(f);
    out->size = (size_t)end;
    return 0;
}

static int load_sample(const char *name, Blob *out)
{
    const char *dir = test_media_dir();
    char path[1024];

    if (dir == NULL)
        return 77;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    return load_blob(path, out) == 0 ? 0 : 77;
}

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static unsigned long get32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* A new container whose frame i is the source's frame src[i]. A negative
 * entry -(k + 1) is frame k with half of its video data cut off. Each
 * frame keeps its keyframe bit. */
static int splice(const Blob *in, const int *src, int count, Blob *out)
{
    bink2 b = bink2_open_memory(in->data, (unsigned long)in->size);
    unsigned long index_off, pos;
    size_t total;
    int i;

    if (b == NULL)
        return -1;
    index_off = 44UL + (b->version >= 'i' ? 4UL : 0UL) +
                12UL * (unsigned long)b->audio_tracks;
    if ((get32(in->data + index_off) & ~1UL) != b->frame_offsets[0]) {
        bink2_close(b);
        return -1;
    }
    total = index_off + 4UL * ((unsigned long)count + 1UL);
    for (i = 0; i < count; i++) {
        int k = src[i] < 0 ? -src[i] - 1 : src[i];

        total += b->frame_sizes[k];
    }
    out->data = (unsigned char *)calloc(1, total);
    if (out->data == NULL) {
        bink2_close(b);
        return -1;
    }
    memcpy(out->data, in->data, index_off);
    put32(out->data + 8, (unsigned long)count);
    pos = index_off + 4UL * ((unsigned long)count + 1UL);
    for (i = 0; i < count; i++) {
        int k = src[i] < 0 ? -src[i] - 1 : src[i];
        const unsigned char *pkt = in->data + b->frame_offsets[k];
        unsigned long size = b->frame_sizes[k];

        if (src[i] < 0) {
            unsigned long video = 0;
            int t;

            for (t = 0; t < b->audio_tracks; t++)
                video += 4 + get32(pkt + video);
            size = video + (size - video) / 2;
        }
        put32(out->data + index_off + 4UL * (unsigned long)i,
              pos | b->frame_keyframes[k]);
        memcpy(out->data + pos, pkt, size);
        pos += size;
    }
    put32(out->data + index_off + 4UL * (unsigned long)count, pos);
    put32(out->data + 4, pos - 8);
    out->size = pos;
    bink2_close(b);
    return 0;
}

static uint64_t picture_hash(bink2 b, int luma_only)
{
    unsigned long w = 0, h = 0;
    uint64_t hash = 0;
    int p;

    bink2_info_video(b, &w, &h, NULL);
    for (p = 0; p < (luma_only ? 1 : 3); p++) {
        int pw = p == 0 ? (int)w : (int)((w + 1) / 2);
        int ph = p == 0 ? (int)h : (int)((h + 1) / 2);

        hash = hash * 31 + fnv1a_rows(bink2_get_plane(b, p), pw, ph,
                                      bink2_get_stride(b, p));
    }
    return hash;
}

#define MAX_FRAMES 32

/* Hashes of frames 0..n-1; threads 0 is the default. */
static int decode_hashes(const Blob *in, int filters, int threads, int n,
                         int luma_only, uint64_t *hash)
{
    bink2 b = bink2_open_memory(in->data, (unsigned long)in->size);
    int i;

    if (b == NULL)
        return -1;
    bink2_enable_video(b, 1);
    bink2_enable_filters(b, (unsigned char)filters);
    if (threads)
        bink2_internal_set_threads(b, threads);
    for (i = 0; i < n; i++) {
        if ((i == 0 ? bink2_first(b) : bink2_next(b)) == BINK2_ERROR) {
            bink2_close(b);
            return -1;
        }
        hash[i] = picture_hash(b, luma_only);
    }
    bink2_close(b);
    return 0;
}

static int test_filters_change_output(const Blob *in, int n)
{
    uint64_t on[MAX_FRAMES], off[MAX_FRAMES];
    int i, differ = 0;

    CHECK(decode_hashes(in, 1, 0, n, 1, on) == 0);
    CHECK(decode_hashes(in, 0, 0, n, 1, off) == 0);
    for (i = 0; i < n; i++)
        differ |= on[i] != off[i];
    CHECK(differ);
    return 0;
}

static int test_threads_match(const Blob *in, int n)
{
    uint64_t serial[MAX_FRAMES], threaded[MAX_FRAMES];
    int i;

    CHECK(decode_hashes(in, 1, 1, n, 0, serial) == 0);
    CHECK(decode_hashes(in, 1, 0, n, 0, threaded) == 0);
    for (i = 0; i < n; i++)
        CHECK(serial[i] == threaded[i]);
    return 0;
}

/* ge_video_86l has one keyframe; repeating frames 0..9 gives a second at
 * frame 10, whose GOP decodes exactly as the first. */
static const int REPEAT[20] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
};

static int test_toggle_latches_at_keyframe(const Blob *rep)
{
    uint64_t on[20], off[20];
    bink2 b;
    int i, differ = 0;

    CHECK(decode_hashes(rep, 1, 0, 20, 0, on) == 0);
    CHECK(decode_hashes(rep, 0, 0, 20, 0, off) == 0);
    for (i = 4; i < 10; i++)
        differ |= on[i] != off[i];
    CHECK(differ);

    b = bink2_open_memory(rep->data, (unsigned long)rep->size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    for (i = 0; i < 20; i++) {
        CHECK((i == 0 ? bink2_first(b) : bink2_next(b)) != BINK2_ERROR);
        CHECK(picture_hash(b, 0) == (i < 10 ? on[i] : off[i]));
        if (i == 3)
            CHECK(bink2_enable_filters(b, 0) == 0);
    }
    bink2_close(b);
    return 0;
}

static int test_seek_matches_linear(const Blob *rep)
{
    uint64_t linear[20];
    bink2 b;
    int i;

    CHECK(decode_hashes(rep, 1, 0, 20, 0, linear) == 0);
    b = bink2_open_memory(rep->data, (unsigned long)rep->size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    CHECK(bink2_first(b) != BINK2_ERROR);
    for (i = 0; i < 5; i++)
        CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(bink2_seek_keyframe(b, 13) != BINK2_ERROR);
    CHECK(picture_hash(b, 0) == linear[10]);
    CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(picture_hash(b, 0) == linear[11]);
    CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(picture_hash(b, 0) == linear[12]);
    CHECK(bink2_seek_keyframe(b, 4) != BINK2_ERROR);
    CHECK(picture_hash(b, 0) == linear[0]);
    CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(picture_hash(b, 0) == linear[1]);
    CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(picture_hash(b, 0) == linear[2]);
    bink2_close(b);
    return 0;
}

static int test_error_restores_filtered(const Blob *in)
{
    static const int damaged[12] = { 0, 1, 2, 3, 4, -6, 6, 7, 8, 9, 0, 1 };
    uint64_t on[12], off[12];
    Blob bad;
    bink2 b;
    int i;

    {
        Blob rep;

        CHECK(splice(in, REPEAT, 12, &rep) == 0);
        CHECK(decode_hashes(&rep, 1, 0, 12, 0, on) == 0);
        CHECK(decode_hashes(&rep, 0, 0, 12, 0, off) == 0);
        free(rep.data);
    }
    CHECK(on[4] != off[4]);
    CHECK(splice(in, damaged, 12, &bad) == 0);
    b = bink2_open_memory(bad.data, (unsigned long)bad.size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    for (i = 0; i < 5; i++) {
        CHECK((i == 0 ? bink2_first(b) : bink2_next(b)) != BINK2_ERROR);
        CHECK(picture_hash(b, 0) == on[i]);
    }
    CHECK(bink2_next(b) == BINK2_ERROR);
    CHECK(picture_hash(b, 0) == on[4]);
    for (i = 6; i < 10; i++)
        bink2_next(b);
    CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(picture_hash(b, 0) == on[10]);
    CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(picture_hash(b, 0) == on[11]);
    bink2_close(b);
    free(bad.data);
    return 0;
}

/* The KB2j fixture's frame flags turn deblocking off (0x4000 plus a row
 * array of ones, or 0x4000 | 0x8000), so DB may touch nothing: every byte
 * of every plane, padding included, matches an unfiltered decode that
 * runs only stage DC after each frame. Stage DC is in-loop, so its output
 * is that decode's next reference too. The fixture has no DC-only blocks,
 * so DC changes nothing here either and the comparison would also hold
 * without the call; dcrecon_frame covers DC's own bounds. */
static int test_border_planes_bounded(void)
{
    Blob in;
    bink2 b[2];
    int f, i, p;

    CHECK(load_blob(BINK2_FIXTURE_DIR "/synthetic_kb2j.bk2", &in) == 0);
    for (i = 0; i < 2; i++) {
        b[i] = bink2_open_memory(in.data, (unsigned long)in.size);
        CHECK(b[i] != NULL);
        bink2_enable_video(b[i], 1);
        bink2_enable_filters(b[i], (unsigned char)(i == 0));
    }
    for (f = 0; f < 32; f++) {
        for (i = 0; i < 2; i++)
            CHECK((f == 0 ? bink2_first(b[i]) : bink2_next(b[i])) !=
                  BINK2_ERROR);
        bink2_filter_dcrecon(b[1]->video);
        for (p = 0; p < 3; p++) {
            const Bink2Video *v = b[0]->video;

            CHECK(memcmp(v->cur[p], b[1]->video->cur[p],
                         plane_bytes(v, p)) == 0);
        }
    }
    for (i = 0; i < 2; i++)
        bink2_close(b[i]);
    free(in.data);
    return 0;
}

int main(void)
{
    Blob g, f, rep;
    int skip;

    CHECK(test_vertical_rules() == 0);
    CHECK(test_horizontal_rules() == 0);
    CHECK(test_deblock_bounds() == 0);
    CHECK(test_uncovered_revision() == 0);
    CHECK(test_dcrecon_frame() == 0);
    CHECK(test_dcrecon_gates() == 0);
    CHECK(test_dc_before_db() == 0);
    CHECK(test_edge_flags() == 0);
    CHECK(test_fixture_arrays() == 0);
    CHECK(test_border_planes_bounded() == 0);

    skip = load_sample("ge_video_86l.bk2", &g);
    if (skip)
        return skip;
    skip = load_sample("sc13_01_partial.bk2", &f);
    if (skip) {
        free(g.data);
        return skip;
    }
    CHECK(test_filters_change_output(&g, 10) == 0);
    CHECK(test_filters_change_output(&f, 20) == 0);
    CHECK(test_threads_match(&g, 10) == 0);
    CHECK(test_threads_match(&f, 20) == 0);
    CHECK(splice(&g, REPEAT, 20, &rep) == 0);
    CHECK(test_toggle_latches_at_keyframe(&rep) == 0);
    CHECK(test_seek_matches_linear(&rep) == 0);
    CHECK(test_error_restores_filtered(&g) == 0);
    free(rep.data);
    free(g.data);
    free(f.data);
    return 0;
}
