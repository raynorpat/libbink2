#include "bink2_enc_bits.h"

void bink2_bc_init(Bink2BitCounter *counter)
{
    if (counter == NULL)
        return;
    counter->bits = 0;
}

void bink2_bc_put(Bink2BitCounter *counter, unsigned value, int count)
{
    (void)value;
    if (counter == NULL || count < 0)
        return;
    counter->bits += (size_t)count;
}

size_t bink2_bc_bits(const Bink2BitCounter *counter)
{
    if (counter == NULL)
        return 0;
    return counter->bits;
}
