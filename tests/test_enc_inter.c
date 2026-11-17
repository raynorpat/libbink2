#include "bink2.h"
#include "bink2_encoder.h"
#include "bink2_enc_motion.h"

#include "test_common.h"

#include <stdio.h>

#define TEST_WIDTH 320
#define TEST_HEIGHT 240
#define TEST_FRAMES 4

static unsigned char source_y(int x, int y, unsigned long frame)
{
    int base = (x + y) & 0xFF;

    if (frame == 2 || frame == 3) {
        int sx = x - 8;

        if (sx < 0)
            sx = 0;
        base = (sx + y) & 0xFF;
    }
    if (frame == 3)
        base += ((x * 31 + y * 17 + 3) & 0x3F) - 32;
    if (base < 0)
        base = 0;
    if (base > 255)
        base = 255;
    return (unsigned char)base;
}

static int read_yuv(void *user, unsigned long frame, unsigned char *y,
                    unsigned char *u, unsigned char *v)
{
    int cw = (TEST_WIDTH + 1) / 2;
    int ch = (TEST_HEIGHT + 1) / 2;
    int x, row;

    (void)user;
    for (row = 0; row < TEST_HEIGHT; row++)
        for (x = 0; x < TEST_WIDTH; x++)
            y[row * TEST_WIDTH + x] = source_y(x, row, frame);
    for (row = 0; row < ch; row++)
        for (x = 0; x < cw; x++) {
            u[row * cw + x] =
                (unsigned char)((128 + x + (int)frame) & 0xFF);
            v[row * cw + x] =
                (unsigned char)((128 - row + (int)frame) & 0xFF);
        }
    return 0;
}

static int check_plane(bink2 b, int plane, const unsigned char *ref, int width,
                       int height)
{
    const unsigned char *p = bink2_get_plane(b, plane);
    int stride = bink2_get_stride(b, plane);
    int row;

    CHECK(p != NULL);
    for (row = 0; row < height; row++)
        CHECK(memcmp(p + (size_t)row * stride, ref + (size_t)row * width,
                     (size_t)width) == 0);
    return 0;
}

static int test_mv_inversion(void)
{
    Bink2Video v;
    Bink2Mv desired[4] = { { 2, 0 }, { 4, 0 }, { 6, 0 }, { 8, 0 } };
    int raw[4][2];
    int predicted[4][2];
    int k;

    CHECK(bink2_video_init(&v, 64, 64, 0, 'j') == 0);
    v.old_mb_pos = 1;
    bink2_enc_invert_mv(&v, 0x80, desired, 4, raw);
    bink2_enc_predict_mv(&v, 0x80, raw, 4, predicted);
    for (k = 0; k < 4; k++) {
        CHECK(predicted[k][0] == desired[k].x);
        CHECK(predicted[k][1] == desired[k].y);
    }
    bink2_video_uninit(&v);
    return 0;
}

static int has_nonzero_motion(bink2 b)
{
    unsigned long mbw = 0, mbh = 0;
    const int16_t *mvx = NULL;
    const int16_t *mvy = NULL;
    const unsigned char *count = NULL;
    unsigned long i;

    CHECK(bink2_get_motion(b, &mbw, &mbh, &mvx, &mvy, &count) == 1);
    for (i = 0; i < mbw * mbh; i++) {
        if (count[i] != 0 && (mvx[i * 4] != 0 || mvy[i * 4] != 0))
            return 1;
    }
    return 0;
}

int main(void)
{
    bink2_enc_source src;
    bink2_enc_options options;
    char err[256];
    unsigned long frames = 0, w = 0, h = 0;
    unsigned long f;
    int cw = (TEST_WIDTH + 1) / 2;
    int ch = (TEST_HEIGHT + 1) / 2;
    size_t y_size = (size_t)TEST_WIDTH * TEST_HEIGHT;
    size_t c_size = (size_t)cw * ch;
    unsigned char *ref = (unsigned char *)malloc(y_size + c_size * 2);
    const Bink2EncStats *stats;
    FILE *file;
    bink2 b;

    CHECK(test_mv_inversion() == 0);

    memset(&src, 0, sizeof(src));
    src.width = TEST_WIDTH;
    src.height = TEST_HEIGHT;
    src.frame_count = TEST_FRAMES;
    src.fps_num = 30;
    src.fps_den = 1;
    src.read_frame_yuv = read_yuv;

    bink2_enc_options_default(&options);
    options.keyframe_interval = 150;
    options.recon_path = "test_enc_inter_recon.yuv";

    CHECK(bink2_enc_encode(&src, &options, "test_enc_inter.bk2", err,
                           sizeof(err)) == BINK2_ENC_OK);
    stats = bink2_enc_last_stats();
    CHECK(stats->skip > 0);
    CHECK(stats->motion > 0);
    CHECK(stats->residual > 0);

    file = fopen("test_enc_inter.bk2", "rb");
    CHECK(file != NULL);
    CHECK(fseek(file, 48, SEEK_SET) == 0);
    for (f = 0; f < TEST_FRAMES; f++) {
        unsigned char bytes[4];

        CHECK(fread(bytes, 1, 4, file) == 4);
        CHECK((bytes[0] & 1) == (f == 0 ? 1 : 0));
    }
    fclose(file);

    b = bink2_open_file("test_enc_inter.bk2", BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_info_all(b, NULL, &frames, NULL) == 0);
    CHECK(frames == TEST_FRAMES);
    CHECK(bink2_info_video(b, &w, &h, NULL) == 0);
    CHECK(w == TEST_WIDTH && h == TEST_HEIGHT);
    CHECK(bink2_enable_video(b, 1) == 0);

    file = fopen("test_enc_inter_recon.yuv", "rb");
    CHECK(file != NULL);
    for (f = 0; f < TEST_FRAMES; f++) {
        int status = f == 0 ? bink2_first(b) : bink2_next(b);

        CHECK(status != BINK2_ERROR);
        CHECK(fread(ref, 1, y_size, file) == y_size);
        CHECK(check_plane(b, 0, ref, TEST_WIDTH, TEST_HEIGHT) == 0);
        CHECK(fread(ref, 1, c_size, file) == c_size);
        CHECK(check_plane(b, 1, ref, cw, ch) == 0);
        CHECK(fread(ref, 1, c_size, file) == c_size);
        CHECK(check_plane(b, 2, ref, cw, ch) == 0);
        if (f == 2)
            CHECK(has_nonzero_motion(b) == 1);
    }
    fclose(file);
    bink2_close(b);
    free(ref);

    remove("test_enc_inter.bk2");
    remove("test_enc_inter_recon.yuv");
    return 0;
}
