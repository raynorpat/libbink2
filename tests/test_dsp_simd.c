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

/* Every block position on a grid that reaches all four borders, and every
 * motion vector in -70..70, from an exact-size source so ASan sees any
 * read outside the 32-aligned (luma) or 16-aligned (chroma) plane. */
static int mc_case(int chroma)
{
    const int w = chroma ? 40 : 80, h = chroma ? 36 : 72;
    const int fw = chroma ? (w + 15) & ~15 : (w + 31) & ~31;
    const int fh = chroma ? (h + 15) & ~15 : (h + 31) & ~31;
    const int bs = chroma ? 8 : 16;
    const int step = chroma ? 8 : 16;
    unsigned char *src = (unsigned char *)malloc((size_t)fw * fh);
    unsigned char da[24 * 24], db[24 * 24];
    int x, y, mvx, mvy, i;

    CHECK(src != NULL);
    for (i = 0; i < fw * fh; i++)
        src[i] = (unsigned char)next_rand();
    for (y = 0; y + bs <= fh; y += step) {
        for (x = 0; x + bs <= fw; x += step) {
            for (mvy = -70; mvy <= 70; mvy++) {
                for (mvx = -70; mvx <= 70; mvx++) {
                    memset(da, 0xA5, sizeof(da));
                    memset(db, 0xA5, sizeof(db));
                    if (chroma) {
                        bink2_mc_chroma(da + 25, 24, src, fw, x, y, w, h,
                                        mvx, mvy);
                        bink2_mc_chroma_c(db + 25, 24, src, fw, x, y, w, h,
                                          mvx, mvy);
                    } else {
                        bink2_mc_luma(da + 25, 24, src, fw, x, y, w, h,
                                      mvx, mvy);
                        bink2_mc_luma_c(db + 25, 24, src, fw, x, y, w, h,
                                        mvx, mvy);
                    }
                    if (memcmp(da, db, sizeof(da)) != 0) {
                        fprintf(stderr, "%s x=%d y=%d mv=%d,%d\n",
                                chroma ? "chroma" : "luma", x, y, mvx, mvy);
                        free(src);
                        return 1;
                    }
                }
            }
        }
    }
    free(src);
    return 0;
}

static int test_mc_luma(void)
{
    return mc_case(0);
}

static int test_mc_chroma(void)
{
    return mc_case(1);
}

/* Random values, every k + 0.5 tie in -300..300 (round half to even), and
 * signed zeros, in both the put and the add form. */
static int test_block_f(void)
{
    enum { STRIDE = 40 };
    float ties[602 + 2];
    int n, i, nties = 0;

    for (i = -300; i <= 300; i++)
        ties[nties++] = (float)i + 0.5f;
    ties[nties++] = 0.0f;
    ties[nties++] = -0.0f;
    for (n = 0; n < 20000; n++) {
        unsigned char da[STRIDE * 8], db[STRIDE * 8];
        float block[64];

        for (i = 0; i < 64; i++) {
            if (n & 1)
                block[i] = ties[next_rand() % (uint32_t)nties];
            else
                block[i] = (float)rand_range(-600000, 600000) / 1000.0f;
        }
        for (i = 0; i < STRIDE * 8; i++)
            da[i] = db[i] = (unsigned char)next_rand();
        bink2_put_block_f(da + 3, STRIDE, block);
        bink2_put_block_f_c(db + 3, STRIDE, block);
        CHECK(memcmp(da, db, sizeof(da)) == 0);
        bink2_add_block_f(da + 5, STRIDE, block);
        bink2_add_block_f_c(db + 5, STRIDE, block);
        CHECK(memcmp(da, db, sizeof(da)) == 0);
    }
    return 0;
}

/* Every width from 1 to 70 and 333, both ranges, from exact-size heap
 * rows so ASan sees any read past a row. */
static int yuv_case(int width, int studio)
{
    int cw = (width + 1) / 2;
    unsigned char *y = (unsigned char *)malloc((size_t)width);
    unsigned char *u = (unsigned char *)malloc((size_t)cw);
    unsigned char *v = (unsigned char *)malloc((size_t)cw);
    unsigned char *ra = (unsigned char *)malloc((size_t)width * 3);
    unsigned char *rb = (unsigned char *)malloc((size_t)width * 3);
    int i, ok;

    CHECK(y != NULL && u != NULL && v != NULL && ra != NULL && rb != NULL);
    for (i = 0; i < width; i++)
        y[i] = (unsigned char)next_rand();
    for (i = 0; i < cw; i++) {
        u[i] = (unsigned char)next_rand();
        v[i] = (unsigned char)next_rand();
    }
    /* Extremes of every channel. */
    if (width >= 4) {
        y[0] = 0;
        y[1] = 255;
        u[0] = 0;
        v[1] = 255;
    }
    bink2_yuv_row(ra, y, u, v, width, studio);
    bink2_yuv_row_c(rb, y, u, v, width, studio);
    ok = memcmp(ra, rb, (size_t)width * 3) == 0;
    if (!ok)
        fprintf(stderr, "yuv width=%d studio=%d\n", width, studio);
    free(y);
    free(u);
    free(v);
    free(ra);
    free(rb);
    return ok ? 0 : 1;
}

static int test_yuv_row(void)
{
    int width, studio, round;

    for (round = 0; round < 20; round++)
        for (studio = 0; studio < 2; studio++) {
            for (width = 1; width <= 70; width++)
                CHECK(yuv_case(width, studio) == 0);
            CHECK(yuv_case(333, studio) == 0);
        }
    return 0;
}

int main(void)
{
    CHECK(test_idct_new() == 0);
    CHECK(test_put_add_block() == 0);
    CHECK(test_mc_luma() == 0);
    CHECK(test_mc_chroma() == 0);
    CHECK(test_block_f() == 0);
    CHECK(test_yuv_row() == 0);
    return 0;
}
