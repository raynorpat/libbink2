#include "bink2.h"
#include "bink2_encoder.h"
#include "bink2_internal.h"
#include "bink2_thread.h"
#include "test_common.h"

#include <stdio.h>

#define FRAMES 12

typedef struct {
    int width;
    int height;
} Size;

/* A textured picture that pans and changes, so the encoder emits intra,
 * skip, motion and residual macroblocks. */
static int read_yuv(void *user, unsigned long frame, unsigned char *y,
                    unsigned char *u, unsigned char *v)
{
    const Size *s = (const Size *)user;
    int cw = (s->width + 1) / 2;
    int ch = (s->height + 1) / 2;
    int f = (int)frame;
    int px, row;

    for (row = 0; row < s->height; row++)
        for (px = 0; px < s->width; px++) {
            int xx = px + f * 3, yy = row + f;
            int val = xx * xx / 97 + yy * yy / 131 +
                      (((xx / 24) ^ (yy / 24)) & 1) * 60;

            if (f % 5 == 4 && px < s->width / 3)
                val += (px * 31 + row * 17) & 0x3F;
            y[row * s->width + px] = (unsigned char)(val & 0xFF);
        }
    for (row = 0; row < ch; row++)
        for (px = 0; px < cw; px++) {
            u[row * cw + px] = (unsigned char)((128 + px + f) & 0xFF);
            v[row * cw + px] = (unsigned char)((128 - row + 2 * f) & 0xFF);
        }
    return 0;
}

static int encode(const char *path, int width, int height, unsigned slices)
{
    bink2_enc_source src;
    bink2_enc_options options;
    Size size;
    char err[256];

    size.width = width;
    size.height = height;
    memset(&src, 0, sizeof(src));
    src.width = width;
    src.height = height;
    src.frame_count = FRAMES;
    src.fps_num = 30;
    src.fps_den = 1;
    src.read_frame_yuv = read_yuv;
    src.user = &size;
    bink2_enc_options_default(&options);
    options.keyframe_interval = 4;
    options.slices = slices;
    if (bink2_enc_encode(&src, &options, path, err, sizeof(err)) !=
        BINK2_ENC_OK) {
        fprintf(stderr, "encode %s: %s\n", path, err);
        return -1;
    }
    return 0;
}

static uint64_t frame_hash(bink2 b)
{
    unsigned long w = 0, h = 0, flags = 0;
    const unsigned char *rgb;
    uint64_t hash = 0;
    int p;

    bink2_info_video(b, &w, &h, &flags);
    for (p = 0; p < 3; p++) {
        int pw = p == 0 ? (int)w : (int)((w + 1) / 2);
        int ph = p == 0 ? (int)h : (int)((h + 1) / 2);

        hash = hash * 31 + fnv1a_rows(bink2_get_plane(b, p), pw, ph,
                                      bink2_get_stride(b, p));
    }
    rgb = bink2_get_video(b);
    if (rgb == NULL)
        return 0;
    return hash * 31 + fnv1a(rgb, (size_t)w * h * 3);
}

static bink2 open_with(const char *path, int threads)
{
    bink2 b = bink2_open_file(path, BINK2_MODE_DISK);

    if (b == NULL)
        return NULL;
    bink2_enable_video(b, 1);
    bink2_internal_set_threads(b, threads);
    return b;
}

/* Decodes every frame and stores its hash; returns 0 on success. */
static int decode_all(const char *path, int threads, uint64_t out[FRAMES])
{
    bink2 b = open_with(path, threads);
    int i;

    if (b == NULL)
        return -1;
    for (i = 0; i < FRAMES; i++) {
        char status = i == 0 ? bink2_first(b) : bink2_next(b);

        if (status == BINK2_ERROR) {
            bink2_close(b);
            return -1;
        }
        out[i] = frame_hash(b);
    }
    bink2_close(b);
    return 0;
}

