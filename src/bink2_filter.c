#include "bink2_filter.h"
#include "bink2_video.h"

int bink2_filter_covers(int version, int stage)
{
    /* RAD's studied decoder rejects KB2l and KB2n, so nothing is known
     * about them. */
    int known = version >= 'f' && version <= 'm' && version != 'l';

    switch (stage) {
    case BINK2_STAGE_DEBLOCK:
    case BINK2_STAGE_DCRECON:
        return known;
    case BINK2_STAGE_RESIDUAL:
        /* KB2f inter syntax has no residual filter to reproduce. */
        return known && version != 'f';
    default:
        return 0;
    }
}

unsigned bink2_filter_db_class_mask(int class_p, int class_q)
{
    unsigned mask = 0;

    if (class_p == 3 && class_q == 3)
        return 0;
    if (class_p >= 2)
        mask |= BINK2_DB_TAP_P1;
    if (class_p >= 1)
        mask |= BINK2_DB_TAP_P0;
    if (class_q >= 1)
        mask |= BINK2_DB_TAP_Q0;
    if (class_q >= 2)
        mask |= BINK2_DB_TAP_Q1;
    return mask;
}

int bink2_filter_mv_differ(int ax, int ay, int bx, int by)
{
    int dx = ax - bx;
    int dy = ay - by;

    return dx >= 2 || dx <= -2 || dy >= 2 || dy <= -2;
}

/* Division rounding towards minus infinity, for a positive divisor; C99's
 * '/' truncates and '>>' on a negative value is implementation-defined. */
static int floor_div(int x, int k)
{
    return x >= 0 ? x / k : -((k - 1 - x) / k);
}

static unsigned char clamp255(int v)
{
    return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
}

void bink2_filter_db_edge(unsigned char *q0, int across, int along,
                          unsigned mask)
{
    int line;

    if (!(mask & BINK2_DB_TAPS_ALL))
        return;
    for (line = 0; line < 8; line++, q0 += along) {
        unsigned char *p1 = q0 - 2 * across;
        unsigned char *p0 = q0 - across;
        unsigned char *q1 = q0 + across;
        int d = *q0 - *p0;
        int a = floor_div(d + 4, 8);
        int b = floor_div(d + 2, 4);

        /* Every tap uses the line's original p0 and q0 through d. */
        if (mask & BINK2_DB_TAP_P1)
            *p1 = clamp255(*p1 + a);
        if (mask & BINK2_DB_TAP_P0)
            *p0 = clamp255(*p0 + b);
        if (mask & BINK2_DB_TAP_Q0)
            *q0 = clamp255(*q0 - b);
        if (mask & BINK2_DB_TAP_Q1)
            *q1 = clamp255(*q1 - a);
    }
}

/* Motion vector of the 16x16 luma quadrant holding luma pixel (x, y). */
static void quadrant_mv(const Bink2Video *v, int x, int y, int *mx, int *my)
{
    size_t k = ((size_t)(y >> 5) * v->mb_w + (x >> 5)) * 4 +
               (size_t)(((x >> 4) & 1) | (((y >> 4) & 1) << 1));

    *mx = v->mv[0][k];
    *my = v->mv[1][k];
}

/* Taps for the edge segment between blocks with words wp and wq. (px, py)
 * and (qx, qy) are luma pixels on either side of the edge, used when both
 * macroblocks are non-intra; on16 says the edge is on a 16-pixel luma
 * boundary (every chroma edge is). */
static unsigned edge_mask(const Bink2Video *v, unsigned wp, unsigned wq,
                          int on16, int px, int py, int qx, int qy)
{
    int ax, ay, bx, by;

    if ((wp | wq) & BINK2_INFO_INTRA)
        return bink2_filter_db_class_mask(
            (int)(wp >> BINK2_INFO_CLASS_SHIFT) & 3,
            (int)(wq >> BINK2_INFO_CLASS_SHIFT) & 3);
    if (!on16)
        return 0;
    quadrant_mv(v, px, py, &ax, &ay);
    quadrant_mv(v, qx, qy, &bx, &by);
    return bink2_filter_mv_differ(ax, ay, bx, by) ? BINK2_DB_TAPS_ALL : 0;
}

