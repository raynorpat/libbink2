#include "bink2_bits.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void bink2_bits_init(Bink2Bits *bits, const unsigned char *data, size_t size)
{
    bits->data = data;
    bits->size = size;
    bits->bitpos = 0;
}

int bink2_bits_left(const Bink2Bits *bits)
{
    size_t total = bits->size * 8;

    if (bits->bitpos >= total)
        return 0;
    return (int)(total - bits->bitpos);
}

int bink2_bits_failed(const Bink2Bits *bits)
{
    return bits->bitpos > bits->size * 8;
}

static int host_is_little_endian(void)
{
    const uint16_t one = 1;
    unsigned char first;

    memcpy(&first, &one, 1);
    return first == 1;
}

/* The next count (1..32) bits, LSB-first, without consuming them. The
 * caller guarantees they are inside the buffer; no byte past the buffer is
 * read. */
static uint32_t peek_bits(const Bink2Bits *bits, int count)
{
    size_t byte = bits->bitpos >> 3;
    const unsigned char *p = bits->data + byte;
    int shift = (int)(bits->bitpos & 7);
    uint64_t window = 0;

    if (byte + 8 <= bits->size && host_is_little_endian()) {
        memcpy(&window, p, 8);
    } else {
        int nbytes = (shift + count + 7) >> 3;
        int i;

        for (i = 0; i < nbytes; i++)
            window |= (uint64_t)p[i] << (8 * i);
    }
    return (uint32_t)((window >> shift) & (((uint64_t)1 << count) - 1));
}

int bink2_bits_read(Bink2Bits *bits, int count)
{
    uint32_t value;
    size_t total;

    if (count < 0 || count > 32)
        return -1;
    total = bits->size * 8;
    if (bits->bitpos > total || (size_t)count > total - bits->bitpos)
        return -1;
    if (count == 0)
        return 0;
    value = peek_bits(bits, count);
    bits->bitpos += (size_t)count;
    return (int)value;
}

int bink2_bits_read1(Bink2Bits *bits)
{
    size_t pos = bits->bitpos;

    if (pos >= bits->size * 8)
        return -1;
    bits->bitpos = pos + 1;
    return (bits->data[pos >> 3] >> (pos & 7)) & 1;
}

int bink2_bits_read_unary(Bink2Bits *bits, int stop, int max)
{
    size_t total = bits->size * 8;
    int done = 0;

    if (max < 0)
        return -1;
    while (done < max) {
        size_t avail;
        uint32_t window, hit;
        int n, k;

        if (bits->bitpos >= total)
            return -1;
        avail = total - bits->bitpos;
        n = max - done < 24 ? max - done : 24;
        if ((size_t)n > avail)
            n = (int)avail;
        window = peek_bits(bits, n);
        hit = stop == 1 ? window : stop == 0 ? ~window : 0;
        hit &= ((uint32_t)1 << n) - 1;
        if (hit != 0) {
            for (k = 0; !(hit & 1u); k++)
                hit >>= 1;
            bits->bitpos += (size_t)k + 1;
            return done + k;
        }
        bits->bitpos += (size_t)n;
        done += n;
    }
    return max;
}

int bink2_bits_read_vlc(Bink2Bits *bits, const uint16_t *codes,
                        const uint8_t *lengths, int count)
{
    size_t total = bits->size * 8;
    uint32_t window;
    int n, i, best = -1, best_len = 13;

    if (bits->bitpos >= total)
        return -1;
    n = total - bits->bitpos < 12 ? (int)(total - bits->bitpos) : 12;
    window = peek_bits(bits, n);
    /* The shortest matching code wins, then the lowest index: the order a
     * bit-at-a-time search finds them in. */
    for (i = 0; i < count; i++) {
        int len = lengths[i];

        if (len >= 1 && len <= n && len < best_len &&
            (window & (((uint32_t)1 << len) - 1)) == codes[i]) {
            best = i;
            best_len = len;
        }
    }
    if (best < 0) {
        /* The search read every bit it could before giving up. */
        bits->bitpos += (size_t)n;
        return -1;
    }
    bits->bitpos += (size_t)best_len;
    return best;
}

