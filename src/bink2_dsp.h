#ifndef LIBBINK2_DSP_H
#define LIBBINK2_DSP_H

#include <stdint.h>

void bink2_idct_old(float block[64]);
void bink2_put_block_f(unsigned char *dst, int stride, const float block[64]);
void bink2_add_block_f(unsigned char *dst, int stride, const float block[64]);

void bink2_idct_new(int32_t block[64]);
void bink2_put_block(unsigned char *dst, int stride, const int32_t block[64]);
void bink2_add_block(unsigned char *dst, int stride, const int32_t block[64]);

void bink2_mc_luma(unsigned char *dst, int dst_stride,
                   const unsigned char *src, int src_stride,
                   int x, int y, int frame_w, int frame_h,
                   int mvx, int mvy);
void bink2_mc_chroma(unsigned char *dst, int dst_stride,
                     const unsigned char *src, int src_stride,
                     int x, int y, int frame_w, int frame_h,
                     int mvx, int mvy);

/* One row of YUV 4:2:0 to packed RGB24: y has width samples, u and v have
 * (width + 1) / 2. Full-range BT.601, or studio range when studio_range. */
void bink2_yuv_row_c(unsigned char *rgb, const unsigned char *y,
                     const unsigned char *u, const unsigned char *v,
                     int width, int studio_range);
void bink2_yuv_row(unsigned char *rgb, const unsigned char *y,
                   const unsigned char *u, const unsigned char *v,
                   int width, int studio_range);

#endif
