#include "bink2_video.h"
#include "bink2_dsp.h"

#include <stdlib.h>
#include <string.h>

const uint8_t bink2_luma_repos[16] = {
    0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15
};
const uint8_t bink2_next_skips[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0
};

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] |
           ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static int bits_readz(Bink2Bits *bits, int count)
{
    if (count <= 0)
        return 0;
    return bink2_bits_read(bits, count);
}

static int skip_block_flags(Bink2Bits *bits, int offset, int size)
{
    int flags_left, mode = 0, temp, bits_count, nb_coded;

    if (size <= 0)
        return 0;
    temp = bink2_bits_read1(bits);
    if (temp < 0)
        return -1;
    if (temp == 0) {
        int j;

        for (j = 0; j < (size >> 3); j++) {
            if (bink2_bits_read(bits, 8) < 0)
                return -1;
        }
        if ((size & 7) && bink2_bits_read(bits, size & 7) < 0)
            return -1;
        return 0;
    }

    flags_left = size;
    while (flags_left > 0) {
        temp = bink2_bits_read1(bits);
        if (temp < 0)
            return -1;
        if (temp == 0) {
            if (mode != 3) {
                if (bink2_bits_read1(bits) < 0)
                    return -1;
            }
            mode = 2;
            if (flags_left < 5) {
                if (bits_readz(bits, flags_left - 1) < 0)
                    return -1;
                offset += flags_left;
                flags_left = 0;
            } else {
                if (bink2_bits_read(bits, 4) < 0)
                    return -1;
                offset += 5;
                flags_left -= 5;
            }
            if (offset >= 8)
                offset -= 8;
        } else {
            bits_count = flags_left < 4 ? 2 : flags_left < 16 ? 4 : 5;
            nb_coded = bits_count + 1;
            if (mode != 3) {
                nb_coded++;
                if (bink2_bits_read1(bits) < 0)
                    return -1;
            }
            if (nb_coded > flags_left)
                nb_coded = flags_left;
            flags_left -= nb_coded;
            if (flags_left > 0) {
                temp = bink2_bits_read(bits, bits_count);
                if (temp < 0)
                    return -1;
                flags_left -= temp;
                nb_coded += temp;
                mode = temp == (1 << bits_count) - 1 ? 1 : 3;
            }
            while (nb_coded > 8)
                nb_coded -= 8;
            if (nb_coded > 0) {
                offset += nb_coded;
                if (offset >= 8)
                    offset -= 8;
            }
        }
    }
    return 0;
}

