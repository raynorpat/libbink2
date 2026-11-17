#include "bink2.h"
#include "bink2_encoder.h"

#include "test_common.h"

#include <math.h>
#include <stdio.h>

#define TEST_WIDTH 160
#define TEST_HEIGHT 120
#define TEST_FPS 30

typedef struct {
    int black_frame;
} SourceCtx;

static int source_y(const SourceCtx *ctx, unsigned long frame, int x, int y)
{
    int bx, by, value;

    if (ctx->black_frame >= 0 && (int)frame == ctx->black_frame)
        return 0;
    value = (x * 3 + y * 5 + (int)frame * 4) & 0xFF;
    bx = ((int)frame * 3) % TEST_WIDTH;
    by = ((int)frame * 2) % TEST_HEIGHT;
    if (x >= bx && x < bx + 24 && y >= by && y < by + 24)
        value = 230;
    value ^= (x * 17 + y * 13 + (int)frame * 7) & 0x1F;
    return value & 0xFF;
}

static int read_yuv(void *user, unsigned long frame, unsigned char *y,
                    unsigned char *u, unsigned char *v)
{
    const SourceCtx *ctx = (const SourceCtx *)user;
    int cw = (TEST_WIDTH + 1) / 2;
    int ch = (TEST_HEIGHT + 1) / 2;
    int x, row;

    for (row = 0; row < TEST_HEIGHT; row++)
        for (x = 0; x < TEST_WIDTH; x++)
            y[row * TEST_WIDTH + x] =
                (unsigned char)source_y(ctx, frame, x, row);
    for (row = 0; row < ch; row++)
        for (x = 0; x < cw; x++) {
            u[row * cw + x] =
                (unsigned char)((128 + x + (int)frame) & 0xFF);
            v[row * cw + x] =
                (unsigned char)((128 - row + (int)frame) & 0xFF);
        }
    return 0;
}

static long file_size(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;

    if (file == NULL)
        return -1;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fclose(file);
    return size;
}

static int compare_recon(const char *path, const char *recon_path,
                         unsigned long frames)
{
    FILE *recon = fopen(recon_path, "rb");
    bink2 b;
    unsigned char *ref;
    int cw = (TEST_WIDTH + 1) / 2;
    int ch = (TEST_HEIGHT + 1) / 2;
    size_t y_size = (size_t)TEST_WIDTH * TEST_HEIGHT;
    size_t c_size = (size_t)cw * ch;
    unsigned long f;
    int row;

    CHECK(recon != NULL);
    ref = (unsigned char *)malloc(y_size + c_size * 2);
    CHECK(ref != NULL);
    b = bink2_open_file(path, BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_enable_video(b, 1) == 0);
    for (f = 0; f < frames; f++) {
        int status = f == 0 ? bink2_first(b) : bink2_next(b);
        int plane;

        CHECK(status != BINK2_ERROR);
        for (plane = 0; plane < 3; plane++) {
            const unsigned char *decoded = bink2_get_plane(b, plane);
            int stride = bink2_get_stride(b, plane);
            int pw = plane == 0 ? TEST_WIDTH : cw;
            int ph = plane == 0 ? TEST_HEIGHT : ch;
            size_t size = plane == 0 ? y_size : c_size;

            CHECK(decoded != NULL);
            CHECK(fread(ref, 1, size, recon) == size);
            for (row = 0; row < ph; row++)
                CHECK(memcmp(decoded + (size_t)row * stride,
                             ref + (size_t)row * pw, (size_t)pw) == 0);
        }
    }
    bink2_close(b);
    free(ref);
    fclose(recon);
    return 0;
}

