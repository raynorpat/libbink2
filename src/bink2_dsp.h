#ifndef LIBBINK2_DSP_H
#define LIBBINK2_DSP_H

#include <stdint.h>

/* SIMD is chosen at compile time: NEON on arm64, SSE2 on x86-64. Each
 * routine below dispatches to it; the *_c versions are the scalar
 * references, kept in every build so tests can compare against them.
 * Define BINK2_NO_SIMD (CMake -DBINK2_SIMD=OFF) to use only scalar code. */
#if !defined(BINK2_NO_SIMD)
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define BINK2_SIMD_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define BINK2_SIMD_SSE2 1
#endif
#endif

void bink2_idct_old(float block[64]);
void bink2_put_block_f(unsigned char *dst, int stride, const float block[64]);
void bink2_add_block_f(unsigned char *dst, int stride, const float block[64]);
void bink2_put_block_f_c(unsigned char *dst, int stride,
                         const float block[64]);
void bink2_add_block_f_c(unsigned char *dst, int stride,
                         const float block[64]);

void bink2_idct_new(int32_t block[64]);
void bink2_put_block(unsigned char *dst, int stride, const int32_t block[64]);
void bink2_add_block(unsigned char *dst, int stride, const int32_t block[64]);
void bink2_idct_new_c(int32_t block[64]);
void bink2_put_block_c(unsigned char *dst, int stride,
                       const int32_t block[64]);
void bink2_add_block_c(unsigned char *dst, int stride,
                       const int32_t block[64]);

void bink2_mc_luma(unsigned char *dst, int dst_stride,
                   const unsigned char *src, int src_stride,
                   int x, int y, int frame_w, int frame_h,
                   int mvx, int mvy);
void bink2_mc_chroma(unsigned char *dst, int dst_stride,
                     const unsigned char *src, int src_stride,
                     int x, int y, int frame_w, int frame_h,
                     int mvx, int mvy);
void bink2_mc_luma_c(unsigned char *dst, int dst_stride,
                     const unsigned char *src, int src_stride,
                     int x, int y, int frame_w, int frame_h,
                     int mvx, int mvy);
void bink2_mc_chroma_c(unsigned char *dst, int dst_stride,
                       const unsigned char *src, int src_stride,
                       int x, int y, int frame_w, int frame_h,
                       int mvx, int mvy);
/* The filter step alone: src points at the block's top-left sample, with
 * the taps around it readable (2 before and 3 after for luma, 1 after for
 * chroma). hmode/vmode are the sub-pel phases. */
void bink2_mc_luma_block_c(unsigned char *dst, int dst_stride,
                           const unsigned char *src, int src_stride,
                           int hmode, int vmode);
void bink2_mc_chroma_block_c(unsigned char *dst, int dst_stride,
                             const unsigned char *src, int src_stride,
                             int hmode, int vmode);

/* One row of YUV 4:2:0 to packed RGB24: y has width samples, u and v have
 * (width + 1) / 2. Full-range BT.601, or studio range when studio_range. */
void bink2_yuv_row_c(unsigned char *rgb, const unsigned char *y,
                     const unsigned char *u, const unsigned char *v,
                     int width, int studio_range);
void bink2_yuv_row(unsigned char *rgb, const unsigned char *y,
                   const unsigned char *u, const unsigned char *v,
                   int width, int studio_range);

#if defined(BINK2_SIMD_NEON)
#define BINK2_SIMD(name) name##_neon
#elif defined(BINK2_SIMD_SSE2)
#define BINK2_SIMD(name) name##_sse2
#endif

/* Round-to-nearest-even float conversion needs AArch64 NEON (vcvtnq). */
#if (defined(BINK2_SIMD_NEON) && defined(__aarch64__)) || \
    defined(BINK2_SIMD_SSE2)
#define BINK2_SIMD_FLOAT_BLOCK 1
#endif

#if defined(BINK2_SIMD)
void BINK2_SIMD(bink2_idct_new)(int32_t block[64]);
void BINK2_SIMD(bink2_put_block)(unsigned char *dst, int stride,
                                 const int32_t block[64]);
void BINK2_SIMD(bink2_add_block)(unsigned char *dst, int stride,
                                 const int32_t block[64]);
void BINK2_SIMD(bink2_mc_luma_block)(unsigned char *dst, int dst_stride,
                                     const unsigned char *src, int src_stride,
                                     int hmode, int vmode);
void BINK2_SIMD(bink2_mc_chroma_block)(unsigned char *dst, int dst_stride,
                                       const unsigned char *src,
                                       int src_stride, int hmode, int vmode);
/* Converts the first (width & ~15) pixels of a row and returns that
 * count; reads no sample past them. */
int BINK2_SIMD(bink2_yuv_row16)(unsigned char *rgb, const unsigned char *y,
                                const unsigned char *u, const unsigned char *v,
                                int width, int studio_range);
#endif
#if defined(BINK2_SIMD_FLOAT_BLOCK)
void BINK2_SIMD(bink2_put_block_f)(unsigned char *dst, int stride,
                                   const float block[64]);
void BINK2_SIMD(bink2_add_block_f)(unsigned char *dst, int stride,
                                   const float block[64]);
#endif

#endif
