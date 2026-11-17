#include "bink2_dsp.h"
#include "bink2_video.h"
#include "test_common.h"

static int test_geometry(void)
{
    Bink2Video v;
    unsigned long mw = 0, mh = 0;
    const int16_t *mvx = NULL;
    const int16_t *mvy = NULL;
    const unsigned char *count = NULL;

    CHECK(bink2_video_init(&v, 720, 460, 0, 'g') == 0);
    CHECK(v.aligned_w == 736 && v.aligned_h == 480);
    CHECK(v.mb_w == 23 && v.mb_h == 15);
    CHECK(v.stride[0] == 736);
    CHECK(v.stride[1] == 368);
    CHECK(v.plane_w[0] == 720 && v.plane_h[0] == 460);
    CHECK(v.plane_w[1] == 360 && v.plane_h[1] == 230);
    CHECK(v.cur[0] != NULL && v.cur[1] != NULL && v.cur[2] != NULL);
    CHECK(v.cur[3] == NULL);
    CHECK(bink2_video_motion(&v, &mw, &mh, &mvx, &mvy, &count) == 1);
    CHECK(mw == 23 && mh == 15);
    CHECK(count[0] == 0);
    bink2_video_uninit(&v);

    CHECK(bink2_video_init(&v, 64, 64, BINK2_FLAG_ALPHA, 'j') == 0);
    CHECK(v.cur[3] != NULL);
    CHECK(v.stride[3] == 64);
    bink2_video_uninit(&v);
    return 0;
}

static int rgb_at(Bink2Video *v, int x, int y, int channel)
{
    const unsigned char *rgb = bink2_video_rgb(v);

    CHECK(rgb != NULL);
    return rgb[(y * (int)v->plane_w[0] + x) * 3 + channel];
}

static int fill_and_check(Bink2Video *v, int y, int u, int vv,
                          int er, int eg, int eb)
{
    int i;
    int size = v->stride[0] * v->aligned_h;

    memset(v->cur[0], y, (size_t)size);
    memset(v->cur[1], u, (size_t)v->stride[1] * (v->aligned_h / 2));
    memset(v->cur[2], vv, (size_t)v->stride[2] * (v->aligned_h / 2));
    v->rgb_valid = 0;
    CHECK(rgb_at(v, 0, 0, 0) == er);
    CHECK(rgb_at(v, 0, 0, 1) == eg);
    CHECK(rgb_at(v, 0, 0, 2) == eb);
    for (i = 0; i < 3; i++)
        CHECK(rgb_at(v, (int)v->plane_w[0] - 1,
                     (int)v->plane_h[0] - 1, i) ==
              rgb_at(v, 0, 0, i));
    return 0;
}

static int test_rgb(void)
{
    Bink2Video v;

    CHECK(bink2_video_init(&v, 16, 16, 0, 'j') == 0);
    CHECK(fill_and_check(&v, 128, 128, 128, 128, 128, 128) == 0);
    CHECK(fill_and_check(&v, 0, 128, 128, 0, 0, 0) == 0);
    CHECK(fill_and_check(&v, 255, 128, 128, 255, 255, 255) == 0);
    v.range = BINK2_RANGE_STUDIO;
    CHECK(fill_and_check(&v, 16, 128, 128, 0, 0, 0) == 0);
    CHECK(fill_and_check(&v, 235, 128, 128, 255, 255, 255) == 0);
    CHECK(fill_and_check(&v, 128, 128, 128, 130, 130, 130) == 0);
    bink2_video_uninit(&v);
    return 0;
}

static int test_idct_zero(void)
{
    float old_block[64];
    int32_t new_block[64];
    int i;

    memset(old_block, 0, sizeof(old_block));
    memset(new_block, 0, sizeof(new_block));
    bink2_idct_old(old_block);
    bink2_idct_new(new_block);
    for (i = 0; i < 64; i++) {
        CHECK(old_block[i] == 0.0f);
        CHECK(new_block[i] == 0);
    }
    return 0;
}

static int test_mc_identity(void)
{
    uint8_t luma[128 * 128];
    uint8_t chroma[64 * 64];
    uint8_t dst[256];
    int x, y;

    for (y = 0; y < 128; y++)
        for (x = 0; x < 128; x++)
            luma[y * 128 + x] = (uint8_t)(x + y);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            chroma[y * 64 + x] = (uint8_t)(x * 3 + y);

    memset(dst, 0, sizeof(dst));
    bink2_mc_luma(dst, 16, luma, 128, 16, 16, 128, 128, 0, 0);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            CHECK(dst[y * 16 + x] == luma[(y + 16) * 128 + x + 16]);

    memset(dst, 0, sizeof(dst));
    bink2_mc_chroma(dst, 8, chroma, 64, 8, 8, 64, 64, 0, 0);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            CHECK(dst[y * 8 + x] == chroma[(y + 8) * 64 + x + 8]);
    return 0;
}

static int test_mc_edge(void)
{
    Bink2Video v;
    unsigned char luma[736 * 480];
    unsigned char chroma[368 * 240];
    unsigned char dst[32 * 32];
    static const int motions[8][2] = {
        { -2000, -2000 }, { 2000, 2000 }, { -2000, 2000 }, { 2000, -2000 },
        { 1, 0 }, { 0, 1 }, { 3, 3 }, { -3, 1 }
    };
    int i;

    CHECK(bink2_video_init(&v, 720, 460, 0, 'g') == 0);
    memset(luma, 77, sizeof(luma));
    memset(chroma, 55, sizeof(chroma));
    for (i = 0; i < 8; i++) {
        memset(dst, 0, sizeof(dst));
        bink2_mc_luma(dst, 32, luma, 736, 0, 0, 720, 460,
                      motions[i][0], motions[i][1]);
        {
            int row, col;
            for (row = 0; row < 16; row++)
                for (col = 0; col < 16; col++)
                    CHECK(dst[row * 32 + col] == 77);
        }
        memset(dst, 0, sizeof(dst));
        bink2_mc_luma(dst, 32, luma, 736, 688, 432, 720, 460,
                      motions[i][0], motions[i][1]);
        {
            int row, col;
            for (row = 0; row < 16; row++)
                for (col = 0; col < 16; col++)
                    CHECK(dst[row * 32 + col] == 77);
        }
        memset(dst, 0, sizeof(dst));
        bink2_mc_chroma(dst, 16, chroma, 368, 0, 0, 360, 230,
                        motions[i][0], motions[i][1]);
        {
            int row, col;
            for (row = 0; row < 8; row++)
                for (col = 0; col < 8; col++)
                    CHECK(dst[row * 16 + col] == 55);
        }
        memset(dst, 0, sizeof(dst));
        bink2_mc_chroma(dst, 16, chroma, 368, 344, 216, 360, 230,
                        motions[i][0], motions[i][1]);
        {
            int row, col;
            for (row = 0; row < 8; row++)
                for (col = 0; col < 8; col++)
                    CHECK(dst[row * 16 + col] == 55);
        }
    }
    bink2_video_uninit(&v);
    return 0;
}

int main(void)
{
    CHECK(test_geometry() == 0);
    CHECK(test_rgb() == 0);
    CHECK(test_idct_zero() == 0);
    CHECK(test_mc_identity() == 0);
    CHECK(test_mc_edge() == 0);
    return 0;
}