static double frame_psnr(const char *path, unsigned long target,
                         const SourceCtx *ctx)
{
    bink2 b = bink2_open_file(path, BINK2_MODE_DISK);
    const unsigned char *plane;
    int stride;
    unsigned long f;
    double mse = 0.0;
    int x, y;

    if (b == NULL)
        return 0.0;
    bink2_enable_video(b, 1);
    CHECK(bink2_first(b) != BINK2_ERROR);
    for (f = 1; f <= target; f++)
        CHECK(bink2_next(b) != BINK2_ERROR);
    plane = bink2_get_plane(b, 0);
    stride = bink2_get_stride(b, 0);
    CHECK(plane != NULL);
    for (y = 0; y < TEST_HEIGHT; y++)
        for (x = 0; x < TEST_WIDTH; x++) {
            int got = plane[(size_t)y * stride + x];
            int want = source_y(ctx, target, x, y);
            int diff = got - want;

            mse += (double)diff * diff;
        }
    bink2_close(b);
    mse /= (double)TEST_WIDTH * TEST_HEIGHT;
    if (mse <= 0.0)
        return 99.0;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

static int encode(const char *path, const char *recon, unsigned long frames,
                  int quality, unsigned long bitrate, unsigned key_interval,
                  int black_frame)
{
    SourceCtx ctx;
    bink2_enc_source src;
    bink2_enc_options options;
    char err[256];

    ctx.black_frame = black_frame;
    memset(&src, 0, sizeof(src));
    src.width = TEST_WIDTH;
    src.height = TEST_HEIGHT;
    src.frame_count = frames;
    src.fps_num = TEST_FPS;
    src.fps_den = 1;
    src.read_frame_yuv = read_yuv;
    src.user = &ctx;

    bink2_enc_options_default(&options);
    options.quality = quality;
    options.target_bitrate = bitrate;
    options.keyframe_interval = key_interval;
    options.motion_range = 4;
    options.recon_path = recon;
    return bink2_enc_encode(&src, &options, path, err, sizeof(err));
}

static int index_is_key(const char *path, unsigned long frame)
{
    FILE *file = fopen(path, "rb");
    unsigned char bytes[4];
    int key = 0;

    CHECK(file != NULL);
    CHECK(fseek(file, (long)(48 + 4 * frame), SEEK_SET) == 0);
    CHECK(fread(bytes, 1, 4, file) == 4);
    key = bytes[0] & 1;
    fclose(file);
    return key;
}

int main(void)
{
    SourceCtx ctx = { -1 };
    long low_size, high_size;
    double low_psnr, high_psnr;
    long rate_size;
    double target_size;
    unsigned long f;
    int i;

    /* Quality mode: a high setting must create a larger, better file. */
    CHECK(encode("test_rate_low.bk2", "test_rate_low.yuv", 120, 40, 0, 150,
                 -1) == BINK2_ENC_OK);
    CHECK(encode("test_rate_high.bk2", "test_rate_high.yuv", 120, 90, 0, 150,
                 -1) == BINK2_ENC_OK);
    low_size = file_size("test_rate_low.bk2");
    high_size = file_size("test_rate_high.bk2");
    CHECK(low_size > 0 && high_size > low_size);
    CHECK(compare_recon("test_rate_low.bk2", "test_rate_low.yuv", 120) == 0);
    CHECK(compare_recon("test_rate_high.bk2", "test_rate_high.yuv", 120) ==
          0);
    low_psnr = frame_psnr("test_rate_low.bk2", 60, &ctx);
    high_psnr = frame_psnr("test_rate_high.bk2", 60, &ctx);
    CHECK(high_psnr > low_psnr);

    /* Bitrate mode: within 25% of 100 KiB/s over the clip duration. */
    CHECK(encode("test_rate_bitrate.bk2", "test_rate_bitrate.yuv", 120, 80,
                 100 * 1024 * 8, 150, -1) == BINK2_ENC_OK);
    CHECK(compare_recon("test_rate_bitrate.bk2", "test_rate_bitrate.yuv",
                        120) == 0);
    rate_size = file_size("test_rate_bitrate.bk2");
    target_size = 100.0 * 1024.0 * 120.0 / TEST_FPS;
    CHECK(rate_size > target_size * 0.75 && rate_size < target_size * 1.25);

    /* Scene cut: the black frame becomes a keyframe. */
    CHECK(encode("test_rate_scene.bk2", "test_rate_scene.yuv", 61, 80, 0,
                 150, 60) == BINK2_ENC_OK);
    CHECK(compare_recon("test_rate_scene.bk2", "test_rate_scene.yuv", 61) ==
          0);
    CHECK(index_is_key("test_rate_scene.bk2", 0) == 1);
    CHECK(index_is_key("test_rate_scene.bk2", 59) == 0);
    CHECK(index_is_key("test_rate_scene.bk2", 60) == 1);

    /* Extremes must still decode. */
    {
        static const unsigned long bitrates[1] = { 1 };
        static const unsigned keyints[1] = { 150 };
        static const int qualities[1] = { 80 };

        CHECK(encode("test_rate_extreme.bk2", "test_rate_extreme.yuv", 5,
                     80, bitrates[0], keyints[0], -1) == BINK2_ENC_OK);
        CHECK(compare_recon("test_rate_extreme.bk2",
                            "test_rate_extreme.yuv", 5) == 0);
        CHECK(encode("test_rate_extreme.bk2", "test_rate_extreme.yuv", 5,
                     80, 0, 0, -1) == BINK2_ENC_OK);
        CHECK(compare_recon("test_rate_extreme.bk2",
                            "test_rate_extreme.yuv", 5) == 0);
        CHECK(encode("test_rate_extreme.bk2", "test_rate_extreme.yuv", 5,
                     qualities[0], 0, 150, -1) == BINK2_ENC_OK);
        CHECK(compare_recon("test_rate_extreme.bk2",
                            "test_rate_extreme.yuv", 5) == 0);
        for (i = 0; i < 3; i++) {
            bink2 b = bink2_open_file("test_rate_extreme.bk2",
                                      BINK2_MODE_DISK);

            CHECK(b != NULL);
            bink2_enable_video(b, 1);
            CHECK(bink2_first(b) != BINK2_ERROR);
            for (f = 1; f < 5; f++)
                CHECK(bink2_next(b) != BINK2_ERROR);
            bink2_close(b);
        }
    }

    remove("test_rate_low.bk2");
    remove("test_rate_low.yuv");
    remove("test_rate_high.bk2");
    remove("test_rate_high.yuv");
    remove("test_rate_bitrate.bk2");
    remove("test_rate_bitrate.yuv");
    remove("test_rate_scene.bk2");
    remove("test_rate_scene.yuv");
    remove("test_rate_extreme.bk2");
    remove("test_rate_extreme.yuv");
    return 0;
}
