#include "bink2.h"
#include "bink2_encoder.h"
#include "bink2_enc_internal.h"

#include "test_common.h"

#include <stdio.h>

typedef struct {
    int width;
    int height;
    int moving;
    int flash;
} Ctx;

static int read_yuv(void *user, unsigned long frame, unsigned char *y,
                    unsigned char *u, unsigned char *v)
{
    const Ctx *ctx = (const Ctx *)user;
    int cw = (ctx->width + 1) / 2;
    int ch = (ctx->height + 1) / 2;
    int x, row;

    for (row = 0; row < ctx->height; row++)
        for (x = 0; x < ctx->width; x++) {
            int value = (x + row + (int)frame * ctx->flash) & 0xFF;

            if (ctx->moving) {
                int bx = ((int)frame * 2) % ctx->width;

                if (x >= bx && x < bx + 16 && row >= 16 && row < 40)
                    value = 230;
            }
            y[row * ctx->width + x] = (unsigned char)value;
        }
    for (row = 0; row < ch; row++)
        for (x = 0; x < cw; x++) {
            u[row * cw + x] =
                (unsigned char)((128 + x + (int)frame) & 0xFF);
            v[row * cw + x] =
                (unsigned char)((128 - row + (int)frame) & 0xFF);
        }
    return 0;
}

static int compare_file(const char *path, const char *recon_path, int width,
                        int height, unsigned long frames, int check_strides)
{
    FILE *recon = fopen(recon_path, "rb");
    bink2 b;
    unsigned char *ref;
    int cw = (width + 1) / 2;
    int ch = (height + 1) / 2;
    size_t y_size = (size_t)width * height;
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
        int pw = width, ph = height;
        int p;

        CHECK(status != BINK2_ERROR);
        for (plane = 0; plane < 3; plane++) {
            const unsigned char *decoded;
            int stride;
            size_t size;

            if (plane == 0) {
                pw = width;
                ph = height;
                size = y_size;
            } else {
                pw = cw;
                ph = ch;
                size = c_size;
            }
            CHECK(fread(ref, 1, size, recon) == size);
            decoded = bink2_get_plane(b, plane);
            stride = bink2_get_stride(b, plane);
            CHECK(decoded != NULL);
            if (check_strides) {
                int expect = plane == 0 ? ((width + 31) & ~31)
                                        : ((((width + 1) / 2) + 15) & ~15);

                CHECK(stride == expect);
            }
            for (p = 0; p < ph; p++)
                CHECK(memcmp(decoded + (size_t)p * stride,
                             ref + (size_t)p * pw, (size_t)pw) == 0);
        }
    }
    bink2_close(b);
    free(ref);
    fclose(recon);
    return 0;
}

/* Block counts from the last encode_clip call. */
static Bink2EncStats clip_stats;

static int encode_clip(const char *path, const char *recon_path, Ctx *ctx,
                       unsigned long frames, unsigned slices, int four_mv,
                       unsigned key_interval, unsigned motion_range)
{
    bink2_enc_source src;
    bink2_enc_options options;
    FILE *file;
    int status;

    memset(&src, 0, sizeof(src));
    src.width = ctx->width;
    src.height = ctx->height;
    src.frame_count = frames;
    src.fps_num = 30;
    src.fps_den = 1;
    src.read_frame_yuv = read_yuv;
    src.user = ctx;
    bink2_enc_options_default(&options);
    options.slices = slices;
    options.four_mv = four_mv;
    options.keyframe_interval = key_interval;
    options.motion_range = motion_range;
    options.recon_path = recon_path;
    file = fopen(path, "wb+");
    if (file == NULL)
        return BINK2_ENC_ERR_ENCODE;
    status = bink2_enc_encode_intra(&src, &options, file, NULL, &clip_stats);
    if (fclose(file) != 0 && status == BINK2_ENC_OK)
        status = BINK2_ENC_ERR_ENCODE;
    return status;
}

