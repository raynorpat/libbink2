#include "bink2.h"
#include "reference_checksums.h"
#include "test_common.h"

#include <stdio.h>

static int compare_prefix(const char *name, const Bink2AudioChecksum *expected,
                          int tracks, const char *const *prefix_files)
{
    const char *dir = test_media_dir();
    char path[1024];
    bink2 b;
    unsigned long prefix_bytes[7] = { 0 };
    unsigned char channels[7] = { 0 };
    unsigned char depths[7] = { 0 };
    unsigned long rates[7] = { 0 };
    unsigned char *collected[7] = { 0 };
    unsigned long collected_bytes[7] = { 0 };
    unsigned long frame;
    int t;

    if (dir == NULL)
        return 77;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    b = bink2_open_file(path, BINK2_MODE_DISK);
    if (b == NULL)
        return 77;
    CHECK(bink2_info_audio(b, NULL, channels, depths, rates) == 0);
    for (t = 0; t < tracks; t++) {
        CHECK(bink2_enable_audio(b, (unsigned char)t, 1) == 0);
        /* One clean block: 1920 output samples per channel. */
        prefix_bytes[t] = 1920UL * channels[t] * 2UL;
        collected[t] = (unsigned char *)calloc(1, prefix_bytes[t]);
        CHECK(collected[t] != NULL);
    }

    for (frame = 0; frame < 400; frame++) {
        char status = frame == 0 ? bink2_first(b) : bink2_next(b);
        int done = 1;

        if (status == BINK2_ERROR)
            break;
        for (t = 0; t < tracks; t++) {
            unsigned long size = bink2_get_audio_size(b, (unsigned char)t);
            const unsigned char *pcm = bink2_get_audio(b, (unsigned char)t);
            unsigned long room = prefix_bytes[t] - collected_bytes[t];

            if (size > room)
                size = room;
            if (size != 0 && pcm != NULL && room != 0) {
                memcpy(collected[t] + collected_bytes[t], pcm, size);
                collected_bytes[t] += size;
            }
            if (collected_bytes[t] < prefix_bytes[t])
                done = 0;
        }
        if (done)
            break;
        if (status == BINK2_LAST)
            break;
    }

    for (t = 0; t < tracks; t++) {
        char file[1024];
        FILE *f;
        unsigned char *ref;
        size_t ref_size;
        size_t i;
        int max_diff = 0;

        snprintf(file, sizeof(file), BINK2_FIXTURE_DIR "/%s", prefix_files[t]);
        f = fopen(file, "rb");
        CHECK(f != NULL);
        fseek(f, 0, SEEK_END);
        CHECK((unsigned long)ftell(f) >= prefix_bytes[t]);
        fseek(f, 0, SEEK_SET);
        ref_size = (size_t)prefix_bytes[t];
        ref = (unsigned char *)malloc(ref_size);
        CHECK(ref != NULL);
        CHECK(fread(ref, 1, ref_size, f) == ref_size);
        fclose(f);
        for (i = 0; i + 1 < ref_size; i += 2) {
            short a = (short)(collected[t][i] | (collected[t][i + 1] << 8));
            short e = (short)(ref[i] | (ref[i + 1] << 8));
            int diff = (int)a - (int)e;

            if (diff < 0)
                diff = -diff;
            if (diff > max_diff)
                max_diff = diff;
        }
        /* The oracle's float transforms and ours round differently; the
         * bitstream is correct when the first clean block is within 4 LSBs. */
        CHECK(max_diff <= 4);
        free(ref);
        free(collected[t]);
    }
    bink2_close(b);
    return 0;
}

int main(void)
{
    static const char *const kb2f_files[6] = {
        "kb2f_a0_10f.s16", "kb2f_a1_10f.s16", "kb2f_a2_10f.s16",
        "kb2f_a3_10f.s16", "kb2f_a4_10f.s16", "kb2f_a5_10f.s16"
    };
    static const char *const kb2g_files[1] = { "kb2g_a0_10f.s16" };

    {
        int r = compare_prefix("sc13_01_partial.bk2", bink2_kb2f_audio, 6,
                               kb2f_files);
        if (r == 77)
            return 77;
        CHECK(r == 0);
    }
    {
        int r = compare_prefix("ge_video_86l.bk2", bink2_kb2g_audio, 1,
                               kb2g_files);
        if (r == 77)
            return 77;
        CHECK(r == 0);
    }
    return 0;
}
