#ifndef LIBBINK2_ENC_DCT_H
#define LIBBINK2_ENC_DCT_H

#include <stdint.h>

/** Forward transform pair to bink2_idct_new + bink2_put_block. */
void bink2_fdct_block(const uint8_t *pixels, int stride, float out[64]);

/**
 * Quantise the float transform of one block. `q` is the Bink 2 quantiser
 * (0..36); the matrix index is q & 3 and the scale doubles every four steps.
 * Returns the number of nonzero levels.
 */
int bink2_quantise(const float in[64], const uint16_t qmat[4][64], int q,
                   int deadzone, int32_t levels[64]);

/** Inverse of bink2_quantise, matching the decoder's inline arithmetic. */
void bink2_dequantise(const int32_t levels[64], const uint16_t qmat[4][64],
                      int q, int32_t out[64]);

#endif