int bink2_video_init(Bink2Video *v, unsigned long width, unsigned long height,
                     unsigned long flags, int version)
{
    size_t size;
    int i;

    if (v == NULL || width == 0 || height == 0)
        return -1;

    memset(v, 0, sizeof(*v));
    v->width = width;
    v->height = height;
    v->aligned_w = (width + 31) & ~31UL;
    v->aligned_h = (height + 31) & ~31UL;
    v->version = version;
    v->flags = flags;
    v->has_alpha = (flags & BINK2_FLAG_ALPHA) ? 1 : 0;
    v->range = BINK2_RANGE_FULL;

    v->stride[0] = (int)v->aligned_w;
    v->stride[1] = (int)((((width + 1) / 2) + 15) & ~15UL);
    v->stride[2] = v->stride[1];
    v->stride[3] = v->has_alpha ? (int)v->aligned_w : 0;

    v->plane_w[0] = (int)width;
    v->plane_h[0] = (int)height;
    v->plane_w[1] = (int)((width + 1) / 2);
    v->plane_h[1] = (int)((height + 1) / 2);
    v->plane_w[2] = v->plane_w[1];
    v->plane_h[2] = v->plane_h[1];
    v->plane_w[3] = (int)width;
    v->plane_h[3] = (int)height;

    for (i = 0; i < 4; i++) {
        unsigned long h = (i == 1 || i == 2) ? v->aligned_h / 2
                                             : v->aligned_h;

        if (i == 3 && !v->has_alpha)
            continue;
        size = (size_t)v->stride[i] * h;
        v->cur[i] = (unsigned char *)calloc(1, size);
        v->ref[i] = (unsigned char *)calloc(1, size);
        if (v->cur[i] == NULL || v->ref[i] == NULL) {
            bink2_video_uninit(v);
            return -1;
        }
    }

    v->mb_w = (int)(v->aligned_w / 32);
    v->mb_h = (int)(v->aligned_h / 32);
    v->mv_capacity = (size_t)v->mb_w * (size_t)v->mb_h * 4;
    v->mv[0] = (int16_t *)calloc(v->mv_capacity, sizeof(int16_t));
    v->mv[1] = (int16_t *)calloc(v->mv_capacity, sizeof(int16_t));
    v->mv_count = (unsigned char *)calloc(v->mv_capacity, 1);
    for (i = 0; i < BINK2_MAX_SLICES; i++) {
        Bink2Slice *sl = &v->slice[i];

        sl->dc_cur = (Bink2OldDc *)calloc((size_t)v->mb_w + 1,
                                          sizeof(Bink2OldDc));
        sl->dc_prev = (Bink2OldDc *)calloc((size_t)v->mb_w + 1,
                                           sizeof(Bink2OldDc));
        sl->mv_cur = (int32_t *)calloc(((size_t)v->mb_w + 1) * 8,
                                       sizeof(int32_t));
        sl->mv_prev = (int32_t *)calloc(((size_t)v->mb_w + 1) * 8,
                                        sizeof(int32_t));
        sl->q_cur = (Bink2Quant *)calloc((size_t)v->mb_w + 1,
                                         sizeof(Bink2Quant));
        sl->q_prev = (Bink2Quant *)calloc((size_t)v->mb_w + 1,
                                          sizeof(Bink2Quant));
        if (sl->dc_cur == NULL || sl->dc_prev == NULL ||
            sl->mv_cur == NULL || sl->mv_prev == NULL ||
            sl->q_cur == NULL || sl->q_prev == NULL) {
            bink2_video_uninit(v);
            return -1;
        }
    }
    v->rgb_size = (size_t)width * height * 3;
    v->rgb = (unsigned char *)calloc(1, v->rgb_size);
    if (v->mv[0] == NULL || v->mv[1] == NULL || v->mv_count == NULL ||
        v->rgb == NULL) {
        bink2_video_uninit(v);
        return -1;
    }
    return 0;
}

void bink2_video_uninit(Bink2Video *v)
{
    int i;

    if (v == NULL)
        return;
    for (i = 0; i < 4; i++) {
        free(v->cur[i]);
        free(v->ref[i]);
    }
    free(v->mv[0]);
    free(v->mv[1]);
    free(v->mv_count);
    for (i = 0; i < BINK2_MAX_SLICES; i++) {
        free(v->slice[i].dc_cur);
        free(v->slice[i].dc_prev);
        free(v->slice[i].mv_cur);
        free(v->slice[i].mv_prev);
        free(v->slice[i].q_cur);
        free(v->slice[i].q_prev);
    }
    free(v->rgb);
    free(v->packet);
    bink2_pool_destroy(v->pool);
    memset(v, 0, sizeof(*v));
}

void bink2_video_reset_slice(const Bink2Video *v, Bink2Slice *s)
{
    size_t n = (size_t)v->mb_w + 1;

    memset(s->dc_cur, 0, n * sizeof(Bink2OldDc));
    memset(s->dc_prev, 0, n * sizeof(Bink2OldDc));
    memset(s->mv_cur, 0, n * 8 * sizeof(int32_t));
    memset(s->mv_prev, 0, n * 8 * sizeof(int32_t));
    memset(s->q_cur, 0, n * sizeof(Bink2Quant));
    memset(s->q_prev, 0, n * sizeof(Bink2Quant));
    s->mb_pos = 0;
    s->comp = 0;
}

void bink2_video_reset(Bink2Video *v)
{
    int i;

    if (v == NULL)
        return;
    for (i = 0; i < 4; i++) {
        unsigned long h = (i == 1 || i == 2) ? v->aligned_h / 2
                                             : v->aligned_h;

        if (v->cur[i] != NULL)
            memset(v->cur[i], 0, (size_t)v->stride[i] * h);
        if (v->ref[i] != NULL)
            memset(v->ref[i], 0, (size_t)v->stride[i] * h);
    }
    memset(v->mv[0], 0, v->mv_capacity * sizeof(int16_t));
    memset(v->mv[1], 0, v->mv_capacity * sizeof(int16_t));
    memset(v->mv_count, 0, v->mv_capacity);
    for (i = 0; i < BINK2_MAX_SLICES; i++)
        bink2_video_reset_slice(v, &v->slice[i]);
    v->has_prev = 0;
    v->rgb_valid = 0;
}

