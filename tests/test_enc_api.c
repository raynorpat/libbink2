#include "bink2_encoder.h"
#include "test_common.h"

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

    return 0;
}

int main(void)
{
    CHECK(test_defaults() == 0);
    CHECK(test_usage_rejects() == 0);
    return 0;
}
