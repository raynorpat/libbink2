/* Each dispatching DSP routine (SIMD where the build has it) must match its
 * scalar reference bit for bit. Without SIMD the comparison is trivial and
 * still passes. */
#include "bink2_dsp.h"
#include "test_common.h"

static uint32_t seed = 1;

static uint32_t next_rand(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

static int32_t rand_range(int32_t lo, int32_t hi)
{
    return lo + (int32_t)(next_rand() % (uint32_t)(hi - lo + 1));
}

static void fill_coeffs(int32_t block[64], int kind)
{
    int i;

    for (i = 0; i < 64; i++) {
        switch (kind) {
        case 0:
            block[i] = rand_range(-32768, 32767);
            break;
        case 1:
            block[i] = rand_range(-2048, 2047);
            break;
        case 2:
            block[i] = 0;
            break;
        default:
            block[i] = (next_rand() & 1) ? 32767 : -32768;
            break;
        }
    }
    if (kind == 2) {
        block[0] = rand_range(-4096, 4095);
        block[1 + next_rand() % 63] = rand_range(-1024, 1023);
    }
}

static int test_idct_new(void)
{
    int n;

    for (n = 0; n < 20000; n++) {
        int32_t a[64], b[64];

        fill_coeffs(a, n % 4);
        /* Out-of-int16 inputs are truncated to int16 first. */
        if (n % 97 == 0)
            a[next_rand() % 64] = (int32_t)(next_rand() & 0x7FFFFFFF);
        memcpy(b, a, sizeof(a));
        bink2_idct_new(a);
        bink2_idct_new_c(b);
        CHECK(memcmp(a, b, sizeof(a)) == 0);
    }
    return 0;
}

static int test_put_add_block(void)
{
    enum { STRIDE = 40 };
    int n;

    for (n = 0; n < 20000; n++) {
        unsigned char da[STRIDE * 8], db[STRIDE * 8];
        int32_t block[64];
        int i;

        for (i = 0; i < 64; i++)
            block[i] = n % 10 == 0 ? rand_range(-100000, 100000)
                                   : rand_range(-600, 600);
        for (i = 0; i < STRIDE * 8; i++)
            da[i] = db[i] = (unsigned char)next_rand();
        bink2_put_block(da + 3, STRIDE, block);
        bink2_put_block_c(db + 3, STRIDE, block);
        CHECK(memcmp(da, db, sizeof(da)) == 0);
        bink2_add_block(da + 5, STRIDE, block);
        bink2_add_block_c(db + 5, STRIDE, block);
        CHECK(memcmp(da, db, sizeof(da)) == 0);
    }
    return 0;
}

int main(void)
{
    CHECK(test_idct_new() == 0);
    CHECK(test_put_add_block() == 0);
    return 0;
}