int bink2_video_slice_heights(int version, unsigned long flags,
                              unsigned long height, int heights[8])
{
    unsigned long aligned_h = (height + 31) & ~31UL;

    if (version <= 'f') {
        heights[0] = (int)(((height / 2 + 16) & ~31UL));
        heights[1] = (int)aligned_h;
        return 2;
    }
    if (version == 'g') {
        if (aligned_h < 128) {
            heights[0] = (int)aligned_h;
            return 1;
        }
        heights[0] = (int)(((height / 2 + 16) & ~31UL));
        heights[1] = (int)aligned_h;
        return 2;
    }
    {
        static const int slice_counts[4] = { 2, 3, 4, 8 };
        int n = slice_counts[flags & 3];
        int start = 0;
        int end = (int)aligned_h + 32 * n - 1;
        int i;

        for (i = 0; i < n - 1; i++) {
            start += ((end - start) / (n - i)) & ~31;
            end -= 32;
            heights[i] = start;
        }
        heights[n - 1] = (int)aligned_h;
        return n;
    }
}

typedef struct {
    Bink2Video *v;
    const unsigned char *data;
    size_t padded_size;
    int is_keyframe;
    int y0[BINK2_MAX_SLICES];
    int y1[BINK2_MAX_SLICES];
    unsigned long start[BINK2_MAX_SLICES];  /* first bit of each slice */
    unsigned long end[BINK2_MAX_SLICES];    /* bit after each slice */
    int status[BINK2_MAX_SLICES];
} SliceJobs;

/* Decodes slice i with its own reader and prediction state. Slices write
 * disjoint macroblock rows and read only the finished reference. */
static void decode_slice_job(void *arg, int i)
{
    SliceJobs *jobs = (SliceJobs *)arg;
    Bink2Video *v = jobs->v;
    Bink2Bits bits;

    bink2_bits_init(&bits, jobs->data, jobs->padded_size);
    bits.bitpos = jobs->start[i];
    if (v->version <= 'f')
        jobs->status[i] = bink2_video_decode_slice_old(v, &v->slice[i], &bits,
                                                       jobs->is_keyframe,
                                                       jobs->y0[i],
                                                       jobs->y1[i]);
    else
        jobs->status[i] = bink2_video_decode_slice_new(v, &v->slice[i], &bits,
                                                       jobs->is_keyframe,
                                                       jobs->y0[i],
                                                       jobs->y1[i]);
    jobs->end[i] = (unsigned long)bits.bitpos;
}

/* How many threads decode a frame of num_slices slices. Starts the pool
 * the first time it is needed; if it cannot start, decoding is serial. */
static int slice_threads(Bink2Video *v, int num_slices)
{
    int limit = v->thread_limit;
    int want;

    if (v->pool != NULL)
        return v->pool_workers + 1 < num_slices ? v->pool_workers + 1
                                                : num_slices;
    if (v->pool_workers < 0 || num_slices < 2)
        return 1;
    if (limit == 0)
        limit = bink2_env_thread_limit();
    if (limit == 0)
        limit = bink2_cpu_count();
    want = limit < num_slices ? limit : num_slices;
    if (want < 2)
        return 1;
    v->pool = bink2_pool_create(want - 1);
    if (v->pool == NULL) {
        v->pool_workers = -1;
        return 1;
    }
    v->pool_workers = want - 1;
    return want;
}

void bink2_video_set_threads(Bink2Video *v, int n)
{
    if (v == NULL || n < 0 || n == v->thread_limit)
        return;
    bink2_pool_destroy(v->pool);
    v->pool = NULL;
    v->pool_workers = 0;
    v->thread_limit = n;
}

