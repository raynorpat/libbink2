#ifndef LIBBINK2_TABLES_H
#define LIBBINK2_TABLES_H

#include <stdint.h>

/* Tables shared by the decoder and the encoder. Values are byte-for-byte
 * identical to the pinned FFmpeg Bink 2 reference. */

extern const uint8_t bink2g_scan[64];
extern const uint16_t bink2g_ac_skip_codes[2][14];
extern const uint8_t bink2g_ac_skip_bits[2][14];
extern const uint16_t bink2g_mv_codes[];
extern const uint8_t bink2g_mv_bits[];
extern const int32_t bink2g_dc_pat[];
extern const uint16_t bink2g_luma_intra_qmat[4][64];
extern const uint16_t bink2g_chroma_intra_qmat[4][64];
extern const uint16_t bink2g_inter_qmat[4][64];

extern const uint16_t bink2f_ac_val_codes[2][13];
extern const uint8_t bink2f_ac_val_bits[2][13];
extern const uint16_t bink2f_ac_skip_codes[2][14];
extern const uint8_t bink2f_ac_skip_bits[2][14];

#endif
