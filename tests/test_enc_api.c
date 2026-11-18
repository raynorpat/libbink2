#include "bink2_encoder.h"
#include "test_common.h"

#include <limits.h>

typedef struct {
    int width;
    int height;
} FrameCtx;

static int read_rgb_fill(void *user, unsigned long frame,
                         unsigned char *dst)
{
    const FrameCtx *ctx = (const FrameCtx *)user;
    size_t size = (size_t)ctx->width * (size_t)ctx->height * 3u;

    memset(dst, (int)(frame & 0xFF), size);
    return 0;
}

static int read_audio_fail(void *user, unsigned long start,
                           unsigned long count, short *dst)
{
    (void)user;
    (void)start;
    (void)count;
    (void)dst;
    return 1;
}

static int read_rgb(void *user, unsigned long frame, unsigned char *dst)
{
    (void)user;
    (void)frame;
    (void)dst;
    return 0;
}

static int read_yuv(void *user, unsigned long frame, unsigned char *y,
                    unsigned char *u, unsigned char *v)
{
    (void)user;
    (void)frame;
    (void)y;
    (void)u;
    (void)v;
    return 0;
}

static int read_audio(void *user, unsigned long start, unsigned long count,
                      short *dst)
{
    (void)user;
    (void)start;
    (void)count;
    (void)dst;
    return 0;
}

static void valid_source(bink2_enc_source *src)
{
    memset(src, 0, sizeof(*src));
    src->width = 64;
    src->height = 64;
    src->frame_count = 1;
    src->fps_num = 30;
    src->fps_den = 1;
    src->read_frame = read_rgb;
    src->read_frame_yuv = read_yuv;
}

static int test_defaults(void)
{
    bink2_enc_options o;

    bink2_enc_options_default(&o);
    CHECK(o.keyframe_interval == 150);
    CHECK(o.motion_range == 8);
    CHECK(o.slices == 2);
    CHECK(o.four_mv == 1);
    CHECK(o.quality == 80);
    CHECK(o.target_bitrate == 0);
    CHECK(o.audio_quality == 80);
    CHECK(o.verify == 0);
    CHECK(o.recon_path == NULL);
    CHECK(o.verbose == 0);
    return 0;
}

static int test_usage_rejects(void)
{
    bink2_enc_source src;
    bink2_enc_options o;
    char err[256];

    valid_source(&src);
    bink2_enc_options_default(&o);

    err[0] = '\0';
    CHECK(bink2_enc_encode(NULL, &o, "x.bk2", err, sizeof(err)) ==
          BINK2_ENC_ERR_USAGE);
    CHECK(err[0] != '\0');

    err[0] = '\0';
    CHECK(bink2_enc_encode(&src, &o, NULL, err, sizeof(err)) ==
          BINK2_ENC_ERR_USAGE);
    CHECK(err[0] != '\0');

#define REJECT(field, value) do { \
    valid_source(&src); \
    src.field = (value); \
    err[0] = '\0'; \
    CHECK(bink2_enc_encode(&src, &o, "x.bk2", err, sizeof(err)) == \
          BINK2_ENC_ERR_USAGE); \
    CHECK(err[0] != '\0'); \
} while (0)

    REJECT(width, 0);
    REJECT(width, 7681);
    REJECT(height, 0);
    REJECT(height, 4801);
    REJECT(frame_count, 0);
    REJECT(frame_count, 1000001UL);
    REJECT(fps_num, 0);
    REJECT(fps_den, 0);
#if ULONG_MAX > 0xFFFFFFFFUL
    /* The header stores both as 32-bit fields; larger values would be
     * truncated. */
    REJECT(fps_num, 0x100000000UL);
    REJECT(fps_den, 0x100000000UL);
#endif
    valid_source(&src);
    src.read_frame = NULL;
    src.read_frame_yuv = NULL;
    err[0] = '\0';
    CHECK(bink2_enc_encode(&src, &o, "x.bk2", err, sizeof(err)) ==
          BINK2_ENC_ERR_USAGE);
    CHECK(err[0] != '\0');

    valid_source(&src);
#define REJECT_OPT(field, value) do { \
    bink2_enc_options_default(&o); \
    o.field = (value); \
    err[0] = '\0'; \
    CHECK(bink2_enc_encode(&src, &o, "x.bk2", err, sizeof(err)) == \
          BINK2_ENC_ERR_USAGE); \
    CHECK(err[0] != '\0'); \
} while (0)

    REJECT_OPT(slices, 1);
    REJECT_OPT(slices, 5);
    REJECT_OPT(motion_range, 65);
    REJECT_OPT(quality, -1);
    REJECT_OPT(quality, 101);
    REJECT_OPT(audio_quality, -1);
    REJECT_OPT(audio_quality, 101);
    CHECK(strstr(err, "audio_quality") != NULL);

    valid_source(&src);
    src.audio.channels = 3;
    err[0] = '\0';
    CHECK(bink2_enc_encode(&src, &o, "x.bk2", err, sizeof(err)) ==
          BINK2_ENC_ERR_USAGE);
    CHECK(strstr(err, "audio") != NULL);

    valid_source(&src);
    src.audio.channels = 1;
    src.audio.rate = 0;
    src.audio.sample_count = 1000;
    src.read_audio = read_audio;
    err[0] = '\0';
    CHECK(bink2_enc_encode(&src, &o, "x.bk2", err, sizeof(err)) ==
          BINK2_ENC_ERR_USAGE);

    valid_source(&src);
    src.audio.channels = 1;
    src.audio.rate = 48000;
    src.audio.sample_count = 0;
    src.read_audio = read_audio;
    err[0] = '\0';
    CHECK(bink2_enc_encode(&src, &o, "x.bk2", err, sizeof(err)) ==
          BINK2_ENC_ERR_USAGE);

    valid_source(&src);
    src.audio.channels = 1;
    src.audio.rate = 48000;
    src.audio.sample_count = 1000;
    err[0] = '\0';
    CHECK(bink2_enc_encode(&src, &o, "x.bk2", err, sizeof(err)) ==
          BINK2_ENC_ERR_USAGE);

    return 0;
}

static int test_audio_callback_failure(void)
{
    bink2_enc_source src;
    bink2_enc_options o;
    FrameCtx ctx = { 32, 32 };
    char err[256] = "";

    memset(&src, 0, sizeof(src));
    src.width = 32;
    src.height = 32;
    src.frame_count = 2;
    src.fps_num = 25;
    src.fps_den = 1;
    src.read_frame = read_rgb_fill;
    src.user = &ctx;
    src.audio.rate = 48000;
    src.audio.channels = 1;
    src.audio.sample_count = 1000;
    src.read_audio = read_audio_fail;
    bink2_enc_options_default(&o);
    CHECK(bink2_enc_encode(&src, &o, "test_enc_api_audio.bk2", err,
                           sizeof(err)) == BINK2_ENC_ERR_INPUT);
    CHECK(err[0] != '\0');
    remove("test_enc_api_audio.bk2");
    return 0;
}

int main(void)
{
    CHECK(test_defaults() == 0);
    CHECK(test_usage_rejects() == 0);
    CHECK(test_audio_callback_failure() == 0);
    return 0;
}