static int decode_impl(Bink2Video *v, const unsigned char *data,
                       size_t real_size, size_t padded_size, int is_keyframe)
{
    Bink2Bits bits;
    unsigned long frame_flags;
    int num_slices;
    int slice_height[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    SliceJobs jobs;
    int threads;
    int i;

    /* The last picture becomes the reference by swapping pointers: every
     * Y/U/V macroblock of cur is rewritten each frame, and cur is only read
     * where this frame already wrote it. Alpha stays copied (see the end of
     * this function): KB2f motion macroblocks keep or add to the previous
     * alpha in place. */
    for (i = 0; i < 3; i++) {
        unsigned char *t = v->ref[i];

        v->ref[i] = v->cur[i];
        v->cur[i] = t;
    }

    frame_flags = le32(data);
    v->frame_flags = frame_flags;
    v->rgb_valid = 0;
    memset(v->mv[0], 0, v->mv_capacity * sizeof(int16_t));
    memset(v->mv[1], 0, v->mv_capacity * sizeof(int16_t));
    memset(v->mv_count, 0, v->mv_capacity);

    bink2_bits_init(&bits, data, padded_size);
    bink2_bits_skip(&bits, 32);

    num_slices = bink2_video_slice_heights(v->version, v->flags, v->height,
                                          slice_height);
    if (real_size < (size_t)4 * (size_t)num_slices)
        return BINK2_ERROR;

    bink2_bits_skip(&bits, 32 * (num_slices - 1));
    if (frame_flags & 0x10000) {
        if (!(frame_flags & 0x8000)) {
            int row_size = (int)((((v->height + 15) & ~15UL) >> 3) - 1);

            if (skip_block_flags(&bits, 1, row_size) != 0)
                return BINK2_ERROR;
        }
        if (!(frame_flags & 0x4000)) {
            int col_size = (int)((((v->width + 31) & ~31UL) >> 3) - 1);

            if (skip_block_flags(&bits, 1, col_size) != 0)
                return BINK2_ERROR;
        }
    }

    /* Every slice start is known up front: slice 0 follows the header,
     * the others are at their stored offsets. Reject what the serial walk
     * would reject before it reached that slice. */
    jobs.v = v;
    jobs.data = data;
    jobs.padded_size = padded_size;
    jobs.is_keyframe = is_keyframe;
    for (i = 0; i < num_slices; i++) {
        jobs.y0[i] = i == 0 ? 0 : slice_height[i - 1];
        jobs.y1[i] = slice_height[i];
        if (i == 0) {
            jobs.start[i] = bits.bitpos;
        } else {
            unsigned long target =
                le32(data + 4 + 4UL * (unsigned long)(i - 1)) * 8UL;

            if (target > (unsigned long)real_size * 8UL ||
                target < jobs.start[i - 1])
                return BINK2_ERROR;
            jobs.start[i] = target;
        }
    }

    threads = slice_threads(v, num_slices);
    if (threads > 1)
        bink2_pool_run(v->pool, decode_slice_job, &jobs, num_slices);
    else
        for (i = 0; i < num_slices; i++)
            decode_slice_job(&jobs, i);

    /* The checks the serial walk makes between slices: each slice must end
     * at or before the next one's start, and a valid frame may rely on a
     * few implicit zero bits after the last byte. More than 64 means the
     * packet was truncated. */
    for (i = 0; i < num_slices; i++) {
        if (jobs.status[i] != 0)
            return BINK2_ERROR;
        if (i > 0) {
            unsigned long end = (jobs.end[i - 1] + 31UL) & ~31UL;

            if (jobs.start[i] < end || jobs.start[i] - end > 0x7FFFFFFFUL)
                return BINK2_ERROR;
        }
    }
    if (jobs.end[num_slices - 1] > real_size * 8UL + 64UL)
        return BINK2_ERROR;

    if (v->cur[3] != NULL && v->ref[3] != NULL)
        memcpy(v->ref[3], v->cur[3], (size_t)v->stride[3] * v->aligned_h);
    v->has_prev = 1;
    return 0;
}

int bink2_video_decode(Bink2Video *v, const unsigned char *data, size_t size,
                       int is_keyframe)
{
    unsigned char *padded;
    int ret;

    if (v == NULL || data == NULL || size < 8)
        return BINK2_ERROR;
    if (size > (size_t)-1 - 8)
        return BINK2_ERROR;
    if (v->packet_capacity < size + 8) {
        padded = (unsigned char *)realloc(v->packet, size + 8);
        if (padded == NULL)
            return BINK2_ERROR;
        v->packet = padded;
        v->packet_capacity = size + 8;
    }
    padded = v->packet;
    memcpy(padded, data, size);
    memset(padded + size, 0, 8);
    ret = decode_impl(v, padded, size, size + 8, is_keyframe);
    if (ret != 0 && v->has_prev) {
        /* Do not publish a half-decoded frame after an error: restore the
         * previous picture so bink2_get_plane keeps returning it. */
        int i;

        for (i = 0; i < 4; i++) {
            unsigned long h = (i == 1 || i == 2) ? v->aligned_h / 2
                                                 : v->aligned_h;

            if (v->cur[i] != NULL && v->ref[i] != NULL)
                memcpy(v->cur[i], v->ref[i], (size_t)v->stride[i] * h);
        }
        v->rgb_valid = 0;
    }
    return ret;
}

const unsigned char *bink2_video_plane(Bink2Video *v, int plane)
{
    if (v == NULL || plane < 0 || plane > 3)
        return NULL;
    return v->cur[plane];
}

int bink2_video_stride(const Bink2Video *v, int plane)
{
    if (v == NULL || plane < 0 || plane > 3)
        return 0;
    return v->stride[plane];
}

int bink2_video_plane_width(const Bink2Video *v, int plane)
{
    if (v == NULL || plane < 0 || plane > 3)
        return 0;
    return v->plane_w[plane];
}

int bink2_video_plane_height(const Bink2Video *v, int plane)
{
    if (v == NULL || plane < 0 || plane > 3)
        return 0;
    return v->plane_h[plane];
}

typedef struct {
    Bink2Video *v;
    int bands;
} RgbJobs;

static void rgb_band_job(void *arg, int band)
{
    const RgbJobs *jobs = (const RgbJobs *)arg;
    const Bink2Video *v = jobs->v;
    int h = v->plane_h[0];
    /* Even band edges keep each chroma row inside one band. */
    int y0 = (int)(((long)h * band / jobs->bands) & ~1L);
    int y1 = band + 1 == jobs->bands
                 ? h : (int)(((long)h * (band + 1) / jobs->bands) & ~1L);
    int y;

    for (y = y0; y < y1; y++)
        bink2_yuv_row(v->rgb + (size_t)y * v->plane_w[0] * 3,
                      v->cur[0] + (size_t)y * v->stride[0],
                      v->cur[1] + (size_t)(y / 2) * v->stride[1],
                      v->cur[2] + (size_t)(y / 2) * v->stride[2],
                      v->plane_w[0], v->range == BINK2_RANGE_STUDIO);
}

const unsigned char *bink2_video_rgb(Bink2Video *v)
{
    RgbJobs jobs;

    if (v == NULL || v->cur[0] == NULL || v->cur[1] == NULL ||
        v->cur[2] == NULL)
        return NULL;
    if (v->rgb_valid)
        return v->rgb;

    /* Use the slice threads when the decoder has started them. */
    jobs.v = v;
    jobs.bands = v->pool != NULL ? v->pool_workers + 1 : 1;
    if (jobs.bands > 1)
        bink2_pool_run(v->pool, rgb_band_job, &jobs, jobs.bands);
    else
        rgb_band_job(&jobs, 0);
    v->rgb_valid = 1;
    return v->rgb;
}

int bink2_video_motion(Bink2Video *v, unsigned long *mb_w, unsigned long *mb_h,
                       const int16_t **mv_x, const int16_t **mv_y,
                       const unsigned char **mv_count)
{
    if (v == NULL || v->mv[0] == NULL)
        return 0;
    if (mb_w != NULL)
        *mb_w = (unsigned long)v->mb_w;
    if (mb_h != NULL)
        *mb_h = (unsigned long)v->mb_h;
    if (mv_x != NULL)
        *mv_x = v->mv[0];
    if (mv_y != NULL)
        *mv_y = v->mv[1];
    if (mv_count != NULL)
        *mv_count = v->mv_count;
    return 1;
}
