#ifndef LIBBINK2_VIDEO_NEW_H
#define LIBBINK2_VIDEO_NEW_H

#include "bink2_bits.h"
#include "bink2_video.h"

#include <stdint.h>

/*
 * Readers for the KB2g+ video syntax, shared with the encoder so both sides
 * run the same tables and bit rules. On overread a reader marks the
 * Bink2Bits with bink2_bits_failed() and returns a zero/0xFF value; callers
 * must check bink2_bits_failed() (or the slice decoder will fail the frame).
 */

uint8_t bink2_new_read_btype(Bink2Bits *bits, uint8_t lru[4]);
int bink2_new_read_quant(Bink2Bits *bits, int prev_q);
uint32_t bink2_new_read_cbp_luma(Bink2Bits *bits, uint32_t prev_cbp,
                                 uint32_t edge);
uint32_t bink2_new_read_cbp_chroma(Bink2Bits *bits, uint32_t prev_cbp);
/**
 * Read `count` DC values, scaled by the quantiser pattern for q. The encoder
 * writes values in the same scaled domain; at q = 0 the scaling is identity.
 */
void bink2_new_read_dcs(Bink2Bits *bits, int32_t dcs[16], int count, int q);
/**
 * Read one coded 8x8 AC block. The result is the signed quantised magnitude
 * (the decoder's `val`) in natural coefficient order; multiply by
 * qmat[q & 3][pos] << (q >> 2) and shift to dequantise. `cbp` bit 0 says
 * whether the block is coded and any high bit selects the second skip table.
 */
void bink2_new_read_acs(Bink2Bits *bits, int32_t levels[64],
                        const uint16_t qmat[4][64], int q, uint32_t cbp);
int bink2_new_read_mv(Bink2Bits *bits, int mvs[4][2], int *nb);

/*
 * Predictors for the current macroblock (s->mb_pos), shared with the
 * encoder so both sides predict identically. `flags` carries 0x80 on a
 * slice's first row and 0x20 in its first column.
 */
/** Predicted intra (inter = 0) or inter (inter = 1) quantiser. */
int bink2_new_predict_q(const Bink2Slice *s, int flags, int inter);
/**
 * Adds the predicted motion vector to raw[0] (nb = 1, stored to all four
 * slots) or to each of raw[0..3] (nb = 4), and stores the result in
 * s->mv_cur for the macroblock.
 */
void bink2_new_predict_mv(Bink2Slice *s, int flags, const int raw[4][2],
                          int nb);

#endif
