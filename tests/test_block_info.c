#include "bink2.h"
#include "bink2_bits.h"
#include "bink2_enc_syntax.h"
#include "bink2_filter.h"
#include "bink2_tables.h"
#include "bink2_video.h"
#include "test_common.h"

/* The per-8x8 block info words the slice decoders store for the filter
 * stages: hand-built KB2g and KB2f frames with known AC symbol counts per
 * block, checked word by word. */

static unsigned spec_class(int n)
{
    return n == 0 ? 3u : n < 4 ? 2u : n < 8 ? 1u : 0u;
}

static uint16_t word_at(const Bink2Video *v, int p, int bx, int by)
{
    return v->blk_info[p][by * v->blk_stride[p] + bx];
}

static int test_class_thresholds(void)
{
    static const int n[8] = { 0, 1, 3, 4, 7, 8, 63, 64 };
    static const unsigned want[8] = { 3, 2, 2, 1, 1, 0, 0, 0 };
    int i;

    for (i = 0; i < 8; i++)
        CHECK(bink2_video_ac_class(n[i]) == want[i]);
    return 0;
}

/* RAD's KB2f DC rounding: Y and alpha add 0.49999991f in single precision
 * before truncating, chroma truncates; both keep 12 bits. */
static int test_kb2f_dc_rounding(void)
{
    CHECK(bink2_video_old_intra_word(100.5f, 0, 1) == (0xE000u | 101));
    CHECK(bink2_video_old_intra_word(100.5f, 0, 0) == (0xE000u | 100));
    CHECK(bink2_video_old_intra_word(100.4f, 0, 1) == (0xE000u | 100));
    /* 1005.5 + 0.49999991f rounds to 1006.0f in single precision. */
    CHECK(bink2_video_old_intra_word(1005.5f, 0, 1) == (0xE000u | 1006));
    CHECK(bink2_video_old_intra_word(4095.75f, 4, 1) == (0xA000u | 0));
    CHECK(bink2_video_old_intra_word(4095.75f, 4, 0) == (0xA000u | 4095));
    return 0;
}

/* ---- KB2g: written with the encoder's syntax writers ---- */

/* Luma counts by decode index (group * 4 + block); 0 with the cbp bit set
 * is a coded block with no symbols. */
static const int G_LUMA[2][16] = {
    { 0, 0, 1, 2, 3, 4, 5, 7, 8, 9, 63, 1, 3, 4, 7, 8 },
    { 8, 7, 4, 3, 1, 63, 9, 8, 7, 5, 4, 3, 2, 1, 0, 0 },
};
static const uint32_t G_LUMA_CBP[2] = { 0xFFFE, 0x7FFF };
static const int G_U[4] = { 1, 8, 0, 4 };   /* comp 1, plane 2 */
static const int G_V[4] = { 3, 0, 7, 63 };  /* comp 2, plane 1 */

static void g_levels(int32_t levels[64], int n)
{
    int s;

    memset(levels, 0, 64 * sizeof(int32_t));
    for (s = 1; s <= n; s++)
        levels[bink2g_scan[s]] = (s & 1) ? 1 : -2;
}

static void g_put_luma(Bink2BitWriter *bw, uint32_t *prev, int mb, int q)
{
    const uint16_t (*qmat)[64] = bink2g_luma_intra_qmat;
    int32_t dcs[16], levels[64];
    int i;

    bink2_write_cbp_luma(bw, *prev, 0, G_LUMA_CBP[mb]);
    *prev = G_LUMA_CBP[mb];
    for (i = 0; i < 16; i++)
        dcs[i] = (i * 13 + mb * 5) % 40 - 20;
    bink2_write_dcs(bw, dcs, 16, q);
    for (i = 0; i < 16; i++) {
        g_levels(levels, G_LUMA[mb][i]);
        bink2_write_acs(bw, levels, qmat, q, (G_LUMA_CBP[mb] >> i) & 1);
    }
}

