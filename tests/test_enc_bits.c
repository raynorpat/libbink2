#include "bink2_enc_bits.h"
#include "test_common.h"

int main(void)
{
    Bink2BitCounter counter;

    bink2_bc_init(&counter);
    CHECK(bink2_bc_bits(&counter) == 0);
    bink2_bc_put(&counter, 5, 3);
    CHECK(bink2_bc_bits(&counter) == 3);
    bink2_bc_put(&counter, 17, 5);
    CHECK(bink2_bc_bits(&counter) == 8);
    bink2_bc_put(&counter, 0xFFFFFFFFu, 32);
    CHECK(bink2_bc_bits(&counter) == 40);
    bink2_bc_init(&counter);
    CHECK(bink2_bc_bits(&counter) == 0);
    return 0;
}