/* One direction of one plane. Block (j, i) is the jth 8x8 block of the
 * ith block row; the edge before it lies on luma column or row 8j << s or
 * 8i << s, which also indexes the edge arrays. */
static void deblock_plane(Bink2Video *v, int p, int vertical)
{
    int s = p ? 1 : 0;
    int cols = (int)(v->aligned_w >> 3) >> s;
    int rows = (int)(((v->height + 15) & ~15UL) >> 3) >> s;
    int stride = v->stride[p];
    const uint16_t *info = v->blk_info[p];
    int bs = v->blk_stride[p];
    int i, j;

    for (i = vertical ? 0 : 1; i < rows; i++) {
        int ly = (8 * i) << s;

        if (!vertical && v->row_flags[i << s])
            continue;
        for (j = vertical ? 1 : 0; j < cols; j++) {
            int lx = (8 * j) << s;
            unsigned char *q0 = v->cur[p] + (size_t)(8 * i) * stride + 8 * j;
            unsigned mask;

            if (vertical) {
                if (v->col_flags[j << s])
                    continue;
                mask = edge_mask(v, info[i * bs + j - 1], info[i * bs + j],
                                 s || !(j & 1), lx - 1, ly, lx, ly);
                if (mask)
                    bink2_filter_db_edge(q0, 1, stride, mask);
            } else {
                mask = edge_mask(v, info[(i - 1) * bs + j], info[i * bs + j],
                                 s || !(i & 1), lx, ly - 1, lx, ly);
                if (mask)
                    bink2_filter_db_edge(q0, stride, 1, mask);
            }
        }
    }
}

void bink2_filter_deblock(Bink2Video *v)
{
    int p;

    /* Every vertical edge before any horizontal one. */
    if (!(v->frame_flags & 0x4000))
        for (p = 0; p < 3; p++)
            deblock_plane(v, p, 1);
    if (!(v->frame_flags & 0x8000))
        for (p = 0; p < 3; p++)
            deblock_plane(v, p, 0);
}

unsigned bink2_filter_residual_dirs(const Bink2Video *v)
{
    unsigned dirs = 0;

    if (!v->filters_on ||
        !bink2_filter_covers(v->version, BINK2_STAGE_RESIDUAL) ||
        !(v->frame_flags & 0x2000) || (v->frame_flags & 0x40000))
        return 0;
    if (!(v->frame_flags & 0x4000))
        dirs |= BINK2_RES_VERTICAL;
    if (!(v->frame_flags & 0x8000))
        dirs |= BINK2_RES_HORIZONTAL;
    return dirs;
}

unsigned bink2_filter_residual_word(int dc, int symbols)
{
    return (bink2_video_ac_class(symbols) << BINK2_INFO_CLASS_SHIFT) |
           ((unsigned)dc & BINK2_INFO_DC_MASK);
}

/* The value of x in 16-bit two's complement. */
static int wrap16(int x)
{
    x &= 0xFFFF;
    return x >= 0x8000 ? x - 0x10000 : x;
}

/* The signed 13-bit DC of a word. */
static int word_dc(unsigned w)
{
    int dc = (int)(w & BINK2_INFO_DC_MASK);

    return dc >= 0x1000 ? dc - 0x2000 : dc;
}

void bink2_filter_residual_edge(int16_t *r2, int across, int along,
                                unsigned wa, unsigned wb)
{
    int ca = (int)(wa >> BINK2_INFO_CLASS_SHIFT) & 3;
    int cb = (int)(wb >> BINK2_INFO_CLASS_SHIFT) & 3;
    int line;

    if (ca == 0 && cb == 0)
        return;
    if (ca == 3 && cb == 3) {
        int diff = word_dc(wa) - word_dc(wb);

        if (diff >= -32 && diff <= 32)
            return;
    }
    for (line = 0; line < 8; line++, r2 += along) {
        int16_t *r0 = r2 - 2 * across;
        int16_t *r1 = r2 - across;
        int16_t *r3 = r2 + across;
        int d = wrap16(*r2 - *r1);
        int a = floor_div(wrap16(d + 4), 8);
        int b = floor_div(wrap16(d + 2), 4);

        /* a and b come from the line's original r1 and r2. */
        if (ca >= 2)
            *r0 = (int16_t)wrap16(*r0 + a);
        if (ca >= 1)
            *r1 = (int16_t)wrap16(*r1 + b);
        if (cb >= 1)
            *r2 = (int16_t)wrap16(*r2 - b);
        if (cb >= 2)
            *r3 = (int16_t)wrap16(*r3 - a);
    }
}