static void g_put_chroma(Bink2BitWriter *bw, uint32_t *prev,
                         const int counts[4], int q)
{
    int32_t dcs[16] = { 0 }, levels[64];
    int i;

    bink2_write_cbp_chroma(bw, *prev, 0xD);
    *prev = 0xD;
    for (i = 0; i < 4; i++)
        dcs[i] = 7 - 5 * i;
    bink2_write_dcs(bw, dcs, 4, q);
    for (i = 0; i < 4; i++) {
        g_levels(levels, counts[i]);
        bink2_write_acs(bw, levels, bink2g_chroma_intra_qmat, q,
                        (0xDu >> i) & 1);
    }
}

/* An intra macroblock after its quantiser: Y, then U (comp 1), then V. */
static void g_put_intra(Bink2BitWriter *bw, uint32_t prev[3], int mb, int q)
{
    g_put_luma(bw, &prev[0], mb, q);
    g_put_chroma(bw, &prev[1], G_U, q);
    g_put_chroma(bw, &prev[2], G_V, q);
}

static int decode_bw(Bink2Video *v, Bink2BitWriter *bw, int keyframe)
{
    bink2_bw_align32(bw);
    bink2_bw_put(bw, 0, 32);
    if (bink2_bw_failed(bw))
        return BINK2_ERROR;
    return bink2_video_decode(v, bink2_bw_data(bw), bink2_bw_bytes(bw),
                              keyframe);
}

/* Words of an intra macroblock against the DCs the decoder just used.
 * After the row the slice's dc_prev holds that row's DCs. */
static int check_g_intra(const Bink2Video *v, int mb)
{
    const Bink2OldDc *dc = &v->slice[0].dc_prev[mb];
    int i;

    for (i = 0; i < 16; i++) {
        int pos = bink2_luma_repos[i];
        int n = (G_LUMA_CBP[mb] >> i) & 1 ? G_LUMA[mb][i] : 0;
        unsigned want = 0x8000u | spec_class(n) << 13 |
                        ((unsigned)(int)dc->dc[0][i] & 0x1FFFu);

        CHECK(word_at(v, 0, mb * 4 + (pos & 3), pos >> 2) == want);
    }
    for (i = 0; i < 4; i++) {
        int coded = (0xD >> i) & 1;
        unsigned u = 0x8000u | spec_class(coded ? G_U[i] : 0) << 13 |
                     ((unsigned)(int)dc->dc[1][i] & 0x1FFFu);
        unsigned w = 0x8000u | spec_class(coded ? G_V[i] : 0) << 13 |
                     ((unsigned)(int)dc->dc[2][i] & 0x1FFFu);

        CHECK(word_at(v, 2, mb * 2 + (i & 1), i >> 1) == u);
        CHECK(word_at(v, 1, mb * 2 + (i & 1), i >> 1) == w);
    }
    return 0;
}

static int all_zero(const Bink2Video *v, int mb)
{
    int p, bx, by;

    for (p = 0; p < 3; p++) {
        int n = p ? 2 : 4;

        for (by = 0; by < n; by++)
            for (bx = 0; bx < n; bx++)
                CHECK(word_at(v, p, mb * n + bx, by) == 0);
    }
    return 0;
}

static int g_keyframe(Bink2Video *v, int q)
{
    Bink2BitWriter bw;
    uint32_t prev[3] = { 0, 0, 0 };
    int mb, ret;

    bink2_bw_init(&bw);
    bink2_bw_put(&bw, 0x2000, 32);
    for (mb = 0; mb < 2; mb++) {
        bink2_write_quant(&bw, mb == 0 ? 16 : q, q);
        g_put_intra(&bw, prev, mb, q);
    }
    ret = decode_bw(v, &bw, 1);
    bink2_bw_free(&bw);
    return ret;
}

