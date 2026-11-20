#include "bink2_bits.h"
#include "bink2_tables.h"
#include <stdint.h>
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

    /* A skip past the end clamps to the end without failing. */
    bink2_bits_init(&bits, data, sizeof(data));
    bink2_bits_skip(&bits, 1000);
    CHECK(!bink2_bits_failed(&bits));
    CHECK(bink2_bits_left(&bits) == 0);
    return 0;
}

/* A failed write leaves the position where it was, so later writes would
 * build a stream with a hole in it. The writer stays failed until reset. */
static int test_writer_failure_sticks(void)
{
    Bink2BitWriter bw;

    bink2_bw_init(&bw);
    CHECK(!bink2_bw_failed(&bw));
    CHECK(bink2_bw_put(&bw, 1, 3) == 0);
    CHECK(bink2_bw_put(&bw, 0, 33) == -1);
    CHECK(bink2_bw_failed(&bw));
    CHECK(bink2_bw_put(&bw, 1, 1) == -1);
    CHECK(bink2_bw_align32(&bw) == -1);
    CHECK(bink2_bw_bitpos(&bw) == 3);
    bink2_bw_reset(&bw);
    CHECK(!bink2_bw_failed(&bw));
    CHECK(bink2_bw_put(&bw, 1, 1) == 0);
    bink2_bw_free(&bw);
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

/* The bit-at-a-time definition of an LSB-first read. */
static int ref_read(const unsigned char *data, size_t size, size_t *bitpos,
                    int count)
{
    uint32_t value = 0;
    int i;

    if (*bitpos > size * 8 || (size_t)count > size * 8 - *bitpos)
        return -1;
    for (i = 0; i < count; i++) {
        size_t pos = *bitpos + (size_t)i;

        if (data[pos >> 3] & (1u << (pos & 7)))
            value |= (uint32_t)1u << i;
    }
    *bitpos += (size_t)count;
    return (int)value;
}

/* Random read sequences over buffers of every small size, including reads
 * that end exactly at, or would run past, the last byte. */
static int test_reader_matches_reference(void)
{
    unsigned char data[40];
    uint32_t seed = 7;
    size_t size;
    int round, op;

    for (size = 0; size <= sizeof(data); size++) {
        for (round = 0; round < 200; round++) {
            Bink2Bits bits;
            unsigned char *heap;
            size_t refpos = 0;
            size_t i;

            for (i = 0; i < size; i++) {
                seed = seed * 1664525u + 1013904223u;
                data[i] = (unsigned char)(seed >> 24);
            }
            /* An exact-size heap copy lets ASan see any read past it. */
            heap = (unsigned char *)malloc(size > 0 ? size : 1);
            CHECK(heap != NULL);
            memcpy(heap, data, size);
            bink2_bits_init(&bits, heap, size);
            for (op = 0; op < 64; op++) {
                int count, got, want;

                seed = seed * 1664525u + 1013904223u;
                count = (int)((seed >> 16) % 34);
                if (count == 33) {
                    got = bink2_bits_read1(&bits);
                    want = ref_read(data, size, &refpos, 1);
                } else {
                    got = bink2_bits_read(&bits, count);
                    want = ref_read(data, size, &refpos, count);
                }
                if (got != want || bits.bitpos != refpos) {
                    free(heap);
                    CHECK(got == want);
                    CHECK(bits.bitpos == refpos);
                }
            }
            free(heap);
        }
    }
    return 0;
}

/* The bit-at-a-time VLC search the slice decoders used. */
static int ref_vlc(Bink2Bits *bits, const uint16_t *codes,
                   const uint8_t *lengths, int count)
{
    unsigned code = 0;
    int len, i;

    for (len = 1; len <= 12; len++) {
        int bit = bink2_bits_read1(bits);

        if (bit < 0)
            return -1;
        code |= (unsigned)bit << (len - 1);
        for (i = 0; i < count; i++) {
            if (lengths[i] == len && codes[i] == code)
                return i;
        }
    }
    return -1;
}

static int ref_unary(Bink2Bits *bits, int stop, int max)
{
    int i;

    if (max < 0)
        return -1;
    for (i = 0; i < max; i++) {
        int bit = bink2_bits_read1(bits);

        if (bit < 0)
            return -1;
        if (bit == stop)
            return i;
    }
    return max;
}

typedef struct {
    const uint16_t *codes;
    const uint8_t *lengths;
    int count;
} Codebook;

/* Real codebooks, plus one with an ambiguous prefix, a duplicate, a code
 * longer than 12 bits and a zero length, which the search must resolve
 * the same way the reference does. */
static const uint16_t odd_codes[7] = { 0x1, 0x1, 0x3, 0x0, 0x2, 0x6, 0x6 };
static const uint8_t odd_lengths[7] = { 2, 1, 2, 13, 0, 3, 3 };

static int vlc_case(const unsigned char *data, size_t size, size_t start,
                    const Codebook *book)
{
    Bink2Bits a, b;
    int got, want;

    bink2_bits_init(&a, data, size);
    bink2_bits_init(&b, data, size);
    a.bitpos = b.bitpos = start;
    got = bink2_bits_read_vlc(&a, book->codes, book->lengths, book->count);
    want = ref_vlc(&b, book->codes, book->lengths, book->count);
    CHECK(got == want);
    CHECK(a.bitpos == b.bitpos);
    return 0;
}

static int test_vlc_matches_reference(void)
{
    Codebook books[7];
    uint32_t seed = 11;
    int nb = 0, k, round;

    books[nb].codes = bink2g_ac_skip_codes[0];
    books[nb].lengths = bink2g_ac_skip_bits[0];
    books[nb++].count = 14;
    books[nb].codes = bink2g_ac_skip_codes[1];
    books[nb].lengths = bink2g_ac_skip_bits[1];
    books[nb++].count = 14;
    books[nb].codes = bink2g_mv_codes;
    books[nb].lengths = bink2g_mv_bits;
    books[nb++].count = 16;
    books[nb].codes = bink2f_ac_val_codes[0];
    books[nb].lengths = bink2f_ac_val_bits[0];
    books[nb++].count = 13;
    books[nb].codes = bink2f_ac_skip_codes[1];
    books[nb].lengths = bink2f_ac_skip_bits[1];
    books[nb++].count = 14;
    books[nb].codes = odd_codes;
    books[nb].lengths = odd_lengths;
    books[nb++].count = 7;

    for (round = 0; round < 4000; round++) {
        unsigned char *heap;
        size_t size, start, i;

        seed = seed * 1664525u + 1013904223u;
        size = (seed >> 27) % 20;    /* both peek paths */
        heap = (unsigned char *)malloc(size > 0 ? size : 1);
        CHECK(heap != NULL);
        for (i = 0; i < size; i++) {
            seed = seed * 1664525u + 1013904223u;
            heap[i] = (unsigned char)(seed >> 24);
            if (round & 1)
                heap[i] &= 0x0F;    /* long runs of zeros */
        }
        seed = seed * 1664525u + 1013904223u;
        start = (seed >> 8) % (size * 8 + 3);   /* may start past the end */
        for (k = 0; k < nb; k++) {
            if (vlc_case(heap, size, start, &books[k]) != 0) {
                free(heap);
                return 1;
            }
        }
        {
            Bink2Bits a, b;
            int stop = (int)((seed >> 4) & 1);
            int max = (int)((seed >> 5) % 40);

            bink2_bits_init(&a, heap, size);
            bink2_bits_init(&b, heap, size);
            a.bitpos = b.bitpos = start;
            if (bink2_bits_read_unary(&a, stop, max) !=
                    ref_unary(&b, stop, max) ||
                a.bitpos != b.bitpos) {
                free(heap);
                CHECK(!"unary differs from reference");
            }
        }
        free(heap);
    }
    return 0;
}

int main(void)
{
    CHECK(test_bitreader() == 0);
    CHECK(test_reader_matches_reference() == 0);
    CHECK(test_vlc_matches_reference() == 0);
    CHECK(test_failed_stays_failed() == 0);
    CHECK(test_writer_failure_sticks() == 0);
    CHECK(test_writer_roundtrip() == 0);
    CHECK(test_writer_wrap() == 0);
    CHECK(test_unary() == 0);
    return 0;
}
