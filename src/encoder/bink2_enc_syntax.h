#ifndef LIBBINK2_ENC_SYNTAX_H
#define LIBBINK2_ENC_SYNTAX_H

#include "bink2_bits.h"

#include <stdint.h>

/*
 * Writers for the KB2g+ bitstream. Each is the exact inverse of the
 * bink2_new_read_* function in bink2_video_new.h; the syntax test writes and
 * reads every case back.
 */

void bink2_write_btype(Bink2BitWriter *bw, uint8_t lru[4], int type);
int bink2_btype_bits(const uint8_t lru[4], int type);

void bink2_write_quant(Bink2BitWriter *bw, int prev_q, int q);

void bink2_write_cbp_luma(Bink2BitWriter *bw, uint32_t prev_cbp,
                          uint32_t edge, uint32_t cbp);
void bink2_write_cbp_chroma(Bink2BitWriter *bw, uint32_t prev_cbp,
                            uint32_t cbp);

void bink2_write_dcs(Bink2BitWriter *bw, const int32_t dcs[16], int count,
                     int q);
void bink2_write_acs(Bink2BitWriter *bw, const int32_t levels[64],
                     const uint16_t qmat[4][64], int q, uint32_t cbp);

void bink2_write_mv1(Bink2BitWriter *bw, int x, int y);
void bink2_write_mv4(Bink2BitWriter *bw, const int mv[4][2]);

#endif