static int test_kb2g_words(void)
{
    Bink2Video v;
    Bink2BitWriter bw;
    const int q = 12;
    int mb;

    CHECK(bink2_video_init(&v, 64, 32, 0, 'g') == 0);

    /* Keyframe: two intra macroblocks. */
    CHECK(g_keyframe(&v, q) == BINK2_OK);
    for (mb = 0; mb < 2; mb++)
        CHECK(check_g_intra(&v, mb) == 0);

    /* Inter frame: skip, then motion only. Both clear every word. */
    {
        uint8_t lru[4] = { 2, 3, 1, 0 };

        bink2_bw_init(&bw);
        bink2_bw_put(&bw, 0x2000, 32);
        bink2_write_btype(&bw, lru, 1);
        bink2_write_btype(&bw, lru, 2);
        bink2_write_mv1(&bw, 0, 0);
        CHECK(decode_bw(&v, &bw, 0) == BINK2_OK);
        bink2_bw_free(&bw);
        CHECK(all_zero(&v, 0) == 0);
        CHECK(all_zero(&v, 1) == 0);
    }

    /* Keyframe again, then motion + residual next to an intra macroblock
     * in an inter frame. */
    CHECK(g_keyframe(&v, q) == BINK2_OK);
    {
        uint8_t lru[4] = { 2, 3, 1, 0 };
        int32_t zero[16] = { 0 };
        uint32_t prev[3] = { 0, 0, 0 };

        bink2_bw_init(&bw);
        bink2_bw_put(&bw, 0x2000, 32);
        bink2_write_btype(&bw, lru, 3);
        bink2_write_mv1(&bw, 0, 0);
        bink2_write_quant(&bw, 0, 0);
        bink2_write_cbp_luma(&bw, 0, 0, 0);
        bink2_write_dcs(&bw, zero, 16, 16);
        bink2_bw_put(&bw, 0, 1);            /* no chroma */
        bink2_write_btype(&bw, lru, 0);
        bink2_write_quant(&bw, 16, q);
        g_put_intra(&bw, prev, 1, q);
        CHECK(decode_bw(&v, &bw, 0) == BINK2_OK);
        bink2_bw_free(&bw);
        CHECK(all_zero(&v, 0) == 0);
        CHECK(check_g_intra(&v, 1) == 0);
    }
    bink2_video_uninit(&v);
    return 0;
}

/* ---- stage R: a KB2g motion+residual macroblock ---- */

/* Luma AC symbol counts by decode index and the coded blocks; U and V
 * likewise. Uncoded and coded-empty blocks are class 3. */
static const int R_LUMA[16] = {
    0, 2, 5, 9, 1, 0, 3, 8, 4, 0, 2, 7, 0, 1, 6, 3
};
static const uint32_t R_LUMA_CBP = 0xDF7E;
static const int R_U[4] = { 2, 0, 9, 5 };
static const uint32_t R_U_CBP = 0xB;
static const int R_V[4] = { 1, 4, 0, 0 };
static const uint32_t R_V_CBP = 0x7;

static int r_pattern(int p, int x, int y)
{
    return 60 + (x * 7 + y * 13 + p * 29) % 131;
}

/* A 64x32 video whose previous picture is the pattern. */
static int r_video(Bink2Video *v, int filters)
{
    int p, x, y;

    CHECK(bink2_video_init(v, 64, 32, 0, 'g') == 0);
    v->filters_on = filters;
    for (p = 0; p < 3; p++)
        for (y = 0; y < (p ? 16 : 32); y++)
            for (x = 0; x < v->stride[p]; x++)
                v->cur[p][(size_t)y * v->stride[p] + x] =
                    (unsigned char)r_pattern(p, x, y);
    v->has_prev = 1;
    return 0;
}

static void r_put_blocks(Bink2BitWriter *bw, const int *counts, int n,
                         uint32_t cbp, int seed)
{
    int32_t dcs[16], levels[64];
    int i;

    for (i = 0; i < n; i++)
        dcs[i] = ((i * 37 + seed) % 17 - 8) * 7;
    bink2_write_dcs(bw, dcs, n, 0);
    for (i = 0; i < n; i++) {
        g_levels(levels, counts[i]);
        bink2_write_acs(bw, levels, bink2g_inter_qmat, 0, (cbp >> i) & 1);
    }
}

