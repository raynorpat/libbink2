#include "bink2_filter.h"

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
