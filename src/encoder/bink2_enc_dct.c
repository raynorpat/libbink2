#include "bink2_enc_dct.h"

#include <math.h>
#include <stdlib.h>

/*
 * The decoder's 1-D inverse transform with shift 0 is the integer matrix B
 * (columns are the responses to unit coefficients, with entries in
 * sixteenths). The 2-D output is (B X B^T) >> 6 and bink2_put_block()
 * transposes it, so with G = B/8:
 *
 *     P = G * X^T * G^T        =>   X = H * P^T * H^T,  H = 8 * B^-1.
 *
 * H below is that inverse, computed once from the exact butterfly constants
 * so the forward keeps the integer IDCT's slightly non-cosine basis.
 */
static const double fdct_matrix[8][8] = {
    { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 },
    { 1.6127320954907163, 1.3580901856763923, 0.93368700265251969,
      0.3395225464190984, -0.33952254641909807, -0.93368700265252014,
      -1.3580901856763927, -1.6127320954907156 },
    { 1.3793103448275865, 0.55172413793103448, -0.55172413793103492,
      -1.3793103448275854, -1.3793103448275867, -0.55172413793103425,
      0.55172413793103425, 1.3793103448275863 },
    { 0.97612732095490728, -0.21220159151193643, -1.1458885941644563,
      -0.63660477453580944, 0.63660477453580921, 1.1458885941644563,
      0.21220159151193652, -0.97612732095490728 },
    { 1.0, -1.0, -1.0, 1.0, 1.0, -1.0, -1.0, 1.0 },
    { 0.63660477453580877, -1.1458885941644561, 0.2122015915119364,
      0.9761273209549074, -0.97612732095490717, -0.21220159151193629,
      1.1458885941644561, -0.63660477453580888 },
    { 0.55172413793103425, -1.3793103448275859, 1.3793103448275861,
      -0.55172413793103381, -0.55172413793103448, 1.3793103448275861,
      -1.3793103448275861, 0.55172413793103414 },
    { 0.33952254641909807, -0.9336870026525198, 1.3580901856763927,
      -1.6127320954907163, 1.6127320954907163, -1.3580901856763923,
      0.93368700265252014, -0.33952254641909813 },
};

void bink2_fdct_block(const uint8_t *pixels, int stride, float out[64])
{
    double transposed[64];
    double rows[64];
    int row, col, k;

    /* Q = P^T, because bink2_put_block() transposes the inverse output. */
    for (row = 0; row < 8; row++)
        for (col = 0; col < 8; col++)
            transposed[row * 8 + col] =
                (double)pixels[col * stride + row];

    /* R = F * Q. */
    for (k = 0; k < 8; k++) {
        for (col = 0; col < 8; col++) {
            double sum = 0.0;
            int n;

            for (n = 0; n < 8; n++)
                sum += fdct_matrix[k][n] * transposed[n * 8 + col];
            rows[k * 8 + col] = sum;
        }
    }

    /* X = R * F^T (H already carries the 2-D scale). */
    for (k = 0; k < 8; k++) {
        int l;

        for (l = 0; l < 8; l++) {
            double sum = 0.0;
            int n;

            for (n = 0; n < 8; n++)
                sum += rows[k * 8 + n] * fdct_matrix[l][n];
            out[k * 8 + l] = (float)sum;
        }
    }
}

static int clamp_q(int q)
{
    if (q < 0)
        return 0;
    if (q > 36)
        return 36;
    return q;
}

int bink2_quantise(const float in[64], const uint16_t qmat[4][64], int q,
                   int deadzone, int32_t levels[64])
{
    int count = 0;
    int m;
    int shift;
    int pos;

    q = clamp_q(q);
    m = q & 3;
    shift = q >> 2;
    for (pos = 0; pos < 64; pos++) {
        int64_t scale = (int64_t)qmat[m][pos] << shift;
        long level = 0;

        if (scale > 0)
            level = lround((double)in[pos] * 128.0 / (double)scale);
        if (level < 0)
            level = -level;
        if (level < deadzone)
            level = 0;
        else if (in[pos] < 0.0f)
            level = -level;
        levels[pos] = (int32_t)level;
        if (level != 0)
            count++;
    }
    return count;
}

void bink2_dequantise(const int32_t levels[64], const uint16_t qmat[4][64],
                      int q, int32_t out[64])
{
    int m;
    int shift;
    int pos;

    q = clamp_q(q);
    m = q & 3;
    shift = q >> 2;
    for (pos = 0; pos < 64; pos++) {
        int64_t scale = (int64_t)qmat[m][pos] << shift;
        int64_t value = (int64_t)levels[pos] * scale;

        out[pos] = (int32_t)((value + 0x40) >> 7);
    }
}
