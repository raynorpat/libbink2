#include "bink2_dsp.h"

#include <math.h>
#include <string.h>

static int clip8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return v;
}

static void idct_1d_old(float *blk, int step)
{
    float t00 =  blk[2 * step] + blk[6 * step];
    float t01 = (blk[2 * step] - blk[6 * step]) * 1.4142135f - t00;
    float t02 =  blk[0 * step] + blk[4 * step];
    float t03 =  blk[0 * step] - blk[4 * step];
    float t04 =  blk[3 * step] + blk[5 * step];
    float t05 =  blk[3 * step] - blk[5 * step];
    float t06 =  blk[1 * step] + blk[7 * step];
    float t07 =  blk[1 * step] - blk[7 * step];
    float t08 = t02 + t00;
    float t09 = t02 - t00;
    float t10 = t03 + t01;
    float t11 = t03 - t01;
    float t12 = t06 + t04;
    float t13 = (t06 - t04) * 1.4142135f;
    float t14 = (t07 - t05) * 1.847759f;
    float t15 = t05 * 2.613126f + t14 - t12;
    float t16 = t13 - t15;
    float t17 = t07 * 1.0823922f - t14 + t16;

    blk[0 * step] = t08 + t12;
    blk[1 * step] = t10 + t15;
    blk[2 * step] = t11 + t16;
    blk[3 * step] = t09 - t17;
    blk[4 * step] = t09 + t17;
    blk[5 * step] = t11 - t16;
    blk[6 * step] = t10 - t15;
    blk[7 * step] = t08 - t12;
}

void bink2_idct_old(float *block)
{
    int i, j;

    block[0] += 512.0f;
    for (i = 0; i < 8; i++)
        idct_1d_old(block + i, 8);
    for (i = 0; i < 8; i++) {
        idct_1d_old(block, 1);
        for (j = 0; j < 8; j++)
            block[j] -= 512.0f;
        block += 8;
    }
}

void bink2_put_block_f(unsigned char *dst, int stride, const float block[64])
{
    int i, j;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++)
            dst[j] = (unsigned char)clip8((int)lrintf(block[j]));
        dst += stride;
        block += 8;
    }
}

void bink2_add_block_f(unsigned char *dst, int stride, const float block[64])
{
    int i, j;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++)
            dst[j] = (unsigned char)clip8((int)dst[j] +
                                          (int)lrintf(block[j]));
        dst += stride;
        block += 8;
    }
}

#define IDCT_MUL_A(val) ((val) + ((val) >> 2))
#define IDCT_MUL_B(val) ((val) >> 1)
#define IDCT_MUL_C(val) ((val) - ((val) >> 2) - ((val) >> 4))
#define IDCT_MUL_D(val) ((val) + ((val) >> 2) - ((val) >> 4))
#define IDCT_MUL_E(val) ((val) >> 2)

static void idct_1d_new(int16_t *blk, int step, int shift)
{
    int tmp00 =  blk[3 * step] + blk[5 * step];
    int tmp01 =  blk[3 * step] - blk[5 * step];
    int tmp02 =  IDCT_MUL_A(blk[2 * step]) + IDCT_MUL_B(blk[6 * step]);
    int tmp03 =  IDCT_MUL_B(blk[2 * step]) - IDCT_MUL_A(blk[6 * step]);
    int tmp0  = (blk[0 * step] + blk[4 * step]) + tmp02;
    int tmp1  = (blk[0 * step] + blk[4 * step]) - tmp02;
    int tmp2  =  blk[0 * step] - blk[4 * step];
    int tmp3  =  blk[1 * step] + tmp00;
    int tmp4  =  blk[1 * step] - tmp00;
    int tmp5  =  tmp01 + blk[7 * step];
    int tmp6  =  tmp01 - blk[7 * step];
    int tmp7  =  tmp4 + IDCT_MUL_C(tmp6);
    int tmp8  =  IDCT_MUL_C(tmp4) - tmp6;
    int tmp9  =  IDCT_MUL_D(tmp3) + IDCT_MUL_E(tmp5);
    int tmp10 =  IDCT_MUL_E(tmp3) - IDCT_MUL_D(tmp5);
    int tmp11 =  tmp2 + tmp03;
    int tmp12 =  tmp2 - tmp03;

    blk[0 * step] = (int16_t)((tmp0  + tmp9)  >> shift);
    blk[1 * step] = (int16_t)((tmp11 + tmp7)  >> shift);
    blk[2 * step] = (int16_t)((tmp12 + tmp8)  >> shift);
    blk[3 * step] = (int16_t)((tmp1  + tmp10) >> shift);
    blk[4 * step] = (int16_t)((tmp1  - tmp10) >> shift);
    blk[5 * step] = (int16_t)((tmp12 - tmp8)  >> shift);
    blk[6 * step] = (int16_t)((tmp11 - tmp7)  >> shift);
    blk[7 * step] = (int16_t)((tmp0  - tmp9)  >> shift);
}

