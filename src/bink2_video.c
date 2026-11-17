#include "bink2_video.h"

#include <stdlib.h>
#include <string.h>

static int clip8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return v;
}

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
    v->old_dc_cur = (Bink2OldDc *)calloc((size_t)v->mb_w + 1,
                                         sizeof(Bink2OldDc));
    v->old_dc_prev = (Bink2OldDc *)calloc((size_t)v->mb_w + 1,
                                          sizeof(Bink2OldDc));
    v->old_mv_cur = (int32_t *)calloc(((size_t)v->mb_w + 1) * 8,
                                      sizeof(int32_t));
    v->old_mv_prev = (int32_t *)calloc(((size_t)v->mb_w + 1) * 8,
                                       sizeof(int32_t));
    v->rgb_size = (size_t)width * height * 3;
    v->rgb = (unsigned char *)calloc(1, v->rgb_size);
    if (v->mv[0] == NULL || v->mv[1] == NULL || v->mv_count == NULL ||
        v->old_dc_cur == NULL || v->old_dc_prev == NULL ||
        v->old_mv_cur == NULL || v->old_mv_prev == NULL ||
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
    free(v->old_dc_cur);
    free(v->old_dc_prev);
    free(v->old_mv_cur);
    free(v->old_mv_prev);
    free(v->rgb);
    memset(v, 0, sizeof(*v));
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
    memset(v->old_dc_cur, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2OldDc));
    memset(v->old_dc_prev, 0, ((size_t)v->mb_w + 1) * sizeof(Bink2OldDc));
    memset(v->old_mv_cur, 0, ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));
    memset(v->old_mv_prev, 0, ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));
    v->old_mb_pos = 0;
    v->old_comp = 0;
    v->has_prev = 0;
    v->rgb_valid = 0;
}

int bink2_video_decode(Bink2Video *v, const unsigned char *data, size_t size,
                       int is_keyframe)
{
    Bink2Bits bits;
    unsigned long frame_flags;
    int num_slices;
    int slice_height[4] = { 0, 0, 0, 0 };
    int i;

    if (v == NULL || data == NULL || size < 8)
        return BINK2_ERROR;
    if (v->version > 'f') {
        /* The newer bitstream path lands in Task 9. */
        return BINK2_ERROR;
    }

    frame_flags = le32(data);
    v->rgb_valid = 0;
    memset(v->mv[0], 0, v->mv_capacity * sizeof(int16_t));
    memset(v->mv[1], 0, v->mv_capacity * sizeof(int16_t));
    memset(v->mv_count, 0, v->mv_capacity);
    memset(v->old_mv_cur, 0,
           ((size_t)v->mb_w + 1) * 8 * sizeof(int32_t));

    bink2_bits_init(&bits, data, size);
    bink2_bits_skip(&bits, 32);

    num_slices = 2;
    slice_height[0] = (int)(((v->height / 2 + 16) & ~31UL));
    slice_height[1] = (int)v->aligned_h;

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

    for (i = 0; i < num_slices; i++) {
        int y0 = i == 0 ? 0 : slice_height[i - 1];
        int y1 = slice_height[i];

        if (i > 0) {
            unsigned long target;

            bink2_bits_align32(&bits);
            target = le32(data + 4 + 4UL * (unsigned long)(i - 1)) * 8UL;
            if (target < bits.bitpos)
                return BINK2_ERROR;
            bink2_bits_skip(&bits, (int)(target - bits.bitpos));
        }
        if (bink2_video_decode_slice_old(v, &bits, is_keyframe, y0, y1) != 0)
            return BINK2_ERROR;
    }

    for (i = 0; i < 4; i++) {
        unsigned long h = (i == 1 || i == 2) ? v->aligned_h / 2
                                             : v->aligned_h;

        if (v->cur[i] != NULL && v->ref[i] != NULL)
            memcpy(v->ref[i], v->cur[i], (size_t)v->stride[i] * h);
    }
    v->has_prev = 1;
    return 0;
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

const unsigned char *bink2_video_rgb(Bink2Video *v)
{
    int x, y;

    if (v == NULL || v->cur[0] == NULL || v->cur[1] == NULL ||
        v->cur[2] == NULL)
        return NULL;
    if (v->rgb_valid)
        return v->rgb;

    for (y = 0; y < v->plane_h[0]; y++) {
        const unsigned char *yp = v->cur[0] + (size_t)y * v->stride[0];
        const unsigned char *up = v->cur[1] +
            (size_t)(y / 2) * v->stride[1];
        const unsigned char *vp = v->cur[2] +
            (size_t)(y / 2) * v->stride[2];

        for (x = 0; x < v->plane_w[0]; x++) {
            int yy = yp[x];
            int uu = up[x / 2] - 128;
            int vv = vp[x / 2] - 128;
            int r, g, b;

            if (v->range == BINK2_RANGE_FULL) {
                r = yy + ((359 * vv + 128) >> 8);
                g = yy - ((88 * uu + 183 * vv + 128) >> 8);
                b = yy + ((454 * uu + 128) >> 8);
            } else {
                int sy = yy - 16;

                r = (298 * sy + 409 * vv + 128) >> 8;
                g = (298 * sy - 100 * uu - 208 * vv + 128) >> 8;
                b = (298 * sy + 516 * uu + 128) >> 8;
            }
            {
                size_t off = ((size_t)y * v->plane_w[0] + x) * 3;

                v->rgb[off + 0] = (unsigned char)clip8(r);
                v->rgb[off + 1] = (unsigned char)clip8(g);
                v->rgb[off + 2] = (unsigned char)clip8(b);
            }
        }
    }
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
