#include "bink2.h"
#include "reference_checksums.h"
#include "test_common.h"

#include <stdio.h>

static int open_sample(const char *name, bink2 *out)
{
    const char *dir = test_media_dir();
    char path[1024];

    if (dir == NULL)
        return 77;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    *out = bink2_open_file(path, BINK2_MODE_DISK);
    if (*out == NULL)
        return 77;
    return 0;
}

static uint64_t plane_hash(bink2 b, int plane, int width, int height)
{
    return fnv1a_rows(bink2_get_plane(b, plane), width, height,
                      bink2_get_stride(b, plane));
}

static uint64_t plane_sum(bink2 b, int plane, int width, int height)
{
    const unsigned char *p = bink2_get_plane(b, plane);
    int stride = bink2_get_stride(b, plane);
    uint64_t sum = 0;
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            sum += p[x];
        p += stride;
    }
    return sum;
}

static int64_t abs_diff64(uint64_t a, uint64_t b)
{
    return a > b ? (int64_t)(a - b) : (int64_t)(b - a);
}

static int check_frame(bink2 b, unsigned long w, unsigned long h,
                       unsigned long frame)
{
    int cw = (int)((w + 1) / 2);
    int ch = (int)((h + 1) / 2);

    CHECK(plane_hash(b, 0, (int)w, (int)h) ==
          bink2_kb2f_checksums[frame].y);
    CHECK(plane_hash(b, 1, cw, ch) == bink2_kb2f_checksums[frame].u);
    CHECK(plane_hash(b, 2, cw, ch) == bink2_kb2f_checksums[frame].v);
    return 0;
}

int main(void)
{
    bink2 b;
    int skip;
    int status;
    unsigned long w = 0, h = 0;
    unsigned long frame;
    unsigned long current = 0;

    skip = open_sample("sc13_01_partial.bk2", &b);
    if (skip)
        return skip;
    CHECK(bink2_info_video(b, &w, &h, NULL) == 0);
    CHECK(bink2_enable_video(b, 1) == 0);

    for (frame = 0; frame < 236; frame++) {
        status = frame == 0 ? bink2_first(b) : bink2_next(b);
        CHECK(status != BINK2_ERROR);
        CHECK(check_frame(b, w, h, frame) == 0);
    }

    status = bink2_next(b);
    CHECK(status == BINK2_ERROR);
    status = bink2_next(b);
    CHECK(status == BINK2_LAST);

    CHECK(bink2_seek_keyframe(b, 200) != BINK2_ERROR);
    CHECK(bink2_info_all(b, &current, NULL, NULL) == 0);
    CHECK(current == 0);
    CHECK(check_frame(b, w, h, 0) == 0);

    {
        unsigned long mbw = 0, mbh = 0;
        const int16_t *mvx = NULL;
        const int16_t *mvy = NULL;
        const unsigned char *count = NULL;
        unsigned long i;

        CHECK(bink2_seek_keyframe(b, 2) != BINK2_ERROR);
        CHECK(bink2_next(b) != BINK2_ERROR);
        CHECK(bink2_get_motion(b, &mbw, &mbh, &mvx, &mvy, &count) == 1);
        CHECK(mbw == (w + 31) / 32 && mbh == (h + 31) / 32);
        for (i = 0; i < mbw * mbh; i++)
            CHECK(count[i] == 0 || count[i] == 1 || count[i] == 4);
    }

    bink2_close(b);
    return 0;
}
