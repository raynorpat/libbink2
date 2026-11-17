#include "bink2_motion.h"
#include "test_common.h"

static int test_zero(void)
{
    int16_t mx[4] = { 0 }, my[4] = { 0 };
    unsigned char count[1] = { 0 };
    signed char x[16], y[16];
    int i;

    bink2_motion_expand(mx, my, count, 1, 1, 4, 4, x, y);
    for (i = 0; i < 16; i++)
        CHECK(x[i] == 0 && y[i] == 0);
    return 0;
}

static int test_one_vector(void)
{
    int16_t mx[4] = { 16, 0, 0, 0 };
    int16_t my[4] = { -8, 0, 0, 0 };
    unsigned char count[1] = { 1 };
    signed char x[16], y[16];
    int i;

    bink2_motion_expand(mx, my, count, 1, 1, 4, 4, x, y);
    for (i = 0; i < 16; i++) {
        CHECK(x[i] == 8);
        CHECK(y[i] == -4);
    }
    return 0;
}

static int test_truncation(void)
{
    int16_t mx[4] = { 3, 0, 0, 0 };
    int16_t my[4] = { -3, 0, 0, 0 };
    unsigned char count[1] = { 1 };
    signed char x[16], y[16];
    int i;

    bink2_motion_expand(mx, my, count, 1, 1, 4, 4, x, y);
    for (i = 0; i < 16; i++) {
        CHECK(x[i] == 1);
        CHECK(y[i] == -1);
    }
    return 0;
}

static int test_four_vectors(void)
{
    int16_t mx[4] = { 2, 4, 6, 8 };
    int16_t my[4] = { 0, 0, 0, 0 };
    unsigned char count[1] = { 4 };
    signed char x[16], y[16];
    int bx, by;

    bink2_motion_expand(mx, my, count, 1, 1, 4, 4, x, y);
    for (by = 0; by < 4; by++) {
        for (bx = 0; bx < 4; bx++) {
            int k = (bx / 2) + 2 * (by / 2);
            int expected = (k + 1);

            CHECK(x[by * 4 + bx] == expected);
            CHECK(y[by * 4 + bx] == 0);
        }
    }
    return 0;
}

static int test_edge_clamp(void)
{
    int16_t mx[8] = { 0, 0, 0, 0, 10, 0, 0, 0 };
    int16_t my[8] = { 0, 0, 0, 0, -20, 0, 0, 0 };
    unsigned char count[2] = { 0, 1 };
    signed char x[5], y[5];

    bink2_motion_expand(mx, my, count, 2, 1, 5, 1, x, y);
    CHECK(x[0] == 0);
    CHECK(x[1] == 0);
    CHECK(x[2] == 0);
    CHECK(x[3] == 0);
    CHECK(x[4] == 5);
    CHECK(y[4] == -10);
    return 0;
}

int main(void)
{
    CHECK(test_zero() == 0);
    CHECK(test_one_vector() == 0);
    CHECK(test_truncation() == 0);
    CHECK(test_four_vectors() == 0);
    CHECK(test_edge_clamp() == 0);
    return 0;
}
