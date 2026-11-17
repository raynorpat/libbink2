#include "tools/player/bink2_motion.h"

#include <stdlib.h>

static signed char *motion_x;
static signed char *motion_y;
static size_t motion_cap;

static signed char motion_pixels(int half_pel)
{
    int pixels = half_pel / 2;

    if (pixels < -128)
        pixels = -128;
    if (pixels > 127)
        pixels = 127;
    return (signed char)pixels;
}

void bink2_motion_expand(const int16_t *mv_x, const int16_t *mv_y,
                         const unsigned char *count, unsigned long mb_w,
                         unsigned long mb_h, unsigned long blocks_w,
                         unsigned long blocks_h, signed char *out_x,
                         signed char *out_y)
{
    unsigned long by, bx;

    for (by = 0; by < blocks_h; by++) {
        for (bx = 0; bx < blocks_w; bx++) {
            unsigned long mbx = bx / 4;
            unsigned long mby = by / 4;
            unsigned long mb;
            unsigned long index = by * blocks_w + bx;
            int vx = 0, vy = 0;

            if (mbx >= mb_w)
                mbx = mb_w - 1;
            if (mby >= mb_h)
                mby = mb_h - 1;
            mb = mby * mb_w + mbx;
            if (count[mb] == 1) {
                vx = mv_x[mb * 4];
                vy = mv_y[mb * 4];
            } else if (count[mb] == 4) {
                unsigned long lx = bx % 4;
                unsigned long ly = by % 4;
                unsigned long k = (lx >> 1) + 2 * (ly >> 1);

                vx = mv_x[mb * 4 + k];
                vy = mv_y[mb * 4 + k];
            }
            out_x[index] = motion_pixels(vx);
            out_y[index] = motion_pixels(vy);
        }
    }
}

int bink2_player_motion_get(const bink2 object, unsigned long *blocks_w,
                            unsigned long *blocks_h,
                            const signed char **mv_x,
                            const signed char **mv_y)
{
    unsigned long width = 0, height = 0;
    unsigned long mb_w = 0, mb_h = 0;
    unsigned long bw, bh;
    const int16_t *mx = NULL;
    const int16_t *my = NULL;
    const unsigned char *count = NULL;
    size_t need;

    if (blocks_w != NULL)
        *blocks_w = 0;
    if (blocks_h != NULL)
        *blocks_h = 0;
    if (mv_x != NULL)
        *mv_x = NULL;
    if (mv_y != NULL)
        *mv_y = NULL;
    if (object == NULL ||
        bink2_info_video(object, &width, &height, NULL) != 0 ||
        width == 0 || height == 0)
        return 0;
    if (!bink2_get_motion(object, &mb_w, &mb_h, &mx, &my, &count) ||
        mb_w == 0 || mb_h == 0)
        return 0;
    bw = (width + 7) / 8;
    bh = (height + 7) / 8;
    need = (size_t)bw * (size_t)bh;
    if (need > motion_cap) {
        signed char *grown_x = (signed char *)realloc(motion_x, need);
        signed char *grown_y;

        if (grown_x == NULL)
            return 0;
        motion_x = grown_x;
        grown_y = (signed char *)realloc(motion_y, need);
        if (grown_y == NULL)
            return 0;
        motion_y = grown_y;
        motion_cap = need;
    }
    bink2_motion_expand(mx, my, count, mb_w, mb_h, bw, bh, motion_x,
                        motion_y);
    if (blocks_w != NULL)
        *blocks_w = bw;
    if (blocks_h != NULL)
        *blocks_h = bh;
    if (mv_x != NULL)
        *mv_x = motion_x;
    if (mv_y != NULL)
        *mv_y = motion_y;
    return 1;
}
