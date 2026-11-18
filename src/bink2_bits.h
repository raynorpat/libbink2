#ifndef LIBBINK2_BITS_H
#define LIBBINK2_BITS_H

#include <stddef.h>

/** LSB-first bit reader over a byte buffer. */
typedef struct {
    const unsigned char *data;
    size_t size;
    size_t bitpos;
} Bink2Bits;

void bink2_bits_init(Bink2Bits *bits, const unsigned char *data, size_t size);
/** Read count bits LSB-first, or -1 when fewer bits remain. count is 0..32. */
int bink2_bits_read(Bink2Bits *bits, int count);
int bink2_bits_read1(Bink2Bits *bits);
/**
 * Read a unary code: count the bits before the first bit equal to stop.
 * Returns max when max bits are read without finding stop, or -1 when the
 * stream ends first.
 */
int bink2_bits_read_unary(Bink2Bits *bits, int stop, int max);
int bink2_bits_left(const Bink2Bits *bits);
/** True after a read helper ran past the buffer (it marks bitpos). */
int bink2_bits_failed(const Bink2Bits *bits);
void bink2_bits_skip(Bink2Bits *bits, int count);
void bink2_bits_align32(Bink2Bits *bits);

/**
 * Growing LSB-first bit writer. The same bit order as Bink2Bits, so a buffer
 * produced here can be handed to bink2_bits_init.
 */
typedef struct {
    unsigned char *data;
    size_t capacity;
    size_t bitpos;
    int failed;
} Bink2BitWriter;

void bink2_bw_init(Bink2BitWriter *bw);
void bink2_bw_free(Bink2BitWriter *bw);
void bink2_bw_reset(Bink2BitWriter *bw);
/**
 * Write the low count bits of value, LSB first. count is 0..32. A failed
 * write marks the writer failed, and every later write fails until
 * bink2_bw_reset, so a caller can check bink2_bw_failed once at the end.
 */
int bink2_bw_put(Bink2BitWriter *bw, unsigned value, int count);
/** Pad with zero bits up to the next 32-bit boundary. */
int bink2_bw_align32(Bink2BitWriter *bw);
int bink2_bw_failed(const Bink2BitWriter *bw);
size_t bink2_bw_bitpos(const Bink2BitWriter *bw);
size_t bink2_bw_bytes(const Bink2BitWriter *bw);
const unsigned char *bink2_bw_data(const Bink2BitWriter *bw);

#endif
