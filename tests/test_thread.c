#include "bink2_thread.h"
#include "test_common.h"

typedef struct {
    int hits[64];
} Hits;

static void count_job(void *arg, int index)
{
    Hits *h = (Hits *)arg;

    h->hits[index]++;
}

static int test_runs_every_index(void)
{
    Bink2Pool *pool = bink2_pool_create(3);
    Hits h;
    int run, i;

    CHECK(pool != NULL);
    memset(&h, 0, sizeof(h));
    for (run = 0; run < 1000; run++)
        bink2_pool_run(pool, count_job, &h, 8);
    for (i = 0; i < 8; i++)
        CHECK(h.hits[i] == 1000);
    for (i = 8; i < 64; i++)
        CHECK(h.hits[i] == 0);
    bink2_pool_destroy(pool);
    return 0;
}

static int test_count_below_workers(void)
{
    Bink2Pool *pool = bink2_pool_create(7);
    Hits h;
    int i;

    CHECK(pool != NULL);
    memset(&h, 0, sizeof(h));
    bink2_pool_run(pool, count_job, &h, 2);
    CHECK(h.hits[0] == 1);
    CHECK(h.hits[1] == 1);
    for (i = 2; i < 64; i++)
        CHECK(h.hits[i] == 0);
    bink2_pool_destroy(pool);
    return 0;
}

static int test_count_zero(void)
{
    Bink2Pool *pool = bink2_pool_create(2);
    Hits h;
    int i;

    CHECK(pool != NULL);
    memset(&h, 0, sizeof(h));
    bink2_pool_run(pool, count_job, &h, 0);
    for (i = 0; i < 64; i++)
        CHECK(h.hits[i] == 0);
    bink2_pool_destroy(pool);
    return 0;
}

static int test_create_destroy_loop(void)
{
    int i;

    for (i = 0; i < 200; i++) {
        Bink2Pool *pool = bink2_pool_create(1 + i % 4);

        CHECK(pool != NULL);
        bink2_pool_destroy(pool);
    }
    bink2_pool_destroy(NULL);
    return 0;
}

static int test_cpu_count(void)
{
    CHECK(bink2_cpu_count() >= 1);
    return 0;
}

int main(void)
{
    CHECK(test_runs_every_index() == 0);
    CHECK(test_count_below_workers() == 0);
    CHECK(test_count_zero() == 0);
    CHECK(test_create_destroy_loop() == 0);
    CHECK(test_cpu_count() == 0);
    return 0;
}
