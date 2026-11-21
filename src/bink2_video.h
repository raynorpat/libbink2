#ifndef LIBBINK2_VIDEO_H
#define LIBBINK2_VIDEO_H

#include "bink2.h"
#include "bink2_bits.h"
#include "bink2_thread.h"

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

#define BINK2_MAX_SLICES 8

#define BINK2_INFO_DC_MASK     0x1FFFu
#define BINK2_INFO_CLASS_SHIFT 13
#define BINK2_INFO_INTRA       0x8000u
#define BINK2_INFO_DC_ONLY     0xE000u

/* Prediction state of one slice. Slices reset it at their first row and
 * share nothing else, so each can be decoded on its own thread. */
typedef struct Bink2Slice {
    Bink2OldDc *dc_cur, *dc_prev;   /* mb_w + 1 entries */
    int32_t *mv_cur, *mv_prev;      /* (mb_w + 1) * 8 entries */
    Bink2Quant *q_cur, *q_prev;     /* mb_w + 1 entries */
    int mb_pos;
    int comp;
} Bink2Slice;

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
    int filters_on;     /* latched from bink2_enable_filters at keyframes */

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

    unsigned char *packet;      /* frame packet + 8 zero bytes, reused */
    size_t packet_capacity;

    unsigned char *rgb;
    size_t rgb_size;
    int rgb_valid;

    uint32_t frame_flags;
    Bink2Slice slice[BINK2_MAX_SLICES];

    /* Per-frame block state for the whole-frame filter stages, written by
     * the slice decoders. blk_info[p] has one word per 8x8 block of plane
     * p (indexed like cur[]; NULL for an absent alpha plane), blk_stride[p]
     * words per block row and aligned_h / 8 (Y, alpha) or aligned_h / 16
     * (chroma) rows. Every decoded macroblock rewrites the words of all its
     * blocks in all planes:
     *   bits 0-12   DC value (BINK2_INFO_DC_MASK): the integer v for which
     *               coefficient 0 is v * 8 + 32 in KB2g+; in KB2f the float
     *               DC, rounded with +0.49999991f for Y and alpha and
     *               truncated for chroma, then masked to 12 bits
     *   bits 13-14  class (BINK2_INFO_CLASS_SHIFT) from the number of AC
     *               symbols decoded for the block, see bink2_video_ac_class
     *   bit 15      intra (BINK2_INFO_INTRA)
     * Blocks of skip, motion and motion+residual macroblocks get 0, so the
     * intra bit is also the macroblock's. An intra block with no AC
     * symbols (word >= BINK2_INFO_DC_ONLY) is DC-only.
     *
     * row_flags[i] (i = 0..row_size) is 1 when the frame's 0x10000 row
     * array disables the horizontal edge at luma row 8i; col_flags[i]
     * (i = 0..col_size) likewise for the vertical edge at luma column 8i.
     * Entry 0, the frame border, is always 0, and both arrays are all 0
     * when the frame carries none. The stage's motion vectors are mv[]
     * above: four per macroblock, 0 for skip and intra macroblocks. */
    uint16_t *blk_info[4];
    int blk_stride[4];
    unsigned char *row_flags;
    unsigned char *col_flags;
    int row_size;       /* 16-aligned height / 8 - 1 */
    int col_size;       /* 32-aligned width / 8 - 1 */

    Bink2Pool *pool;    /* started on the first multi-slice frame */
    int pool_workers;   /* -1 after the pool failed to start */
    int thread_limit;   /* 0 = BINK2_THREADS or the CPU count */
} Bink2Video;

int bink2_video_init(Bink2Video *v, unsigned long width, unsigned long height,
                     unsigned long flags, int version);
void bink2_video_uninit(Bink2Video *v);
void bink2_video_reset(Bink2Video *v);
/* 0 = automatic, 1 = serial, n > 1 = up to n threads per frame. */
void bink2_video_set_threads(Bink2Video *v, int n);
void bink2_video_reset_slice(const Bink2Video *v, Bink2Slice *s);
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

int bink2_video_decode_slice_old(Bink2Video *v, Bink2Slice *s, Bink2Bits *bits,
                                 int is_keyframe, int y0, int y1);
int bink2_video_decode_slice_new(Bink2Video *v, Bink2Slice *s, Bink2Bits *bits,
                                 int is_keyframe, int y0, int y1);
void bink2_video_predict_dc(Bink2Slice *s, int is_luma, float mindc,
                            float maxdc, int flags, float tdc[16]);
int bink2_video_slice_heights(int version, unsigned long flags,
                              unsigned long height, int heights[8]);
void bink2_video_mc_luma(Bink2Video *v, const Bink2Slice *s, int x, int y);
void bink2_video_mc_chroma(Bink2Video *v, const Bink2Slice *s, int x, int y);
void bink2_video_copy_mb(Bink2Video *v, int x, int y);
void bink2_video_store_motion(Bink2Video *v, const Bink2Slice *s,
                              int x, int y, int nb);

/* Class 0..3 of a block with `symbols` decoded AC coefficient symbols:
 * 3 for none, 2 for 1-3, 1 for 4-7, 0 for 8 or more. */
unsigned bink2_video_ac_class(int symbols);
/* The KB2f intra info word of a block with float DC `dc`. round = 1 (Y and
 * alpha) adds RAD's single-precision 0.49999991f before truncating; chroma
 * passes 0 and truncates. */
unsigned bink2_video_old_intra_word(float dc, int symbols, int round);

/* The info word of an intra KB2g+ block: dc is an integer in 0..2047. */
unsigned bink2_new_intra_word(float dc, int symbols);
/* Stores the info word of block (bx, by) of the macroblock at luma (x, y)
 * in plane p; bx and by count 8x8 blocks inside the macroblock. */
void bink2_video_set_info(Bink2Video *v, int p, int x, int y, int bx, int by,
                          unsigned word);
/* Sets every info word of the macroblock at luma (x, y) to 0. */
void bink2_video_clear_info(Bink2Video *v, int x, int y);
/* Reads one 0x10000 edge array of `size` flags into flags[1..size].
 * Returns -1 when the stream ends first or a run is longer than the flags
 * left. */
int bink2_video_read_edge_flags(Bink2Bits *bits, unsigned char *flags,
                                int size);
/* Runs the post-reconstruction stages libbink2 covers for this revision on
 * the decoded frame in cur, when filters_on is set. */
void bink2_video_apply_filters(Bink2Video *v);

#endif
