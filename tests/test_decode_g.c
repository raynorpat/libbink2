#include "bink2.h"
#include "bink2_video.h"
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

static int test_slice_layout(void)
{
    int heights[8];

    CHECK(bink2_video_slice_heights('g', 0, 96, heights) == 1);
    CHECK(heights[0] == 96);
    CHECK(bink2_video_slice_heights('g', 0, 576, heights) == 2);
    CHECK(heights[0] == 288 && heights[1] == 576);

    CHECK(bink2_video_slice_heights('h', 0, 576, heights) == 2);
    CHECK(heights[0] == 288 && heights[1] == 576);
    CHECK(bink2_video_slice_heights('h', 1, 576, heights) == 3);
    CHECK(heights[2] == 576);
    CHECK(bink2_video_slice_heights('h', 2, 576, heights) == 4);
    CHECK(heights[3] == 576);
    CHECK(bink2_video_slice_heights('h', 3, 576, heights) == 8);
    CHECK(heights[7] == 576);
    return 0;
}

static int test_fixture(void)
{
    bink2 b;
    int status;
    unsigned long w = 0, h = 0;
    unsigned long frame;

    b = bink2_open_file(BINK2_FIXTURE_DIR "/synthetic_kb2j.bk2",
                        BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_info_video(b, &w, &h, NULL) == 0);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_enable_filters(b, 0) == 0);
    for (frame = 0; frame < 32; frame++) {
        status = frame == 0 ? bink2_first(b) : bink2_next(b);
        CHECK(status != BINK2_ERROR);
        CHECK(plane_hash(b, 0, (int)w, (int)h) ==
              bink2_kb2j_checksums[frame].y);
        CHECK(plane_hash(b, 1, (int)((w + 1) / 2),
                         (int)((h + 1) / 2)) ==
              bink2_kb2j_checksums[frame].u);
        CHECK(plane_hash(b, 2, (int)((w + 1) / 2),
                         (int)((h + 1) / 2)) ==
              bink2_kb2j_checksums[frame].v);
    }
    bink2_close(b);
    return 0;
}

int main(void)
{
    bink2 b;
    int skip;
    int status;
    unsigned long w = 0, h = 0;

    CHECK(test_slice_layout() == 0);
    /* The KB2j fixture ships with the repo, so exercise it even when no
     * external media directory is configured. */
    CHECK(test_fixture() == 0);

    skip = open_sample("ge_video_86l.bk2", &b);
    if (skip)
        return skip;
    CHECK(bink2_info_video(b, &w, &h, NULL) == 0);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_enable_filters(b, 0) == 0);
    {
        unsigned long frame;
        unsigned long mbw = 0, mbh = 0;
        const int16_t *mvx = NULL;
        const int16_t *mvy = NULL;
        const unsigned char *count = NULL;

        for (frame = 0; frame < 490; frame++) {
            status = frame == 0 ? bink2_first(b) : bink2_next(b);
            if (status == BINK2_ERROR) {
                fprintf(stderr, "decode_g failed at frame %lu\n", frame);
                return 1;
            }
            CHECK(plane_hash(b, 0, (int)w, (int)h) ==
                  bink2_kb2g_checksums[frame].y);
            CHECK(plane_hash(b, 1, (int)((w + 1) / 2),
                             (int)((h + 1) / 2)) ==
                  bink2_kb2g_checksums[frame].u);
            CHECK(plane_hash(b, 2, (int)((w + 1) / 2),
                             (int)((h + 1) / 2)) ==
                  bink2_kb2g_checksums[frame].v);
            if (frame == 1) {
                CHECK(bink2_get_motion(b, &mbw, &mbh, &mvx, &mvy,
                                       &count) == 1);
                CHECK(mbw == (w + 31) / 32 && mbh == (h + 31) / 32);
            }
        }
        CHECK(bink2_next(b) == BINK2_LAST);
    }

    bink2_close(b);
    return 0;
}
