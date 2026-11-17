#ifndef LIBBINK2_VIDEO_H
#define LIBBINK2_VIDEO_H

#include "bink2.h"
#include "bink2_bits.h"

#include <stddef.h>
#include <stdint.h>

#define BINK2_RANGE_FULL   0
#define BINK2_RANGE_STUDIO 1

typedef struct {
    float dc[4][16];
    int block_type;
} Bink2OldDc;

typedef struct {
    int8_t intra_q;
    int8_t inter_q;
} Bink2Quant;

extern const uint8_t bink2_luma_repos[16];
extern const uint8_t bink2_next_skips[16];

typedef struct Bink2Video {
    unsigned long width;
    unsigned long height;
    unsigned long aligned_w;
    unsigned long aligned_h;
    int version;
    unsigned long flags;
    int has_alpha;
    int has_prev;
    int range;

    unsigned char *cur[4];
    unsigned char *ref[4];
    int stride[4];
    int plane_w[4];
    int plane_h[4];

    int mb_w;
    int mb_h;

    int16_t *mv[2];
    unsigned char *mv_count;
    size_t mv_capacity;

    unsigned char *rgb;
    size_t rgb_size;
    int rgb_valid;

    Bink2OldDc *old_dc_cur;
    Bink2OldDc *old_dc_prev;
    int32_t *old_mv_cur;
    int32_t *old_mv_prev;
    int old_mb_pos;
    int old_comp;

    uint32_t frame_flags;
    Bink2Quant *new_q_cur;
    Bink2Quant *new_q_prev;
} Bink2Video;

int bink2_video_init(Bink2Video *v, unsigned long width, unsigned long height,
                     unsigned long flags, int version);
void bink2_video_uninit(Bink2Video *v);
void bink2_video_reset(Bink2Video *v);
int bink2_video_decode(Bink2Video *v, const unsigned char *data, size_t size,
                       int is_keyframe);
const unsigned char *bink2_video_plane(Bink2Video *v, int plane);
int bink2_video_stride(const Bink2Video *v, int plane);
int bink2_video_plane_width(const Bink2Video *v, int plane);
int bink2_video_plane_height(const Bink2Video *v, int plane);
const unsigned char *bink2_video_rgb(Bink2Video *v);
int bink2_video_motion(Bink2Video *v, unsigned long *mb_w, unsigned long *mb_h,
                       const int16_t **mv_x, const int16_t **mv_y,
                       const unsigned char **mv_count);

int bink2_video_decode_slice_old(Bink2Video *v, Bink2Bits *bits,
                                 int is_keyframe, int y0, int y1);
int bink2_video_decode_slice_new(Bink2Video *v, Bink2Bits *bits,
                                 int is_keyframe, int y0, int y1);
void bink2_video_predict_dc(Bink2Video *v, int is_luma, float mindc,
                            float maxdc, int flags, float tdc[16]);
int bink2_video_slice_heights(int version, unsigned long flags,
                              unsigned long height, int heights[8]);
void bink2_video_mc_luma(Bink2Video *v, int x, int y);
void bink2_video_mc_chroma(Bink2Video *v, int x, int y);
void bink2_video_copy_mb(Bink2Video *v, int x, int y);
void bink2_video_store_motion(Bink2Video *v, int x, int y, int nb);

#endif
