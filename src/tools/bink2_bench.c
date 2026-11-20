#if !defined(_WIN32)
#define _POSIX_C_SOURCE 199309L
#endif

#include "bink2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

/* Wall-clock seconds. clock() would report CPU time summed over threads. */
static double now_seconds(void)
{
#if defined(_WIN32)
    LARGE_INTEGER freq, count;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static void usage(void)
{
    fprintf(stderr,
            "usage: bink2_bench [--iterations N] [--frames N] [--rgb] file.bk2\n"
            "  --iterations N  decode the file N times (default 5)\n"
            "  --frames N      stop each pass after N frames\n"
            "  --rgb           also convert every frame to RGB\n");
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    unsigned long iterations = 5;
    unsigned long frames = 0;
    unsigned long max_frames = 0;
    int rgb = 0;
    double start, elapsed;
    unsigned long it;
    bink2 b;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            char *end;

            iterations = strtoul(argv[++i], &end, 10);
            if (*end != '\0' || iterations == 0) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end;

            max_frames = strtoul(argv[++i], &end, 10);
            if (*end != '\0' || max_frames == 0) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--rgb") == 0) {
            rgb = 1;
        } else if (argv[i][0] == '-' || path != NULL) {
            usage();
            return 2;
        } else {
            path = argv[i];
        }
    }
    if (path == NULL) {
        usage();
        return 2;
    }

    b = bink2_open_file(path, BINK2_MODE_DISK);
    if (b == NULL) {
        fprintf(stderr, "bink2_bench: cannot open %s\n", path);
        return 1;
    }
    bink2_enable_video(b, 1);
    for (i = 0; i < 7; i++)
        bink2_enable_audio(b, (unsigned char)i, 0);

    start = now_seconds();
    for (it = 0; it < iterations; it++) {
        char status = bink2_first(b);
        unsigned long pass = 0;

        for (;;) {
            if (status == BINK2_ERROR) {
                fprintf(stderr, "bink2_bench: decode failed in %s\n", path);
                bink2_close(b);
                return 1;
            }
            frames++;
            pass++;
            if (rgb && bink2_get_video(b) == NULL) {
                fprintf(stderr, "bink2_bench: RGB conversion failed\n");
                bink2_close(b);
                return 1;
            }
            if (status != BINK2_MORE || pass == max_frames)
                break;
            status = bink2_next(b);
        }
    }
    elapsed = now_seconds() - start;
    bink2_close(b);

    printf("frames=%lu seconds=%.3f fps=%.1f\n", frames, elapsed,
           elapsed > 0.0 ? (double)frames / elapsed : 0.0);
    return 0;
}