void bink2_filter_residual(int16_t *res, int stride, int n,
                           const unsigned *words, unsigned dirs)
{
    int bx, by;

    /* Footprints within one direction never overlap, so only vertical
     * before horizontal matters. */
    if (dirs & BINK2_RES_VERTICAL)
        for (by = 0; by < n; by++)
            for (bx = 1; bx < n; bx++)
                bink2_filter_residual_edge(res + (size_t)(8 * by) * stride +
                                           8 * bx, 1, stride,
                                           words[by * n + bx - 1],
                                           words[by * n + bx]);
    if (dirs & BINK2_RES_HORIZONTAL)
        for (by = 1; by < n; by++)
            for (bx = 0; bx < n; bx++)
                bink2_filter_residual_edge(res + (size_t)(8 * by) * stride +
                                           8 * bx, stride, 1,
                                           words[(by - 1) * n + bx],
                                           words[by * n + bx]);
}

/* ---- Stage DC ---- */

enum { N_UL, N_U, N_UR, N_L, N_C, N_R, N_DL, N_D, N_DR };

/* floor(v * k / 65536). */
static int mul_high(int v, int k)
{
    return floor_div(v * k, 65536);
}

/* The left part Lw(j, d) of the integer weights at position j = 0..7.
 * The right part Rw(j, d) is the mirror image, Lw(7 - j, d). */
static int dc_weight(int j, int d)
{
    switch (j) {
    case 0: return mul_high(d, 0x6800);
    case 1: return mul_high(d, 0x4800);
    case 2: return mul_high(d, 0x3000);
    case 3: return mul_high(d, 0x1C00);
    case 4: return floor_div(d, 16);
    case 5: return floor_div(d, 32);
    case 6: return floor_div(d, 128);
    default: return 0;
    }
}

/* One weighted term of the float form: the substituted DC of neighbour n
 * times w. */
typedef struct {
    unsigned char n;
    float w;
} Bink2DcTerm;

typedef struct {
    int count;
    Bink2DcTerm term[8];
} Bink2DcPixel;

