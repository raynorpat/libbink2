#ifndef LIBBINK2_THREAD_H
#define LIBBINK2_THREAD_H

/* A small fixed pool for running a frame's slices in parallel. */
typedef struct Bink2Pool Bink2Pool;
typedef void (*Bink2Job)(void *arg, int index);

/* Starts `workers` threads (>= 1). NULL on any failure; nothing leaks. */
Bink2Pool *bink2_pool_create(int workers);
/* Runs fn(arg, i) for i in [0, count) on the workers and the caller, and
 * returns after all calls finish. Not reentrant. */
void bink2_pool_run(Bink2Pool *pool, Bink2Job fn, void *arg, int count);
/* Joins the workers and frees the pool. NULL is a no-op. */
void bink2_pool_destroy(Bink2Pool *pool);
/* Online CPUs, at least 1. */
int bink2_cpu_count(void);
/* BINK2_THREADS as an integer in 1..64, or 0 when unset or invalid. */
int bink2_env_thread_limit(void);

#endif
