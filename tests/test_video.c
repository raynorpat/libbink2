#include "bink2.h"
#include "bink2_dsp.h"
#include "bink2_internal.h"
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

/* The frame converter is the row converter applied to every row. */
static int test_yuv_row_matches_frame(void)
{
    Bink2Video v;
    const unsigned long w = 333, h = 251;
    unsigned char *row = (unsigned char *)malloc(w * 3);
    int range, p;
    unsigned long x, y;

    CHECK(row != NULL);
    CHECK(bink2_video_init(&v, w, h, 0, 'j') == 0);
    for (p = 0; p < 3; p++) {
        unsigned char *plane = v.cur[p];
        int ph = p == 0 ? (int)v.aligned_h : (int)v.aligned_h / 2;

        for (y = 0; y < (unsigned long)ph; y++)
            for (x = 0; x < (unsigned long)v.stride[p]; x++)
                plane[y * v.stride[p] + x] =
                    (unsigned char)((x * 7 + y * 13 + p * 50) & 0xFF);
    }
    for (range = 0; range < 2; range++) {
        const unsigned char *rgb;

        v.range = range == 0 ? BINK2_RANGE_FULL : BINK2_RANGE_STUDIO;
        v.rgb_valid = 0;
        rgb = bink2_video_rgb(&v);
        CHECK(rgb != NULL);
        for (y = 0; y < h; y++) {
            bink2_yuv_row(row, v.cur[0] + y * v.stride[0],
                          v.cur[1] + (y / 2) * v.stride[1],
                          v.cur[2] + (y / 2) * v.stride[2], (int)w,
                          v.range == BINK2_RANGE_STUDIO);
            CHECK(memcmp(row, rgb + y * w * 3, w * 3) == 0);
            bink2_yuv_row_c(row, v.cur[0] + y * v.stride[0],
                            v.cur[1] + (y / 2) * v.stride[1],
                            v.cur[2] + (y / 2) * v.stride[2], (int)w,
                            v.range == BINK2_RANGE_STUDIO);
            CHECK(memcmp(row, rgb + y * w * 3, w * 3) == 0);
        }
    }
    bink2_video_uninit(&v);
    free(row);
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

typedef struct {
    unsigned char *data;
    size_t size;
} Packet;

static uint64_t video_hash(Bink2Video *v)
{
    uint64_t h = 0;
    int p;

    for (p = 0; p < 3; p++)
        h = h * 31 + fnv1a_rows(bink2_video_plane(v, p),
                                bink2_video_plane_width(v, p),
                                bink2_video_plane_height(v, p),
                                bink2_video_stride(v, p));
    return h;
}

/* Collect the fixture's keyframe video packets via the container code. */
static int load_keyframes(Packet out[2], unsigned long *w, unsigned long *h,
                          unsigned long *flags, int *version)
{
    bink2 b = bink2_open_file(BINK2_FIXTURE_DIR "/synthetic_kb2j.bk2",
                              BINK2_MODE_DISK);
    unsigned long i;
    int n = 0;

    if (b == NULL)
        return -1;
    bink2_enable_video(b, 1);
    bink2_info_video(b, w, h, flags);
    *version = b->version;
    for (i = 0; i < b->frame_count && n < 2; i++) {
        if (!b->frame_keyframes[i])
            continue;
        if (bink2_seek_keyframe(b, i) == BINK2_ERROR) {
            bink2_close(b);
            return -1;
        }
        out[n].size = b->video_size;
        out[n].data = (unsigned char *)malloc(out[n].size);
        if (out[n].data == NULL) {
            bink2_close(b);
            return -1;
        }
        memcpy(out[n].data, b->video_data, out[n].size);
        n++;
    }
    bink2_close(b);
    return n == 2 ? 0 : -1;
}

/* One decoder reused across packets of different sizes must decode each
 * exactly as a fresh decoder does: a stale tail from a larger packet must
 * not leak into a smaller one. */
static int test_packet_growth(void)
{
    Packet k[2];
    unsigned long w, h, flags;
    int version;
    int order[4];
    Bink2Video reused;
    int i;

    CHECK(load_keyframes(k, &w, &h, &flags, &version) == 0);
    CHECK(k[0].size != k[1].size);
    /* larger, smaller, larger, smaller */
    order[0] = k[0].size > k[1].size ? 0 : 1;
    order[1] = 1 - order[0];
    order[2] = order[0];
    order[3] = order[1];
    CHECK(bink2_video_init(&reused, w, h, flags, version) == 0);
    for (i = 0; i < 4; i++) {
        Bink2Video fresh;
        const Packet *p = &k[order[i]];

        CHECK(bink2_video_init(&fresh, w, h, flags, version) == 0);
        CHECK(bink2_video_decode(&fresh, p->data, p->size, 1) == 0);
        CHECK(bink2_video_decode(&reused, p->data, p->size, 1) == 0);
        CHECK(video_hash(&reused) == video_hash(&fresh));
        bink2_video_uninit(&fresh);
    }
    /* Truncated copies of the smaller packet read the implicit zero tail;
     * after the larger packet the reused buffer holds its bytes there. */
    for (i = 1; i <= 8; i++) {
        Bink2Video fresh;
        const Packet *big = &k[order[0]];
        const Packet *small = &k[order[1]];
        int rf, rr;

        CHECK(bink2_video_init(&fresh, w, h, flags, version) == 0);
        CHECK(bink2_video_decode(&reused, big->data, big->size, 1) == 0);
        rf = bink2_video_decode(&fresh, small->data, small->size - i, 1);
        rr = bink2_video_decode(&reused, small->data, small->size - i, 1);
        CHECK(rf == rr);
        if (rf == 0)
            CHECK(video_hash(&reused) == video_hash(&fresh));
        bink2_video_uninit(&fresh);
    }
    bink2_video_uninit(&reused);
    free(k[0].data);
    free(k[1].data);
    return 0;
}

int main(void)
{
    CHECK(test_geometry() == 0);
    CHECK(test_rgb() == 0);
    CHECK(test_yuv_row_matches_frame() == 0);
    CHECK(test_idct_zero() == 0);
    CHECK(test_mc_identity() == 0);
    CHECK(test_mc_edge() == 0);
    CHECK(test_packet_growth() == 0);
    return 0;
}