/* KB2f weights per pixel, row by row, each in summation order. */
static const Bink2DcPixel dc_float_weights[64] = {
    /* row 0 */
    { 4, { { N_UL, 0.0241113249f }, { N_L, 0.0308280513f },
           { N_U, 0.0308280513f }, { N_C, 0.0392670147f } } },
    { 4, { { N_UL, 0.0175668225f }, { N_L, 0.0225957558f },
           { N_U, 0.0373725556f }, { N_C, 0.0474993102f } } },
    { 4, { { N_UL, 0.0115389917f }, { N_L, 0.0149834668f },
           { N_U, 0.0434003845f }, { N_C, 0.0551116019f } } },
    { 6, { { N_UL, 0.00637227902f }, { N_L, 0.00840451941f },
           { N_U, 0.0461559668f }, { N_C, 0.0584182963f },
           { N_UR, 0.00241113245f }, { N_R, 0.00327225123f } } },
    { 6, { { N_UL, 0.00241113245f }, { N_L, 0.00327225123f },
           { N_U, 0.0461559668f }, { N_C, 0.0584182963f },
           { N_UR, 0.00637227902f }, { N_R, 0.00840451941f } } },
    { 4, { { N_U, 0.0434003845f }, { N_C, 0.0551116019f },
           { N_UR, 0.0115389917f }, { N_R, 0.0149834668f } } },
    { 4, { { N_U, 0.0373725556f }, { N_C, 0.0474993102f },
           { N_UR, 0.0175668225f }, { N_R, 0.0225957558f } } },
    { 4, { { N_U, 0.0308280513f }, { N_C, 0.0392670147f },
           { N_UR, 0.0241113249f }, { N_R, 0.0308280513f } } },
    /* row 1 */
    { 4, { { N_UL, 0.0175668225f }, { N_L, 0.0373725556f },
           { N_U, 0.0225957558f }, { N_C, 0.0474993102f } } },
    { 4, { { N_UL, 0.0126756681f }, { N_L, 0.027486911f },
           { N_U, 0.027486911f }, { N_C, 0.0573849566f } } },
    { 4, { { N_UL, 0.00819785055f }, { N_L, 0.018324608f },
           { N_U, 0.0319647305f }, { N_C, 0.0665472597f } } },
    { 6, { { N_UL, 0.0044089281f }, { N_L, 0.0103678703f },
           { N_U, 0.0341691934f }, { N_C, 0.0704050735f },
           { N_UR, 0.00158445851f }, { N_R, 0.00409892527f } } },
    { 6, { { N_UL, 0.00158445851f }, { N_L, 0.00409892527f },
           { N_U, 0.0341691934f }, { N_C, 0.0704050735f },
           { N_UR, 0.0044089281f }, { N_R, 0.0103678703f } } },
    { 4, { { N_U, 0.0319647305f }, { N_C, 0.0665472597f },
           { N_UR, 0.00819785055f }, { N_R, 0.018324608f } } },
    { 4, { { N_U, 0.027486911f }, { N_C, 0.0573849566f },
           { N_UR, 0.0126756681f }, { N_R, 0.027486911f } } },
    { 4, { { N_U, 0.0225957558f }, { N_C, 0.0474993102f },
           { N_UR, 0.0175668225f }, { N_R, 0.0373725556f } } },
    /* row 2 */
    { 4, { { N_UL, 0.0115389917f }, { N_L, 0.0434003845f },
           { N_U, 0.0149834668f }, { N_C, 0.0551116019f } } },
    { 4, { { N_UL, 0.00819785055f }, { N_L, 0.0319647305f },
           { N_U, 0.018324608f }, { N_C, 0.0665472597f } } },
    { 4, { { N_UL, 0.00516671268f }, { N_L, 0.0213557463f },
           { N_U, 0.0213557463f }, { N_C, 0.0771562383f } } },
    { 6, { { N_UL, 0.00265224581f }, { N_L, 0.0121245524f },
           { N_U, 0.0230090935f }, { N_C, 0.0815651715f },
           { N_UR, 0.00086111878f }, { N_R, 0.00482226489f } } },
    { 6, { { N_UL, 0.00086111878f }, { N_L, 0.00482226489f },
           { N_U, 0.0230090935f }, { N_C, 0.0815651715f },
           { N_UR, 0.00265224581f }, { N_R, 0.0121245524f } } },
    { 4, { { N_U, 0.0213557463f }, { N_C, 0.0771562383f },
           { N_UR, 0.00516671268f }, { N_R, 0.0213557463f } } },
    { 4, { { N_U, 0.018324608f }, { N_C, 0.0665472597f },
           { N_UR, 0.00819785055f }, { N_R, 0.0319647305f } } },
    { 4, { { N_U, 0.0149834668f }, { N_C, 0.0551116019f },
           { N_UR, 0.0115389917f }, { N_R, 0.0434003845f } } },
    /* row 3 */
    { 6, { { N_UL, 0.00637227902f }, { N_L, 0.0461559668f },
           { N_DL, 0.00241113245f }, { N_U, 0.00840451941f },
           { N_C, 0.0584182963f }, { N_D, 0.00327225123f } } },
    { 6, { { N_UL, 0.0044089281f }, { N_L, 0.0341691934f },
           { N_DL, 0.00158445851f }, { N_U, 0.0103678703f },
           { N_C, 0.0704050735f }, { N_D, 0.00409892527f } } },
    { 6, { { N_UL, 0.00265224581f }, { N_L, 0.0230090935f },
           { N_DL, 0.00086111878f }, { N_U, 0.0121245524f },
           { N_C, 0.0815651715f }, { N_D, 0.00482226489f } } },
    { 8, { { N_UL, 0.00124001107f }, { N_L, 0.0132267848f },
           { N_DL, 0.000310002768f }, { N_U, 0.0132267848f },
           { N_C, 0.0859740973f }, { N_D, 0.00537338108f },
           { N_UR, 0.000310002768f }, { N_R, 0.00537338108f } } },
    { 8, { { N_UL, 0.000310002768f }, { N_L, 0.00537338108f },
           { N_U, 0.0132267848f }, { N_C, 0.0859740973f },
           { N_D, 0.00537338108f }, { N_UR, 0.00124001107f },
           { N_R, 0.0132267848f }, { N_DR, 0.000310002768f } } },
    { 6, { { N_U, 0.0121245524f }, { N_C, 0.0815651715f },
           { N_D, 0.00482226489f }, { N_UR, 0.00265224581f },
           { N_R, 0.0230090935f }, { N_DR, 0.00086111878f } } },
    { 6, { { N_U, 0.0103678703f }, { N_C, 0.0704050735f },
           { N_D, 0.00409892527f }, { N_UR, 0.0044089281f },
           { N_R, 0.0341691934f }, { N_DR, 0.00158445851f } } },
    { 6, { { N_U, 0.00840451941f }, { N_C, 0.0584182963f },
           { N_D, 0.00327225123f }, { N_UR, 0.00637227902f },
           { N_R, 0.0461559668f }, { N_DR, 0.00241113245f } } },
    /* row 4 */
    { 6, { { N_UL, 0.00241113245f }, { N_L, 0.0461559668f },
           { N_DL, 0.00637227902f }, { N_U, 0.00327225123f },
           { N_C, 0.0584182963f }, { N_D, 0.00840451941f } } },
    { 6, { { N_UL, 0.00158445851f }, { N_L, 0.0341691934f },
           { N_DL, 0.0044089281f }, { N_U, 0.00409892527f },
           { N_C, 0.0704050735f }, { N_D, 0.0103678703f } } },
    { 6, { { N_UL, 0.00086111878f }, { N_L, 0.0230090935f },
           { N_DL, 0.00265224581f }, { N_U, 0.00482226489f },
           { N_C, 0.0815651715f }, { N_D, 0.0121245524f } } },
    { 8, { { N_UL, 0.000310002768f }, { N_L, 0.0132267848f },
           { N_DL, 0.00124001107f }, { N_U, 0.00537338108f },
           { N_C, 0.0859740973f }, { N_D, 0.0132267848f },
           { N_R, 0.00537338108f }, { N_DR, 0.000310002768f } } },
    { 8, { { N_DL, 0.000310002768f }, { N_L, 0.00537338108f },
           { N_U, 0.00537338108f }, { N_C, 0.0859740973f },
           { N_D, 0.0132267848f }, { N_UR, 0.000310002768f },
           { N_R, 0.0132267848f }, { N_DR, 0.00124001107f } } },
    { 6, { { N_U, 0.00482226489f }, { N_C, 0.0815651715f },
           { N_D, 0.0121245524f }, { N_UR, 0.00086111878f },
           { N_R, 0.0230090935f }, { N_DR, 0.00265224581f } } },
    { 6, { { N_U, 0.00409892527f }, { N_C, 0.0704050735f },
           { N_D, 0.0103678703f }, { N_UR, 0.00158445851f },
           { N_R, 0.0341691934f }, { N_DR, 0.0044089281f } } },
    { 6, { { N_U, 0.00327225123f }, { N_C, 0.0584182963f },
           { N_D, 0.00840451941f }, { N_UR, 0.00241113245f },
           { N_R, 0.0461559668f }, { N_DR, 0.00637227902f } } },
    /* row 5 */
    { 4, { { N_DL, 0.0115389917f }, { N_L, 0.0434003845f },
           { N_C, 0.0551116019f }, { N_D, 0.0149834668f } } },
    { 4, { { N_DL, 0.00819785055f }, { N_L, 0.0319647305f },
           { N_C, 0.0665472597f }, { N_D, 0.018324608f } } },
    { 4, { { N_DL, 0.00516671268f }, { N_L, 0.0213557463f },
           { N_C, 0.0771562383f }, { N_D, 0.0213557463f } } },
    { 6, { { N_DL, 0.00265224581f }, { N_L, 0.0121245524f },
           { N_C, 0.0815651715f }, { N_D, 0.0230090935f },
           { N_R, 0.00482226489f }, { N_DR, 0.00086111878f } } },
    { 6, { { N_DL, 0.00086111878f }, { N_L, 0.00482226489f },
           { N_C, 0.0815651715f }, { N_D, 0.0230090935f },
           { N_R, 0.0121245524f }, { N_DR, 0.00265224581f } } },
    { 4, { { N_C, 0.0771562383f }, { N_D, 0.0213557463f },
           { N_R, 0.0213557463f }, { N_DR, 0.00516671268f } } },
    { 4, { { N_C, 0.0665472597f }, { N_D, 0.018324608f },
           { N_R, 0.0319647305f }, { N_DR, 0.00819785055f } } },
    { 4, { { N_C, 0.0551116019f }, { N_D, 0.0149834668f },
           { N_R, 0.0434003845f }, { N_DR, 0.0115389917f } } },
    /* row 6 */
    { 4, { { N_DL, 0.0175668225f }, { N_L, 0.0373725556f },
           { N_C, 0.0474993102f }, { N_D, 0.0225957558f } } },
    { 4, { { N_DL, 0.0126756681f }, { N_L, 0.027486911f },
           { N_C, 0.0573849566f }, { N_D, 0.027486911f } } },
    { 4, { { N_DL, 0.00819785055f }, { N_L, 0.018324608f },
           { N_C, 0.0665472597f }, { N_D, 0.0319647305f } } },
    { 6, { { N_DL, 0.0044089281f }, { N_L, 0.0103678703f },
           { N_C, 0.0704050735f }, { N_D, 0.0341691934f },
           { N_R, 0.00409892527f }, { N_DR, 0.00158445851f } } },
    { 6, { { N_DL, 0.00158445851f }, { N_L, 0.00409892527f },
           { N_C, 0.0704050735f }, { N_D, 0.0341691934f },
           { N_R, 0.0103678703f }, { N_DR, 0.0044089281f } } },
    { 4, { { N_C, 0.0665472597f }, { N_D, 0.0319647305f },
           { N_R, 0.018324608f }, { N_DR, 0.00819785055f } } },
    { 4, { { N_C, 0.0573849566f }, { N_D, 0.027486911f },
           { N_R, 0.027486911f }, { N_DR, 0.0126756681f } } },
    { 4, { { N_C, 0.0474993102f }, { N_D, 0.0225957558f },
           { N_R, 0.0373725556f }, { N_DR, 0.0175668225f } } },
    /* row 7 */
    { 4, { { N_DL, 0.0241113249f }, { N_L, 0.0308280513f },
           { N_C, 0.0392670147f }, { N_D, 0.0308280513f } } },
    { 4, { { N_DL, 0.0175668225f }, { N_L, 0.0225957558f },
           { N_C, 0.0474993102f }, { N_D, 0.0373725556f } } },
    { 4, { { N_DL, 0.0115389917f }, { N_L, 0.0149834668f },
           { N_C, 0.0551116019f }, { N_D, 0.0434003845f } } },
    { 6, { { N_DL, 0.00637227902f }, { N_L, 0.00840451941f },
           { N_C, 0.0584182963f }, { N_D, 0.0461559668f },
           { N_R, 0.00327225123f }, { N_DR, 0.00241113245f } } },
    { 6, { { N_DL, 0.00241113245f }, { N_L, 0.00327225123f },
           { N_C, 0.0584182963f }, { N_D, 0.0461559668f },
           { N_R, 0.00840451941f }, { N_DR, 0.00637227902f } } },
    { 4, { { N_C, 0.0551116019f }, { N_D, 0.0434003845f },
           { N_R, 0.0149834668f }, { N_DR, 0.0115389917f } } },
    { 4, { { N_C, 0.0474993102f }, { N_D, 0.0373725556f },
           { N_R, 0.0225957558f }, { N_DR, 0.0175668225f } } },
    { 4, { { N_C, 0.0392670147f }, { N_D, 0.0308280513f },
           { N_R, 0.0308280513f }, { N_DR, 0.0241113249f } } },
};