void bink2_bits_skip(Bink2Bits *bits, int count)
{
    size_t total = bits->size * 8;
    size_t next;

    /* A failed reader (bitpos past the end) stays failed; adding to it
     * could wrap it back to a readable position. */
    if (count < 0 || bits->bitpos > total)
        return;
    next = bits->bitpos + (size_t)count;
    bits->bitpos = (next > total) ? total : next;
}

void bink2_bits_align32(Bink2Bits *bits)
{
    size_t total = bits->size * 8;
    size_t next;

    if (bits->bitpos > total)
        return;
    next = (bits->bitpos + 31) & ~(size_t)31;
    bits->bitpos = (next > total) ? total : next;
}

void bink2_bw_init(Bink2BitWriter *bw)
{
    bw->data = NULL;
    bw->capacity = 0;
    bw->bitpos = 0;
    bw->failed = 0;
}

void bink2_bw_free(Bink2BitWriter *bw)
{
    if (bw == NULL)
        return;
    free(bw->data);
    bw->data = NULL;
    bw->capacity = 0;
    bw->bitpos = 0;
    bw->failed = 0;
}

void bink2_bw_reset(Bink2BitWriter *bw)
{
    bw->bitpos = 0;
    bw->failed = 0;
    if (bw->data != NULL && bw->capacity > 0)
        memset(bw->data, 0, bw->capacity);
}

static int bink2_bw_ensure(Bink2BitWriter *bw, size_t bitpos)
{
    size_t need = (bitpos + 7) / 8;
    size_t cap;
    unsigned char *grown;

    if (need <= bw->capacity)
        return 0;
    cap = bw->capacity != 0 ? bw->capacity : 256;
    while (cap < need) {
        if (cap > (size_t)-1 / 2)
            return -1;
        cap *= 2;
    }
    grown = (unsigned char *)realloc(bw->data, cap);
    if (grown == NULL)
        return -1;
    memset(grown + bw->capacity, 0, cap - bw->capacity);
    bw->data = grown;
    bw->capacity = cap;
    return 0;
}

int bink2_bw_put(Bink2BitWriter *bw, unsigned value, int count)
{
    int i;

    if (bw == NULL || bw->failed)
        return -1;
    if (count < 0 || count > 32) {
        bw->failed = 1;
        return -1;
    }
    if (count == 0)
        return 0;
    if (bink2_bw_ensure(bw, bw->bitpos + (size_t)count) != 0) {
        bw->failed = 1;
        return -1;
    }
    for (i = 0; i < count; i++) {
        size_t pos = bw->bitpos + (size_t)i;

        if ((value >> i) & 1u)
            bw->data[pos >> 3] =
                (unsigned char)(bw->data[pos >> 3] | (1u << (pos & 7)));
    }
    bw->bitpos += (size_t)count;
    return 0;
}

int bink2_bw_align32(Bink2BitWriter *bw)
{
    size_t next;

    if (bw == NULL || bw->failed)
        return -1;
    next = (bw->bitpos + 31) & ~(size_t)31;
    if (bink2_bw_ensure(bw, next) != 0) {
        bw->failed = 1;
        return -1;
    }
    bw->bitpos = next;
    return 0;
}

int bink2_bw_failed(const Bink2BitWriter *bw)
{
    return bw == NULL || bw->failed;
}

size_t bink2_bw_bitpos(const Bink2BitWriter *bw)
{
    return bw->bitpos;
}

size_t bink2_bw_bytes(const Bink2BitWriter *bw)
{
    return (bw->bitpos + 7) / 8;
}

const unsigned char *bink2_bw_data(const Bink2BitWriter *bw)
{
    return bw->data;
}
