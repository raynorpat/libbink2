#include "bink2_enc_dct.h"
#include "bink2_dsp.h"
#include "bink2_tables.h"
#include "test_common.h"

#include <math.h>

static int test_constant_block(void)
{
    uint8_t px[64];
    float coeffs[64];
    int32_t levels[64] = { 0 };
    int32_t out[64];
    uint8_t recon[64];
    int i;

    for (i = 0; i < 64; i++)
        px[i] = 128;
    bink2_fdct_block(px, 8, coeffs);
    for (i = 1; i < 64; i++)
        CHECK(fabsf(coeffs[i]) < 1e-3f);
    CHECK(bink2_quantise(coeffs, bink2g_luma_intra_qmat, 0, 0, levels) >= 0);
    bink2_dequantise(levels, bink2g_luma_intra_qmat, 0, out);
    bink2_idct_new(out);
    bink2_put_block(recon, 8, out);
    for (i = 0; i < 64; i++)
        CHECK(abs((int)recon[i] - (int)px[i]) <= 1);
    return 0;
}

static int test_random_block(void)
{
    uint8_t px[64];
    uint8_t recon[64];
    float coeffs[64];
    int32_t levels[64];
    int32_t out[64];
    unsigned seed = 12345;
    int i;

    for (i = 0; i < 64; i++) {
        seed = seed * 1103515245u + 12345u;
        px[i] = (uint8_t)((seed >> 16) & 0xFF);
    }
    bink2_fdct_block(px, 8, coeffs);
    CHECK(bink2_quantise(coeffs, bink2g_luma_intra_qmat, 0, 0, levels) > 0);
    bink2_dequantise(levels, bink2g_luma_intra_qmat, 0, out);
    bink2_idct_new(out);
    bink2_put_block(recon, 8, out);
    for (i = 0; i < 64; i++)
        CHECK(abs((int)recon[i] - (int)px[i]) <= 4);
    return 0;
}

static int test_quantiser_monotonic(void)
{
    uint8_t px[64];
    float coeffs[64];
    int32_t levels0[64];
    int32_t levels8[64];
    unsigned seed = 999;
    int i;
    int n0, n8;

    for (i = 0; i < 64; i++) {
        seed = seed * 1103515245u + 12345u;
        px[i] = (uint8_t)((seed >> 16) & 0xFF);
    }
    bink2_fdct_block(px, 8, coeffs);
    n0 = bink2_quantise(coeffs, bink2g_luma_intra_qmat, 0, 0, levels0);
    n8 = bink2_quantise(coeffs, bink2g_luma_intra_qmat, 8, 0, levels8);
    CHECK(n0 >= 0 && n8 >= 0);
    CHECK(n8 <= n0);

    /* A deadzone above every level removes the whole block. */
    CHECK(bink2_quantise(coeffs, bink2g_luma_intra_qmat, 0, 1000000,
                         levels0) == 0);
    return 0;
}

int main(void)
{
    CHECK(test_constant_block() == 0);
    CHECK(test_random_block() == 0);
    CHECK(test_quantiser_monotonic() == 0);
    return 0;
}