void bink2_idct_new_c(int32_t block[64])
{
    int16_t tmp[64];
    int i;

    for (i = 0; i < 64; i++)
        tmp[i] = (int16_t)block[i];
    for (i = 0; i < 8; i++)
        idct_1d_new(tmp + i, 8, 0);
    for (i = 0; i < 8; i++)
        idct_1d_new(tmp + i * 8, 1, 6);
    for (i = 0; i < 64; i++)
        block[i] = tmp[i];
}

void bink2_put_block_c(unsigned char *dst, int stride,
                       const int32_t block[64])
{
    int i, j;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++)
            dst[j] = (unsigned char)clip8(block[j * 8 + i]);
        dst += stride;
    }
}

void bink2_add_block_c(unsigned char *dst, int stride,
                       const int32_t block[64])
{
    int i, j;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++)
            dst[j] = (unsigned char)clip8((int)dst[j] + block[j * 8 + i]);
        dst += stride;
    }
}

void bink2_idct_new(int32_t block[64])
{
#if defined(BINK2_SIMD)
    BINK2_SIMD(bink2_idct_new)(block);
#else
    bink2_idct_new_c(block);
#endif
}

void bink2_put_block(unsigned char *dst, int stride, const int32_t block[64])
{
#if defined(BINK2_SIMD)
    BINK2_SIMD(bink2_put_block)(dst, stride, block);
#else
    bink2_put_block_c(dst, stride, block);
#endif
}

void bink2_add_block(unsigned char *dst, int stride, const int32_t block[64])
{
#if defined(BINK2_SIMD)
    BINK2_SIMD(bink2_add_block)(dst, stride, block);
#else
    bink2_add_block_c(dst, stride, block);
#endif
}

#define EDGE_STRIDE 32

static void edge_copy(unsigned char *dst, const unsigned char *src,
                      int src_stride, int x0, int y0, int bw, int bh,
                      int w, int h)
{
    int i, j;

    for (j = 0; j < bh; j++) {
        int sy = y0 + j;

        if (sy < 0)
            sy = 0;
        else if (sy >= h)
            sy = h - 1;
        for (i = 0; i < bw; i++) {
            int sx = x0 + i;

            if (sx < 0)
                sx = 0;
            else if (sx >= w)
                sx = w - 1;
            dst[j * EDGE_STRIDE + i] = src[sy * src_stride + sx];
        }
    }
}

#define LH_FILTER(src) \
    ((((((src)[0] + (src)[1]) * 19) >> 1) - \
      ((src)[-1] + (src)[2]) * 2 + \
      (((src)[-2] + (src)[3]) >> 1) + 8) >> 4)
#define LV_FILTER(src, i) \
    ((((((src)[0] + (src)[i]) * 19) >> 1) - \
      ((src)[-(i)] + (src)[2 * (i)]) * 2 + \
      (((src)[-2 * (i)] + (src)[3 * (i)]) >> 1) + 8) >> 4)

void bink2_mc_luma(unsigned char *dst, int dst_stride,
                   const unsigned char *src, int src_stride,
                   int x, int y, int frame_w, int frame_h,
                   int mvx, int mvy)
{
    unsigned char edge[EDGE_STRIDE * 24];
    const unsigned char *msrc;
    int ss = src_stride;
    int fw = (frame_w + 31) & ~31;
    int fh = (frame_h + 31) & ~31;
    int mx = x + (mvx >> 1);
    int my = y + (mvy >> 1);
    int hmode = mvx & 1;
    int vmode = mvy & 1;
    int i, j;

    if (mx < 2 || my < 2 || mx + 19 > fw || my + 19 > fh) {
        edge_copy(edge, src, src_stride, mx - 2, my - 2, 21, 21, fw, fh);
        msrc = edge + 2 + 2 * EDGE_STRIDE;
        ss = EDGE_STRIDE;
    } else {
        msrc = src + mx + my * src_stride;
    }

    if (hmode == 0 && vmode == 0) {
        for (j = 0; j < 16; j++) {
            memcpy(dst, msrc, 16);
            dst += dst_stride;
            msrc += ss;
        }
    } else if (vmode == 0) {
        for (j = 0; j < 16; j++) {
            for (i = 0; i < 16; i++)
                dst[i] = (unsigned char)clip8(LH_FILTER(msrc + i));
            dst += dst_stride;
            msrc += ss;
        }
    } else if (hmode == 0) {
        for (i = 0; i < 16; i++) {
            for (j = 0; j < 16; j++)
                dst[j * dst_stride + i] =
                    (unsigned char)clip8(LV_FILTER(msrc + j * ss + i, ss));
        }
    } else {
        unsigned char temp[21 * 16];

        msrc -= 2 * ss;
        for (i = 0; i < 21; i++) {
            for (j = 0; j < 16; j++)
                temp[i * 16 + j] =
                    (unsigned char)clip8(LH_FILTER(msrc + j));
            msrc += ss;
        }
        for (j = 0; j < 16; j++) {
            for (i = 0; i < 16; i++)
                dst[i] = (unsigned char)clip8(
                    LV_FILTER(temp + (j + 2) * 16 + i, 16));
            dst += dst_stride;
        }
    }
}