/* Inter frame: motion+residual with a zero vector, then a skip. */
static int r_frame(Bink2Video *v, uint32_t flags)
{
    Bink2BitWriter bw;
    uint8_t lru[4] = { 2, 3, 1, 0 };
    int ret;

    bink2_bw_init(&bw);
    bink2_bw_put(&bw, flags, 32);
    bink2_write_btype(&bw, lru, 3);
    bink2_write_mv1(&bw, 0, 0);
    bink2_write_quant(&bw, 0, 0);
    bink2_write_cbp_luma(&bw, 0, flags & 0x40000, R_LUMA_CBP);
    r_put_blocks(&bw, R_LUMA, 16, R_LUMA_CBP, 3);
    bink2_bw_put(&bw, 1, 1);                    /* chroma coded */
    bink2_write_cbp_chroma(&bw, 0, R_U_CBP);
    r_put_blocks(&bw, R_U, 4, R_U_CBP, 5);
    bink2_write_cbp_chroma(&bw, 0, R_V_CBP);
    r_put_blocks(&bw, R_V, 4, R_V_CBP, 11);
    bink2_write_btype(&bw, lru, 1);
    ret = decode_bw(v, &bw, 0);
    bink2_bw_free(&bw);
    return ret;
}

static unsigned r_word(float dc, int n)
{
    return spec_class(n) << 13 | ((unsigned)(int)dc & 0x1FFFu);
}

/* Plane p of macroblock 0 with filters on (a) against off (b): b minus
 * the pattern is the unfiltered residual, which must not have clamped.
 * Filtered with the blocks' words and added back it must give a. Every
 * other pixel is the skip macroblock's copy of the pattern. */
static int r_check_plane(const Bink2Video *a, const Bink2Video *b, int p,
                         unsigned dirs, int *changed)
{
    int n = p ? 2 : 4;
    int size = 8 * n;
    int comp = p == 0 ? 0 : p == 2 ? 1 : 2;
    const int *counts = p == 0 ? R_LUMA : p == 2 ? R_U : R_V;
    uint32_t cbp = p == 0 ? R_LUMA_CBP : p == 2 ? R_U_CBP : R_V_CBP;
    const float *dc = b->slice[0].dc_prev[0].dc[comp];
    int16_t res[32 * 32];
    unsigned words[16];
    int stride = a->stride[p];
    int x, y, i;

    for (i = 0; i < n * n; i++) {
        int pos = p ? i : bink2_luma_repos[i];
        int cnt = (cbp >> i) & 1 ? counts[i] : 0;

        CHECK(dc[i] == a->slice[0].dc_prev[0].dc[comp][i]);
        words[pos] = r_word(dc[i], cnt);
    }
    for (y = 0; y < size; y++)
        for (x = 0; x < size; x++) {
            int got = b->cur[p][(size_t)y * stride + x];

            CHECK(got > 0 && got < 255);
            res[y * 32 + x] = (int16_t)(got - r_pattern(p, x, y));
        }
    bink2_filter_residual(res, 32, n, words, dirs);
    for (y = 0; y < (p ? 16 : 32); y++)
        for (x = 0; x < stride; x++) {
            int want = r_pattern(p, x, y);
            int got = a->cur[p][(size_t)y * stride + x];

            if (x < size) {
                want += res[y * 32 + x];
                want = want < 0 ? 0 : want > 255 ? 255 : want;
            }
            CHECK(got == want);
            *changed |= got != b->cur[p][(size_t)y * stride + x];
        }
    return 0;
}

