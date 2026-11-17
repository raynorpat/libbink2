#ifndef LIBBINK2_PLAYER_MOTION_H
#define LIBBINK2_PLAYER_MOTION_H

#include "bink2.h"

/*
 * The renderers use the same per-8x8 luma motion field as the libbink2
 * players: signed pixels, one entry per visible 8x8 block. libbink2 returns
 * one or four half-pel vectors per 32x32 macroblock, so this adapter expands
 * and halves them (truncating toward zero).
 */
int bink2_player_motion_get(const bink2 object, unsigned long *blocks_w,
                            unsigned long *blocks_h,
                            const signed char **mv_x,
                            const signed char **mv_y);

/* Pure expansion helper, exposed for the unit test. */
void bink2_motion_expand(const int16_t *mv_x, const int16_t *mv_y,
                         const unsigned char *count, unsigned long mb_w,
                         unsigned long mb_h, unsigned long blocks_w,
                         unsigned long blocks_h, signed char *out_x,
                         signed char *out_y);

#endif
