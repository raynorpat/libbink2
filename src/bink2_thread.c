#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#endif

#include "bink2_thread.h"

#include <stdlib.h>

#if defined(_WIN32)
/* Condition variables need Windows Vista; older MinGW headers default to
 * XP and would hide them. */
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
typedef CRITICAL_SECTION Bink2Mutex;
typedef CONDITION_VARIABLE Bink2Cond;
typedef HANDLE Bink2Thread;
#define mutex_init(m) (InitializeCriticalSection(m), 0)
#define mutex_destroy(m) DeleteCriticalSection(m)
#define mutex_lock(m) EnterCriticalSection(m)
#define mutex_unlock(m) LeaveCriticalSection(m)
#define cond_init(c) (InitializeConditionVariable(c), 0)
#define cond_destroy(c) ((void)(c))
#define cond_wait(c, m) SleepConditionVariableCS(c, m, INFINITE)
#define cond_broadcast(c) WakeAllConditionVariable(c)
#define cond_signal(c) WakeConditionVariable(c)
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_mutex_t Bink2Mutex;
typedef pthread_cond_t Bink2Cond;
typedef pthread_t Bink2Thread;
#define mutex_init(m) pthread_mutex_init(m, NULL)
#define mutex_destroy(m) pthread_mutex_destroy(m)
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
#define cond_init(c) pthread_cond_init(c, NULL)
#define cond_destroy(c) pthread_cond_destroy(c)
#define cond_wait(c, m) pthread_cond_wait(c, m)
#define cond_broadcast(c) pthread_cond_broadcast(c)
#define cond_signal(c) pthread_cond_signal(c)
#endif

struct Bink2Pool {
    Bink2Mutex lock;
    Bink2Cond work;     /* a batch was posted, or quit */
    Bink2Cond done;     /* the last job of a batch finished */
    Bink2Thread *threads;
    int workers;
    int started;

    Bink2Job fn;
    void *arg;
    int count;
    int next;           /* next job index to hand out */
    int finished;       /* jobs finished in this batch */
    unsigned batch;     /* bumped for every posted batch */
    int quit;
};

/* Runs jobs of the current batch until none are left. Called and returns
 * with the lock held. */
static void take_jobs(Bink2Pool *pool)
{
    while (pool->next < pool->count) {
        int index = pool->next++;
        Bink2Job fn = pool->fn;
        void *arg = pool->arg;

        mutex_unlock(&pool->lock);
        fn(arg, index);
        mutex_lock(&pool->lock);
        if (++pool->finished == pool->count)
            cond_signal(&pool->done);
    }
}

static void worker_loop(Bink2Pool *pool)
{
    unsigned seen = 0;

    mutex_lock(&pool->lock);
    for (;;) {
        while (!pool->quit && pool->batch == seen)
            cond_wait(&pool->work, &pool->lock);
        if (pool->quit)
            break;
        seen = pool->batch;
        take_jobs(pool);
    }
    mutex_unlock(&pool->lock);
}

#if defined(_WIN32)
static DWORD WINAPI worker_main(LPVOID arg)
{
    worker_loop((Bink2Pool *)arg);
    return 0;
}
#else
static void *worker_main(void *arg)
{
    worker_loop((Bink2Pool *)arg);
    return NULL;
}
#endif

static int thread_start(Bink2Thread *t, Bink2Pool *pool)
{
#if defined(_WIN32)
    *t = CreateThread(NULL, 0, worker_main, pool, 0, NULL);
    return *t == NULL ? -1 : 0;
#else
    return pthread_create(t, NULL, worker_main, pool) == 0 ? 0 : -1;
#endif
}

static void thread_join(Bink2Thread t)
{
#if defined(_WIN32)
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
#else
    pthread_join(t, NULL);
#endif
}

void bink2_pool_destroy(Bink2Pool *pool)
{
    int i;

    if (pool == NULL)
        return;
    mutex_lock(&pool->lock);
    pool->quit = 1;
    cond_broadcast(&pool->work);
    mutex_unlock(&pool->lock);
    for (i = 0; i < pool->started; i++)
        thread_join(pool->threads[i]);
    cond_destroy(&pool->done);
    cond_destroy(&pool->work);
    mutex_destroy(&pool->lock);
    free(pool->threads);
    free(pool);
}

Bink2Pool *bink2_pool_create(int workers)
{
    Bink2Pool *pool;

    if (workers < 1)
        return NULL;
    pool = (Bink2Pool *)calloc(1, sizeof(*pool));
    if (pool == NULL)
        return NULL;
    pool->threads = (Bink2Thread *)calloc((size_t)workers,
                                          sizeof(Bink2Thread));
    if (pool->threads == NULL) {
        free(pool);
        return NULL;
    }
    if (mutex_init(&pool->lock) != 0) {
        free(pool->threads);
        free(pool);
        return NULL;
    }
    if (cond_init(&pool->work) != 0) {
        mutex_destroy(&pool->lock);
        free(pool->threads);
        free(pool);
        return NULL;
    }
    if (cond_init(&pool->done) != 0) {
        cond_destroy(&pool->work);
        mutex_destroy(&pool->lock);
        free(pool->threads);
        free(pool);
        return NULL;
    }
    pool->workers = workers;
    for (pool->started = 0; pool->started < workers; pool->started++) {
        if (thread_start(&pool->threads[pool->started], pool) != 0) {
            bink2_pool_destroy(pool);
            return NULL;
        }
    }
    return pool;
}

void bink2_pool_run(Bink2Pool *pool, Bink2Job fn, void *arg, int count)
{
    if (count <= 0)
        return;
    mutex_lock(&pool->lock);
    pool->fn = fn;
    pool->arg = arg;
    pool->count = count;
    pool->next = 0;
    pool->finished = 0;
    pool->batch++;
    cond_broadcast(&pool->work);
    take_jobs(pool);
    while (pool->finished < pool->count)
        cond_wait(&pool->done, &pool->lock);
    mutex_unlock(&pool->lock);
}

int bink2_cpu_count(void)
{
#if defined(_WIN32)
    SYSTEM_INFO info;

    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
#else
#if defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);

    return n > 0 ? (n > 1024 ? 1024 : (int)n) : 1;
#else
    /* Hidden by _POSIX_C_SOURCE on some BSDs; decode serially there
     * unless BINK2_THREADS asks for more. */
    return 1;
#endif
#endif
}

int bink2_env_thread_limit(void)
{
    const char *value = getenv("BINK2_THREADS");
    char *end;
    long n;

    if (value == NULL || *value == '\0')
        return 0;
    n = strtol(value, &end, 10);
    if (*end != '\0' || n < 1 || n > 64)
        return 0;
    return (int)n;
}