static int test_kb2g_residual_filter(void)
{
    static const struct {
        uint32_t flags;
        unsigned dirs;
    } cases[4] = {
        { 0x2000, BINK2_RES_VERTICAL | BINK2_RES_HORIZONTAL },
        { 0x6000, BINK2_RES_HORIZONTAL },
        { 0xA000, BINK2_RES_VERTICAL },
        { 0x42000, 0 },
    };
    int c, p;

    for (c = 0; c < 4; c++) {
        Bink2Video a, b;
        int changed[3] = { 0, 0, 0 };

        CHECK(r_video(&a, 1) == 0);
        CHECK(r_video(&b, 0) == 0);
        CHECK(r_frame(&a, cases[c].flags) == BINK2_OK);
        CHECK(r_frame(&b, cases[c].flags) == BINK2_OK);
        for (p = 0; p < 3; p++)
            CHECK(r_check_plane(&a, &b, p, cases[c].dirs,
                                &changed[p]) == 0);
        for (p = 0; p < 3; p++)
            CHECK(changed[p] == (cases[c].dirs != 0));
        bink2_video_uninit(&a);
        bink2_video_uninit(&b);
    }
    return 0;
}

/* ---- KB2f: written by hand from the old-syntax readers ---- */

/* One AC block: value levels at consecutive positions from 1, each
 * followed by a skip symbol of skip[k] positions (-1: the terminating
 * skip of 62). */
typedef struct {
    int count;
    int level[63];
    int skip[63];
    int nonzero;    /* what the class counts */
} FBlock;

static void f_put_block(Bink2BitWriter *bw, const FBlock *b)
{
    int k, idx = 1;

    for (k = 0; k < b->count; k++) {
        int mag = b->level[k] < 0 ? -b->level[k] : b->level[k];

        bink2_bw_put(bw, bink2f_ac_val_codes[0][mag],
                     bink2f_ac_val_bits[0][mag]);
        if (mag)
            bink2_bw_put(bw, b->level[k] < 0, 1);
        if (idx > 62)
            break;
        idx++;
        if (b->skip[k] < 0) {
            bink2_bw_put(bw, bink2f_ac_skip_codes[0][12],
                         bink2f_ac_skip_bits[0][12]);
            break;
        }
        bink2_bw_put(bw, bink2f_ac_skip_codes[0][b->skip[k]],
                     bink2f_ac_skip_bits[0][b->skip[k]]);
        idx += b->skip[k];
    }
}

static FBlock f_block(const int *levels, int count, int skip_at, int skip)
{
    FBlock b;
    int k;

    memset(&b, 0, sizeof(b));
    b.count = count;
    for (k = 0; k < count; k++) {
        b.level[k] = levels[k];
        b.skip[k] = k + 1 == count ? -1 : k == skip_at ? skip : 0;
        b.nonzero += levels[k] != 0;
    }
    return b;
}

static const int F_ONES[63] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
static const int F_ZERO[1] = { 0 };
static const int F_3_OF_5[5] = { 1, 0, -1, 0, 3 };
static const int F_7_OF_13[13] = { 2, 0, -2, 0, 2, 0, 2, 0, -3, 0, 2, 0, 1 };
static const int F_1_LATE[4] = { 0, 0, 0, -1 };

/* 16 luma blocks by decode index plus U and V; cbp bit 0 of luma is
 * clear. */
