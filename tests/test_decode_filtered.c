#include "bink2.h"
#include "filtered_checksums.h"
#include "test_common.h"

#include <stdio.h>

static uint64_t plane_hash(bink2 b, int plane, int width, int height)
{
    return fnv1a_rows(bink2_get_plane(b, plane), width, height,
                      bink2_get_stride(b, plane));
}

/* Decode `frames` frames of `b` with the filters on and compare each
 * against the pinned array. */
static int check_stream(bink2 b, const Bink2FrameChecksum *want,
                        unsigned long frames, const char *name)
{
    unsigned long w = 0, h = 0, frame;
    int cw, ch;

    CHECK(bink2_info_video(b, &w, &h, NULL) == 0);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_enable_filters(b, 1) == 0);
    cw = (int)((w + 1) / 2);
    ch = (int)((h + 1) / 2);

    for (frame = 0; frame < frames; frame++) {
        int status = frame == 0 ? bink2_first(b) : bink2_next(b);

        if (status == BINK2_ERROR) {
            fprintf(stderr, "decode_filtered: %s failed at frame %lu\n",
                    name, frame);
            return 1;
        }
        if (plane_hash(b, 0, (int)w, (int)h) != want[frame].y ||
            plane_hash(b, 1, cw, ch) != want[frame].u ||
            plane_hash(b, 2, cw, ch) != want[frame].v) {
            fprintf(stderr, "decode_filtered: %s differs at frame %lu\n",
                    name, frame);
            return 1;
        }
    }
    return 0;
}

static int check_file(const char *path, const Bink2FrameChecksum *want,
                      unsigned long frames, const char *name)
{
    bink2 b = bink2_open_file(path, BINK2_MODE_DISK);
    int rc;

    CHECK(b != NULL);
    rc = check_stream(b, want, frames, name);
    bink2_close(b);
    return rc;
}

int main(void)
{
    const char *dir;
    char path[1024];
    bink2 b;

    /* The KB2j fixture ships with the repo, so check it even when no
     * external media directory is configured. The filters change nothing
     * in it (see tests/README.md), so these values equal the reference
     * ones; the samples below are what exercise the filters. */
    CHECK(check_file(BINK2_FIXTURE_DIR "/synthetic_kb2j.bk2",
                     bink2_kb2j_filtered_checksums, 32, "kb2j") == 0);

    dir = test_media_dir();
    if (dir == NULL)
        return 77;
    snprintf(path, sizeof(path), "%s/sc13_01_partial.bk2", dir);
    b = bink2_open_file(path, BINK2_MODE_DISK);
    if (b == NULL)
        return 77;
    bink2_close(b);
    CHECK(check_file(path, bink2_kb2f_filtered_checksums, 236, "kb2f") == 0);

    snprintf(path, sizeof(path), "%s/ge_video_86l.bk2", dir);
    b = bink2_open_file(path, BINK2_MODE_DISK);
    if (b == NULL)
        return 77;
    bink2_close(b);
    CHECK(check_file(path, bink2_kb2g_filtered_checksums, 490, "kb2g") == 0);
    return 0;
}
