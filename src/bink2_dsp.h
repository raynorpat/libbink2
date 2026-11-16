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

#endif