static void f_blocks(FBlock luma[16], FBlock u[4], FBlock v[4])
{
    luma[0] = f_block(F_ONES, 0, -1, 0);          /* not coded */
    luma[1] = f_block(F_ZERO, 1, -1, 0);          /* one zero symbol */
    luma[2] = f_block(F_ONES, 1, -1, 0);
    luma[3] = f_block(F_3_OF_5, 5, -1, 0);        /* 3 of 5 symbols */
    luma[4] = f_block(F_ONES, 4, -1, 0);
    luma[5] = f_block(F_7_OF_13, 13, -1, 0);      /* 7 of 13 */
    luma[6] = f_block(F_ONES, 8, -1, 0);
    luma[7] = f_block(F_ONES, 63, -1, 0);         /* every position */
    luma[8] = f_block(F_1_LATE, 4, -1, 0);
    luma[9] = f_block(F_ONES, 3, 0, 5);           /* a skip of 5 */
    luma[10] = f_block(F_ONES, 7, 2, 10);         /* a skip of 10 */
    luma[11] = f_block(F_3_OF_5, 5, 1, 3);
    luma[12] = f_block(F_ONES, 2, -1, 0);
    luma[13] = f_block(F_7_OF_13, 13, 4, 9);
    luma[14] = f_block(F_ZERO, 1, -1, 0);
    luma[15] = f_block(F_ONES, 9, -1, 0);
    u[0] = f_block(F_ONES, 1, -1, 0);
    u[1] = f_block(F_7_OF_13, 13, -1, 0);
    u[2] = f_block(F_ZERO, 1, -1, 0);
    u[3] = f_block(F_ONES, 8, -1, 0);
    v[0] = f_block(F_3_OF_5, 5, -1, 0);
    v[1] = f_block(F_ONES, 4, -1, 0);
    v[2] = f_block(F_ONES, 0, -1, 0);             /* not coded */
    v[3] = f_block(F_ONES, 63, -1, 0);
}

/* Explicit cbp with no high (second codebook) bits. */
static void f_put_cbp_luma(Bink2BitWriter *bw, unsigned cbp)
{
    int i, j;

    bink2_bw_put(bw, 0, 1);
    for (i = 0; i < 4; i++) {
        bink2_bw_put(bw, 0, 1);
        bink2_bw_put(bw, (cbp >> (4 * i)) & 0xF, 4);
    }
    for (i = 0; i < 4; i++) {
        unsigned nib = (cbp >> (4 * i)) & 0xF;
        int ones = (int)(nib & 1) + (int)((nib >> 1) & 1) +
                   (int)((nib >> 2) & 1) + (int)((nib >> 3) & 1);

        if (ones > 1)
            bink2_bw_put(bw, 0, 1);
        for (j = 0; j < ones; j++)
            bink2_bw_put(bw, 0, 1);
    }
}

static void f_put_cbp_chroma(Bink2BitWriter *bw, unsigned cbp)
{
    int ones = (int)(cbp & 1) + (int)((cbp >> 1) & 1) +
               (int)((cbp >> 2) & 1) + (int)((cbp >> 3) & 1);
    int j;

    bink2_bw_put(bw, 0, 1);
    bink2_bw_put(bw, cbp, 4);
    if (ones > 1)
        bink2_bw_put(bw, 0, 1);
    for (j = 0; j < ones; j++)
        bink2_bw_put(bw, 0, 1);
}

/* q stays 8 (delta 0). dc_bits = 2, then the first macroblock's extra
 * 6-bit DC term. */
static void f_put_dc(Bink2BitWriter *bw, int count, int seed)
{
    int i, j;

    bink2_bw_put(bw, 1, 1);                 /* delta q 0 */
    bink2_bw_put(bw, 2, 3);
    for (j = 0; j < count; j += 4) {
        int value[4];

        for (i = 0; i < 4; i++) {
            value[i] = (seed + 3 * (i + j)) % 4;
            bink2_bw_put(bw, (unsigned)value[i], 2);
        }
        for (i = 0; i < 4; i++)
            if (value[i] != 0)
                bink2_bw_put(bw, (unsigned)((i + j) & 1), 1);
    }
    bink2_bw_put(bw, 20, 6);
    bink2_bw_put(bw, 0, 1);
}