static void dc_integer(unsigned char *dst, int stride, const int val[9])
{
    static const int row_base[3] = { N_U, N_C, N_D };
    int h[3][8];
    int i, j, k;

    /* Horizontal pass over the rows above, through and below X. */
    for (k = 0; k < 3; k++) {
        int base = val[row_base[k]];
        int dl = val[row_base[k] - 1] - base;
        int dr = val[row_base[k] + 1] - base;

        for (j = 0; j < 8; j++)
            h[k][j] = base + 4 + dc_weight(j, dl) + dc_weight(7 - j, dr);
    }
    for (j = 0; j < 8; j++) {
        int du = h[0][j] - h[1][j];
        int dd = h[2][j] - h[1][j];

        for (i = 0; i < 8; i++)
            dst[(size_t)i * stride + j] = clamp255(floor_div(
                h[1][j] + dc_weight(i, du) + dc_weight(7 - i, dd), 8));
    }
}

static void dc_float(unsigned char *dst, int stride, const int val[9])
{
    float fv[9];
    int i, k;

    for (k = 0; k < 9; k++)
        fv[k] = (float)val[k];
    for (i = 0; i < 64; i++) {
        const Bink2DcPixel *px = &dc_float_weights[i];
        float s = fv[px->term[0].n] * px->term[0].w;

        /* Every product and sum is rounded to single precision, in the
         * table's order. */
        for (k = 1; k < px->count; k++) {
            float t = fv[px->term[k].n] * px->term[k].w;

            s = s + t;
        }
        s = s + 0.49999991f;
        dst[(size_t)(i >> 3) * stride + (i & 7)] = clamp255((int)s);
    }
}