#define CH1_FILTER(src) (((src)[0] * 6 + (src)[1] * 2 + 4) >> 3)
#define CH2_FILTER(src) (((src)[0] + (src)[1] + 1) >> 1)
#define CH3_FILTER(src) (((src)[0] * 2 + (src)[1] * 6 + 4) >> 3)
#define CV1_FILTER(src, i) (((src)[0] * 6 + (src)[i] * 2 + 4) >> 3)
#define CV2_FILTER(src, i) (((src)[0] + (src)[i] + 1) >> 1)
#define CV3_FILTER(src, i) (((src)[0] * 2 + (src)[i] * 6 + 4) >> 3)

static int chroma_h(int mode, const unsigned char *src)
{
    switch (mode) {
    case 1:
        return CH1_FILTER(src);
    case 2:
        return CH2_FILTER(src);
    case 3:
        return CH3_FILTER(src);
    default:
        return src[0];
    }
}

static int chroma_v(int mode, const unsigned char *src, int stride)
{
    switch (mode) {
    case 1:
        return CV1_FILTER(src, stride);
    case 2:
        return CV2_FILTER(src, stride);
    case 3:
        return CV3_FILTER(src, stride);
    default:
        return src[0];
    }
}

void bink2_mc_chroma(unsigned char *dst, int dst_stride,
                     const unsigned char *src, int src_stride,
                     int x, int y, int frame_w, int frame_h,
                     int mvx, int mvy)
{
    unsigned char edge[EDGE_STRIDE * 24];
    const unsigned char *msrc;
    int ss = src_stride;
    int fw = (frame_w + 15) & ~15;
    int fh = (frame_h + 15) & ~15;
    int mx = x + (mvx >> 2);
    int my = y + (mvy >> 2);
    int hmode = mvx & 3;
    int vmode = mvy & 3;
    int i, j;

    if (mx < 0 || my < 0 || mx + 9 > fw || my + 9 > fh) {
        edge_copy(edge, src, src_stride, mx, my, 9, 9, fw, fh);
        msrc = edge;
        ss = EDGE_STRIDE;
    } else {
        msrc = src + mx + my * src_stride;
    }

    if (hmode == 0 && vmode == 0) {
        for (j = 0; j < 8; j++) {
            memcpy(dst, msrc, 8);
            dst += dst_stride;
            msrc += ss;
        }
    } else if (vmode == 0) {
        for (j = 0; j < 8; j++) {
            for (i = 0; i < 8; i++)
                dst[i] = (unsigned char)clip8(chroma_h(hmode, msrc + i));
            dst += dst_stride;
            msrc += ss;
        }
    } else if (hmode == 0) {
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++)
                dst[j * dst_stride + i] =
                    (unsigned char)clip8(chroma_v(vmode, msrc + j * ss + i,
                                                  ss));
        }
    } else {
        unsigned char temp[9 * 8];

        for (i = 0; i < 9; i++) {
            for (j = 0; j < 8; j++)
                temp[i * 8 + j] =
                    (unsigned char)clip8(chroma_h(hmode, msrc + j));
            msrc += ss;
        }
        for (j = 0; j < 8; j++) {
            for (i = 0; i < 8; i++)
                dst[i] = (unsigned char)clip8(
                    chroma_v(vmode, temp + j * 8 + i, 8));
            dst += dst_stride;
        }
    }
}

void bink2_yuv_row_c(unsigned char *rgb, const unsigned char *y,
                     const unsigned char *u, const unsigned char *v,
                     int width, int studio_range)
{
    int x;

    for (x = 0; x < width; x++) {
        int yy = y[x];
        int uu = u[x / 2] - 128;
        int vv = v[x / 2] - 128;
        int r, g, b;

        if (!studio_range) {
            r = yy + ((359 * vv + 128) >> 8);
            g = yy - ((88 * uu + 183 * vv + 128) >> 8);
            b = yy + ((454 * uu + 128) >> 8);
        } else {
            int sy = yy - 16;

            r = (298 * sy + 409 * vv + 128) >> 8;
            g = (298 * sy - 100 * uu - 208 * vv + 128) >> 8;
            b = (298 * sy + 516 * uu + 128) >> 8;
        }
        rgb[x * 3 + 0] = (unsigned char)clip8(r);
        rgb[x * 3 + 1] = (unsigned char)clip8(g);
        rgb[x * 3 + 2] = (unsigned char)clip8(b);
    }
}

void bink2_yuv_row(unsigned char *rgb, const unsigned char *y,
                   const unsigned char *u, const unsigned char *v,
                   int width, int studio_range)
{
    bink2_yuv_row_c(rgb, y, u, v, width, studio_range);
}
