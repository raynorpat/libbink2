#include "bink2_bits.h"
#include "test_common.h"

static int test_bitreader(void)
{
    static const unsigned char data[2] = {0x0A, 0x0C};
    Bink2Bits bits;

    bink2_bits_init(&bits, data, sizeof(data));
    CHECK(bink2_bits_left(&bits) == 16);
    CHECK(bink2_bits_read(&bits, 3) == 2);
    CHECK(bink2_bits_left(&bits) == 13);
    CHECK(bink2_bits_read(&bits, 5) == 1);
    CHECK(bink2_bits_left(&bits) == 8);
    CHECK(bink2_bits_read(&bits, 9) == -1);
    CHECK(bink2_bits_left(&bits) == 8);
    CHECK(bink2_bits_read1(&bits) == 0);
    CHECK(bink2_bits_read(&bits, 7) == 6);
    CHECK(bink2_bits_left(&bits) == 0);
    CHECK(bink2_bits_read1(&bits) == -1);

    bink2_bits_init(&bits, data, sizeof(data));
    CHECK(bink2_bits_read(&bits, 3) == 2);
    bink2_bits_skip(&bits, 5);
    CHECK(bink2_bits_left(&bits) == 8);
    bink2_bits_align32(&bits);
    CHECK(bink2_bits_left(&bits) == 0);

    bink2_bits_init(&bits, data, sizeof(data));
    bink2_bits_align32(&bits);
    CHECK(bink2_bits_left(&bits) == 16);
    return 0;
}

/* The video readers mark a failed reader by setting bitpos to SIZE_MAX.
 * Skipping or aligning must not wrap that back to a readable position. */
static int test_failed_stays_failed(void)
{
    static const unsigned char data[8] = {0};
    Bink2Bits bits;

    bink2_bits_init(&bits, data, sizeof(data));
    bits.bitpos = (size_t)-1;
    CHECK(bink2_bits_failed(&bits));
    bink2_bits_skip(&bits, 1);
    CHECK(bink2_bits_failed(&bits));
    CHECK(bink2_bits_left(&bits) == 0);
    bink2_bits_align32(&bits);
    CHECK(bink2_bits_failed(&bits));
    CHECK(bink2_bits_left(&bits) == 0);
    CHECK(bink2_bits_read1(&bits) == -1);

    /* A skip past the end still clamps to the end, as before. */
    bink2_bits_init(&bits, data, sizeof(data));
    bink2_bits_skip(&bits, 1000);
    CHECK(!bink2_bits_failed(&bits));
    CHECK(bink2_bits_left(&bits) == 0);
    return 0;
}

static int test_writer_roundtrip(void)
{
    Bink2BitWriter bw;
    Bink2Bits bits;

    bink2_bw_init(&bw);
    CHECK(bink2_bw_put(&bw, 0x2A, 6) == 0);
    CHECK(bink2_bw_put(&bw, 0x15, 5) == 0);
    CHECK(bink2_bw_align32(&bw) == 0);
    CHECK(bink2_bw_bytes(&bw) == 4);
    bink2_bits_init(&bits, bink2_bw_data(&bw), bink2_bw_bytes(&bw));
    CHECK(bink2_bits_read(&bits, 6) == 0x2A);
    CHECK(bink2_bits_read(&bits, 5) == 0x15);
    CHECK(bink2_bits_left(&bits) == 21);
    bink2_bw_free(&bw);
    return 0;
}

static int test_writer_wrap(void)
{
    Bink2BitWriter bw;
    Bink2Bits bits;

    bink2_bw_init(&bw);
    CHECK(bink2_bw_put(&bw, 0xFFFFFFFFu, 32) == 0);
    CHECK(bink2_bw_bitpos(&bw) == 32);
    bink2_bits_init(&bits, bink2_bw_data(&bw), bink2_bw_bytes(&bw));
    CHECK(bink2_bits_read(&bits, 16) == 0xFFFF);
    CHECK(bink2_bits_read(&bits, 16) == 0xFFFF);
    bink2_bw_reset(&bw);
    CHECK(bink2_bw_bitpos(&bw) == 0);
    bink2_bw_free(&bw);
    return 0;
}

static int test_unary(void)
{
    /* LSB-first: 0,0,1,0,1,0,0,0 */
    static const unsigned char data[1] = {0x14};
    Bink2Bits bits;

    bink2_bits_init(&bits, data, sizeof(data));
    CHECK(bink2_bits_read_unary(&bits, 1, 8) == 2);
    CHECK(bink2_bits_read_unary(&bits, 1, 8) == 1);
    CHECK(bink2_bits_read_unary(&bits, 1, 8) == -1);
    return 0;
}

int main(void)
{
    CHECK(test_bitreader() == 0);
    CHECK(test_failed_stays_failed() == 0);
    CHECK(test_writer_roundtrip() == 0);
    CHECK(test_writer_wrap() == 0);
    CHECK(test_unary() == 0);
    return 0;
}