void bink2_filter_dc_block(unsigned char *dst, int stride,
                           const unsigned words[9], int threshold,
                           int integer_form)
{
    int x = wrap16((int)words[N_C]);
    int use[9];
    int dc[9];
    int val[9];
    int k;

    for (k = 0; k < 9; k++) {
        int d = wrap16(wrap16((int)words[k]) - x);

        use[k] = threshold > (d < 0 ? -d : d);
        dc[k] = (int)(words[k] & BINK2_INFO_DC_MASK);
    }
    val[N_C] = dc[N_C];
    val[N_L] = use[N_L] ? dc[N_L] : dc[N_C];
    val[N_R] = use[N_R] ? dc[N_R] : dc[N_C];
    val[N_U] = use[N_U] ? dc[N_U] : dc[N_C];
    val[N_D] = use[N_D] ? dc[N_D] : dc[N_C];
    /* Each diagonal is tested against X and falls back to its row's side
     * neighbour value. */
    val[N_UL] = use[N_UL] ? dc[N_UL] : val[N_L];
    val[N_DL] = use[N_DL] ? dc[N_DL] : val[N_L];
    val[N_UR] = use[N_UR] ? dc[N_UR] : val[N_R];
    val[N_DR] = use[N_DR] ? dc[N_DR] : val[N_R];

    if (integer_form)
        dc_integer(dst, stride, val);
    else
        dc_float(dst, stride, val);
}