int main(void)
{
    static const unsigned slice_counts[4] = { 2, 3, 4, 8 };
    Ctx ctx;
    static const int tiny[3][2] = { { 1, 1 }, { 1, 64 }, { 64, 1 } };
    char path[128];
    char recon[128];
    int s, four;
    unsigned long f;

    /* Every slice count with one- and four-vector motion. */
    ctx.width = 160;
    ctx.height = 120;
    ctx.moving = 1;
    ctx.flash = 1;
    for (s = 0; s < 4; s++) {
        for (four = 0; four <= 1; four++) {
            snprintf(path, sizeof(path), "test_opt_%u_%d.bk2",
                     slice_counts[s], four);
            snprintf(recon, sizeof(recon), "test_opt_%u_%d.yuv",
                     slice_counts[s], four);
            CHECK(encode_clip(path, recon, &ctx, 20, slice_counts[s], four,
                              150, 2) == BINK2_ENC_OK);
            CHECK(compare_file(path, recon, ctx.width, ctx.height, 20, 0) ==
                  0);
            remove(path);
            remove(recon);
        }
    }

    /* Odd dimensions: 720x460 with aligned decoder strides. */
    ctx.width = 720;
    ctx.height = 460;
    ctx.moving = 0;
    ctx.flash = 1;
    CHECK(encode_clip("test_opt_odd.bk2", "test_opt_odd.yuv", &ctx, 5, 4, 1,
                      150, 2) == BINK2_ENC_OK);
    CHECK(compare_file("test_opt_odd.bk2", "test_opt_odd.yuv", 720, 460, 5,
                       1) == 0);
    remove("test_opt_odd.bk2");
    remove("test_opt_odd.yuv");

    /* Odd width (not a multiple of 32) with the same exactness gate. */
    ctx.width = 161;
    ctx.height = 121;
    ctx.flash = 1;
    CHECK(encode_clip("test_opt_161.bk2", "test_opt_161.yuv", &ctx, 5, 4, 1,
                      150, 2) == BINK2_ENC_OK);
    CHECK(compare_file("test_opt_161.bk2", "test_opt_161.yuv", 161, 121, 5,
                       1) == 0);
    remove("test_opt_161.bk2");
    remove("test_opt_161.yuv");

    /* One-pixel dimensions: the chroma MC path must stay inside the planes. */
    for (s = 0; s < 3; s++) {
        ctx.width = tiny[s][0];
        ctx.height = tiny[s][1];
        ctx.moving = 0;
        ctx.flash = 8;
        snprintf(path, sizeof(path), "test_opt_tiny_%d.bk2", s);
        snprintf(recon, sizeof(recon), "test_opt_tiny_%d.yuv", s);
        CHECK(encode_clip(path, recon, &ctx, 3, 2, 1, 150, 2) ==
              BINK2_ENC_OK);
        CHECK(clip_stats.motion + clip_stats.residual > 0);
        CHECK(compare_file(path, recon, ctx.width, ctx.height, 3, 0) == 0);
        remove(path);
        remove(recon);
    }

    /* Keyframe interval marks exactly 0, 10 and 20. */
    ctx.width = 160;
    ctx.height = 120;
    ctx.moving = 0;
    ctx.flash = 1;
    CHECK(encode_clip("test_opt_keys.bk2", "test_opt_keys.yuv", &ctx, 21, 2,
                      0, 10, 2) == BINK2_ENC_OK);
    {
        FILE *file = fopen("test_opt_keys.bk2", "rb");

        CHECK(file != NULL);
        for (f = 0; f < 21; f++) {
            unsigned char bytes[4];
            int key;

            CHECK(fseek(file, (long)(48 + 4 * f), SEEK_SET) == 0);
            CHECK(fread(bytes, 1, 4, file) == 4);
            key = bytes[0] & 1;
            CHECK(key == (f == 0 || f == 10 || f == 20));
        }
        fclose(file);
    }
    CHECK(compare_file("test_opt_keys.bk2", "test_opt_keys.yuv", 160, 120,
                       21, 0) == 0);
    remove("test_opt_keys.bk2");
    remove("test_opt_keys.yuv");

    /* motion_range 0 must produce no motion vectors at all. */
    CHECK(encode_clip("test_opt_range.bk2", "test_opt_range.yuv", &ctx, 5, 2,
                      0, 150, 0) == BINK2_ENC_OK);
    {
        bink2 b = bink2_open_file("test_opt_range.bk2", BINK2_MODE_DISK);
        unsigned long mbw = 0, mbh = 0;
        const unsigned char *count = NULL;
        unsigned long i;

        CHECK(b != NULL);
        bink2_enable_video(b, 1);
        CHECK(bink2_first(b) != BINK2_ERROR);
        CHECK(bink2_next(b) != BINK2_ERROR);
        CHECK(bink2_get_motion(b, &mbw, &mbh, NULL, NULL, &count) == 1);
        for (i = 0; i < mbw * mbh; i++)
            CHECK(count[i] == 0);
        bink2_close(b);
    }
    CHECK(compare_file("test_opt_range.bk2", "test_opt_range.yuv", 160, 120,
                       5, 0) == 0);
    remove("test_opt_range.bk2");
    remove("test_opt_range.yuv");
    return 0;
}
