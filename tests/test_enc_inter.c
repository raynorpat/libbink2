#include "bink2.h"
#include "bink2_encoder.h"
#include "bink2_enc_internal.h"
#include "bink2_enc_motion.h"
#include "bink2_enc_video.h"
#include "bink2_bits.h"
#include "bink2_filter.h"

#include "test_common.h"

#include <stdio.h>
#include <string.h>

#define TEST_WIDTH 320
#define TEST_HEIGHT 240
#define TEST_FRAMES 4

static unsigned char source_y(int x, int y, unsigned long frame)
{
    int base = (x + y) & 0xFF;

    /* A flat band gives the intra frame DC-only blocks. */
    if (y >= 192)
        return 100;
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

static int check_plane_quiet(bink2 b, int plane, const unsigned char *ref,
                             int width, int height)
{
    const unsigned char *p;
    int stride, row;

    p = bink2_get_plane(b, plane);
    stride = bink2_get_stride(b, plane);
    if (p == NULL || stride <= 0)
        return 1;
    for (row = 0; row < height; row++)
        if (memcmp(p + (size_t)row * stride, ref + (size_t)row * width,
                   (size_t)width) != 0)
            return 1;
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
    v.slice[0].mb_pos = 1;
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

#define STATE_W 96
#define STATE_H 64
#define STATE_FRAMES 3

/* Frame 1 moves frame 0 right by 8 pixels; frame 2 repeats frame 1, so its
 * macroblocks are skips where frame 1 had motion vectors. */
static void state_source(Bink2Frame *frame, int f)
{
    int shift = f == 0 ? 0 : 8;
    int x, y;

    for (y = 0; y < STATE_H; y++)
        for (x = 0; x < STATE_W; x++) {
            int sx = x - shift < 0 ? 0 : x - shift;

            frame->y[y * frame->stride[0] + x] =
                (unsigned char)((sx * 5 + y * 3 + ((sx ^ y) & 7) * 9) & 0xFF);
        }
    for (y = 0; y < (STATE_H + 1) / 2; y++)
        for (x = 0; x < (STATE_W + 1) / 2; x++) {
            frame->u[y * frame->stride[1] + x] = (unsigned char)(128 + x);
            frame->v[y * frame->stride[2] + x] = (unsigned char)(128 - y);
        }
}

static int same_state(const Bink2Video *enc, const Bink2Video *dec)
{
    int p, r;

    CHECK(enc->frame_flags == dec->frame_flags);
    CHECK(memcmp(enc->row_flags, dec->row_flags,
                 (size_t)dec->row_size + 1) == 0);
    CHECK(memcmp(enc->col_flags, dec->col_flags,
                 (size_t)dec->col_size + 1) == 0);
    CHECK(memcmp(enc->mv[0], dec->mv[0],
                 dec->mv_capacity * sizeof(int16_t)) == 0);
    CHECK(memcmp(enc->mv[1], dec->mv[1],
                 dec->mv_capacity * sizeof(int16_t)) == 0);
    CHECK(memcmp(enc->mv_count, dec->mv_count, dec->mv_capacity) == 0);
    for (p = 0; p < 3; p++) {
        int rows = dec->mb_h * (p == 0 ? 4 : 2);
        int h = p == 0 ? (int)dec->aligned_h : (int)dec->aligned_h / 2;

        CHECK(memcmp(enc->blk_info[p], dec->blk_info[p],
                     (size_t)dec->blk_stride[p] * rows *
                         sizeof(uint16_t)) == 0);
        for (r = 0; r < h; r++)
            CHECK(memcmp(enc->cur[p] + (size_t)r * enc->stride[p],
                         dec->cur[p] + (size_t)r * dec->stride[p],
                         (size_t)dec->stride[p]) == 0);
    }
    return 0;
}

static int same_planes(const Bink2Video *a, const Bink2Video *b)
{
    int p;

    for (p = 0; p < 3; p++) {
        unsigned long h = p == 0 ? a->aligned_h : a->aligned_h / 2;

        CHECK(memcmp(a->cur[p], b->cur[p], (size_t)a->stride[p] * h) == 0);
    }
    return 0;
}

/* After each frame, the state the filter stages read (frame flags, edge
 * flags, motion vectors, info words) and the picture must be the
 * decoder's for the same payload. Frame 2's skips catch vectors left over
 * from frame 1.
 *
 * The frame flags the encoder writes also leave deblocking and the residual
 * edge filter nothing to do: R selects no direction, and a second decoder
 * with the filters off, followed by DC reconstruction alone, gives the same
 * picture as the filtered decode (DB would run after DC). */
static int test_state_matches_decoder(void)
{
    Bink2Video enc, dec, dc_only;
    Bink2Frame frame;
    Bink2EncStats stats;
    int heights[8];
    int slices, f, moved = 0;

    CHECK(bink2_video_init(&enc, STATE_W, STATE_H, 0x10, 'j') == 0);
    CHECK(bink2_video_init(&dec, STATE_W, STATE_H, 0x10, 'j') == 0);
    CHECK(bink2_video_init(&dc_only, STATE_W, STATE_H, 0x10, 'j') == 0);
    dc_only.filters_on = 0;
    slices = bink2_video_slice_heights('j', 0x10, STATE_H, heights);
    CHECK(slices >= 1);
    CHECK(bink2_frame_alloc(&frame, STATE_W, STATE_H) == 0);
    for (f = 0; f < STATE_FRAMES; f++) {
        Bink2BitWriter bw;
        size_t i;

        state_source(&frame, f);
        memset(&stats, 0, sizeof(stats));
        bink2_enc_reset_frame(&enc);
        bink2_bw_init(&bw);
        CHECK(bink2_enc_encode_frame(&bw, &frame, &enc, 12, slices, heights,
                                     f == 0, 8, 1, &stats) == 0);
        CHECK(bink2_video_decode(&dec, bink2_bw_data(&bw),
                                 bink2_bw_bytes(&bw), f == 0) == 0);
        CHECK(bink2_video_decode(&dc_only, bink2_bw_data(&bw),
                                 bink2_bw_bytes(&bw), f == 0) == 0);
        bink2_bw_free(&bw);
        CHECK(same_state(&enc, &dec) == 0);
        CHECK(bink2_filter_residual_dirs(&dec) == 0);
        bink2_filter_dcrecon(&dc_only);
        CHECK(same_planes(&dc_only, &dec) == 0);
        if (f == 1)
            for (i = 0; i < dec.mv_capacity; i++)
                if (dec.mv[0][i] != 0 || dec.mv[1][i] != 0)
                    moved = 1;
        if (f == 2)
            CHECK(stats.skip > 0);
    }
    CHECK(moved);
    bink2_frame_free(&frame);
    bink2_video_uninit(&enc);
    bink2_video_uninit(&dec);
    bink2_video_uninit(&dc_only);
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
    Bink2EncStats stats;
    FILE *file;
    bink2 b;

    CHECK(test_mv_inversion() == 0);
    CHECK(test_state_matches_decoder() == 0);

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

    /* The internal entry point reports the block counts. */
    file = fopen("test_enc_inter.bk2", "wb+");
    CHECK(file != NULL);
    CHECK(bink2_enc_encode_intra(&src, &options, file, NULL, &stats) ==
          BINK2_ENC_OK);
    CHECK(fclose(file) == 0);
    CHECK(stats.skip > 0);
    CHECK(stats.motion > 0);
    CHECK(stats.residual > 0);

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

    /* Decoding with the filters off must differ from recon exactly when
     * KB2j is covered by a stage that changes this clip. DC reconstruction
     * has no opt-out, so the stage list includes it and the clip has DC-only
     * blocks in the flat band. */
    {
        int covered = bink2_filter_covers('j', BINK2_STAGE_DEBLOCK) ||
                      bink2_filter_covers('j', BINK2_STAGE_DCRECON);
        int differs = 0;

        b = bink2_open_file("test_enc_inter.bk2", BINK2_MODE_DISK);
        CHECK(b != NULL);
        CHECK(bink2_enable_video(b, 1) == 0);
        CHECK(bink2_enable_filters(b, 0) == 0);
        file = fopen("test_enc_inter_recon.yuv", "rb");
        CHECK(file != NULL);
        for (f = 0; f < TEST_FRAMES; f++) {
            int status = f == 0 ? bink2_first(b) : bink2_next(b);

            CHECK(status != BINK2_ERROR);
            CHECK(fread(ref, 1, y_size, file) == y_size);
            if (check_plane_quiet(b, 0, ref, TEST_WIDTH, TEST_HEIGHT))
                differs = 1;
            CHECK(fread(ref, 1, c_size * 2, file) == c_size * 2);
        }
        fclose(file);
        bink2_close(b);
        CHECK(covered == 1);
        CHECK(differs == covered);
    }
    free(ref);

    remove("test_enc_inter.bk2");
    remove("test_enc_inter_recon.yuv");
    return 0;
}