static int compare_modes(const char *path)
{
    uint64_t serial[FRAMES], threaded[FRAMES];
    int i;

    CHECK(decode_all(path, 1, serial) == 0);
    CHECK(decode_all(path, 8, threaded) == 0);
    for (i = 0; i < FRAMES; i++) {
        if (serial[i] != threaded[i]) {
            fprintf(stderr, "%s: frame %d differs\n", path, i);
            return 1;
        }
    }
    return 0;
}

static int test_slices_match_serial(void)
{
    static const unsigned counts[4] = { 2, 3, 4, 8 };
    int i;

    for (i = 0; i < 4; i++) {
        char path[64];

        snprintf(path, sizeof(path), "test_threads_s%u.bk2", counts[i]);
        CHECK(encode(path, 320, 288, counts[i]) == 0);
        CHECK(compare_modes(path) == 0);
    }
    return 0;
}

/* 64 rows split 8 ways leaves slices with no macroblock rows. */
static int test_empty_slices(void)
{
    CHECK(encode("test_threads_small.bk2", 96, 64, 8) == 0);
    CHECK(compare_modes("test_threads_small.bk2") == 0);
    return 0;
}

static int test_seek_after_threaded(void)
{
    uint64_t serial[FRAMES];
    bink2 b;
    int i;

    CHECK(decode_all("test_threads_s8.bk2", 1, serial) == 0);
    b = open_with("test_threads_s8.bk2", 8);
    CHECK(b != NULL);
    CHECK(bink2_first(b) != BINK2_ERROR);
    for (i = 1; i < FRAMES; i++)
        CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(bink2_seek_keyframe(b, 8) != BINK2_ERROR);
    CHECK(frame_hash(b) == serial[8]);
    CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(frame_hash(b) == serial[9]);
    CHECK(bink2_first(b) != BINK2_ERROR);
    CHECK(frame_hash(b) == serial[0]);
    CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(frame_hash(b) == serial[1]);
    bink2_close(b);
    return 0;
}

static int test_open_close_no_decode(void)
{
    int i;

    for (i = 0; i < 100; i++) {
        bink2 b = open_with("test_threads_s8.bk2", 8);

        CHECK(b != NULL);
        bink2_close(b);
    }
    return 0;
}

typedef struct {
    uint64_t hashes[4][FRAMES];
    int status[4];
} Parallel;

static void parallel_job(void *arg, int index)
{
    Parallel *p = (Parallel *)arg;

    p->status[index] = decode_all("test_threads_s8.bk2", 0,
                                  p->hashes[index]);
}

/* Several handles decoding at once on application threads, each with its
 * own slice threads. */
static int test_parallel_handles(void)
{
    uint64_t serial[FRAMES];
    Parallel p;
    Bink2Pool *pool;
    int i, j;

    CHECK(decode_all("test_threads_s8.bk2", 1, serial) == 0);
    memset(&p, 0, sizeof(p));
    pool = bink2_pool_create(3);
    CHECK(pool != NULL);
    bink2_pool_run(pool, parallel_job, &p, 4);
    bink2_pool_destroy(pool);
    for (i = 0; i < 4; i++) {
        CHECK(p.status[i] == 0);
        for (j = 0; j < FRAMES; j++)
            CHECK(p.hashes[i][j] == serial[j]);
    }
    return 0;
}

/* Odd sizes leave partial chroma pairs and partial bands for RGB. */
static int test_rgb_odd_size(void)
{
    CHECK(encode("test_threads_odd.bk2", 333, 251, 4) == 0);
    CHECK(compare_modes("test_threads_odd.bk2") == 0);
    return 0;
}

int main(void)
{
    CHECK(test_slices_match_serial() == 0);
    CHECK(test_empty_slices() == 0);
    CHECK(test_seek_after_threaded() == 0);
    CHECK(test_open_close_no_decode() == 0);
    CHECK(test_parallel_handles() == 0);
    CHECK(test_rgb_odd_size() == 0);
    return 0;
}
