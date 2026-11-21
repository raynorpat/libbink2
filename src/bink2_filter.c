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