void bink2_filter_dcrecon(Bink2Video *v)
{
    int threshold = 8 * (int)(v->frame_flags & 0xFF);
    int integer_form = (v->frame_flags & 0x2000) != 0;
    int h16 = (int)((v->height + 15) & ~15UL);
    int p;

    for (p = 0; p < 4; p++) {
        int s = p == 1 || p == 2;
        int cols = v->blk_stride[p];
        int last = ((int)(v->aligned_h >> 3) >> s) - 1;
        int rows = (h16 >> 3) >> s;
        int stride = v->stride[p];
        const uint16_t *info = v->blk_info[p];
        int i, j, k;

        if (info == NULL || v->cur[p] == NULL)
            continue;
        for (i = 0; i < rows; i++) {
            /* The top block row is its own row above, and the bottom
             * block row of the plane its own row below. */
            const uint16_t *row[3];

            row[0] = info + (size_t)(i > 0 ? i - 1 : 0) * cols;
            row[1] = info + (size_t)i * cols;
            row[2] = info + (size_t)(i < last ? i + 1 : last) * cols;
            for (j = 0; j < cols; j++) {
                unsigned words[9];

                if (row[1][j] < BINK2_INFO_DC_ONLY)
                    continue;
                /* Outside the first and last columns a word is 0. */
                for (k = 0; k < 9; k++) {
                    int c = j + k % 3 - 1;

                    words[k] = c < 0 || c >= cols ? 0u : row[k / 3][c];
                }
                bink2_filter_dc_block(v->cur[p] + (size_t)(8 * i) * stride +
                                      8 * j, stride, words, threshold,
                                      integer_form);
            }
        }
    }
}
