#ifndef LIBBINK2_ENC_BITS_H
#define LIBBINK2_ENC_BITS_H

#include <stddef.h>

/** Counts the bits an encode pass would write, for rate decisions. */
typedef struct {
    size_t bits;
} Bink2BitCounter;

void bink2_bc_init(Bink2BitCounter *counter);
void bink2_bc_put(Bink2BitCounter *counter, unsigned value, int count);
size_t bink2_bc_bits(const Bink2BitCounter *counter);

#endif