static int f_frame(Bink2Video *v, int keyframe)
{
    Bink2BitWriter bw;
    FBlock luma[16], u[4], w[4];
    unsigned luma_cbp = 0, u_cbp = 0, v_cbp = 0;
    size_t slice1;
    int i, ret;

    f_blocks(luma, u, w);
    for (i = 0; i < 16; i++)
        if (i != 0)
            luma_cbp |= 1u << i;
    u_cbp = 0xF;
    v_cbp = 0xB;
    bink2_bw_init(&bw);
    bink2_bw_put(&bw, 0xC000, 32);
    bink2_bw_put(&bw, 0, 32);               /* slice 1 offset, below */
    if (!keyframe) {
        bink2_bw_put(&bw, 1, 2);            /* skip */
    } else {
        f_put_cbp_luma(&bw, luma_cbp);
        f_put_dc(&bw, 16, 1);
        for (i = 0; i < 16; i++)
            if ((luma_cbp >> i) & 1)
                f_put_block(&bw, &luma[i]);
        f_put_cbp_chroma(&bw, u_cbp);
        f_put_dc(&bw, 4, 2);
        for (i = 0; i < 4; i++)
            if ((u_cbp >> i) & 1)
                f_put_block(&bw, &u[i]);
        f_put_cbp_chroma(&bw, v_cbp);
        f_put_dc(&bw, 4, 3);
        for (i = 0; i < 4; i++)
            if ((v_cbp >> i) & 1)
                f_put_block(&bw, &w[i]);
    }
    bink2_bw_align32(&bw);
    slice1 = bink2_bw_bytes(&bw);           /* slice 1 has no rows */
    bink2_bw_put(&bw, 0, 32);
    if (bink2_bw_failed(&bw))
        return BINK2_ERROR;
    {
        unsigned char *d = (unsigned char *)bink2_bw_data(&bw);

        d[4] = (unsigned char)(slice1 & 0xFF);
        d[5] = (unsigned char)((slice1 >> 8) & 0xFF);
    }
    ret = bink2_video_decode(v, bink2_bw_data(&bw), bink2_bw_bytes(&bw),
                             keyframe);
    bink2_bw_free(&bw);
    return ret;
}

static int test_kb2f_words(void)
{
    Bink2Video v;
    FBlock luma[16], u[4], w[4];
    const Bink2OldDc *dc;
    int i;

    f_blocks(luma, u, w);
    CHECK(bink2_video_init(&v, 32, 32, 0, 'f') == 0);
    CHECK(f_frame(&v, 1) == BINK2_OK);
    dc = &v.slice[0].dc_prev[0];
    for (i = 0; i < 16; i++) {
        int pos = bink2_luma_repos[i];
        int n = i == 0 ? 0 : luma[i].nonzero;
        unsigned want = 0x8000u | spec_class(n) << 13 |
                        ((unsigned)(int)(dc->dc[0][i] + 0.49999991f) &
                         0xFFFu);

        CHECK(word_at(&v, 0, pos & 3, pos >> 2) == want);
    }
    for (i = 0; i < 4; i++) {
        int nv = i == 2 ? 0 : w[i].nonzero;
        unsigned want_u = 0x8000u | spec_class(u[i].nonzero) << 13 |
                          ((unsigned)(int)dc->dc[1][i] & 0xFFFu);
        unsigned want_v = 0x8000u | spec_class(nv) << 13 |
                          ((unsigned)(int)dc->dc[2][i] & 0xFFFu);

        CHECK(word_at(&v, 2, i & 1, i >> 1) == want_u);
        CHECK(word_at(&v, 1, i & 1, i >> 1) == want_v);
    }
    /* The zero-level symbols decide these classes. */
    CHECK((word_at(&v, 0, 1, 0) >> 13 & 3) == 3);    /* idx 1: 0 of 1 */
    CHECK(luma[3].nonzero == 3 && luma[5].nonzero == 7);

    /* A skip macroblock clears the words. */
    CHECK(f_frame(&v, 0) == BINK2_OK);
    CHECK(all_zero(&v, 0) == 0);
    bink2_video_uninit(&v);
    return 0;
}

int main(void)
{
    CHECK(test_class_thresholds() == 0);
    CHECK(test_kb2f_dc_rounding() == 0);
    CHECK(test_kb2g_words() == 0);
    CHECK(test_kb2f_words() == 0);
    CHECK(test_kb2g_residual_filter() == 0);
    return 0;
}
