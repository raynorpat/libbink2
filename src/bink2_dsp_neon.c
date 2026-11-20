/* NEON versions of the DSP routines. Each matches its scalar reference in
 * bink2_dsp.c bit for bit; tests/test_dsp_simd.c checks that. */
#include "bink2_dsp.h"

#if defined(BINK2_SIMD_NEON)

#include <arm_neon.h>

/* The integer IDCT's multiplies, on four 32-bit lanes. */
#define MUL_A(x) vaddq_s32((x), vshrq_n_s32((x), 2))
#define MUL_B(x) vshrq_n_s32((x), 1)
#define MUL_C(x) vsubq_s32(vsubq_s32((x), vshrq_n_s32((x), 2)), \
                           vshrq_n_s32((x), 4))
#define MUL_D(x) vsubq_s32(vaddq_s32((x), vshrq_n_s32((x), 2)), \
                           vshrq_n_s32((x), 4))
#define MUL_E(x) vshrq_n_s32((x), 2)

/* One 1-D IDCT on four lanes: b[k] holds coefficient k of each lane. */
static void idct8(const int32x4_t b[8], int32x4_t o[8])
{
    int32x4_t t00 = vaddq_s32(b[3], b[5]);
    int32x4_t t01 = vsubq_s32(b[3], b[5]);
    int32x4_t t02 = vaddq_s32(MUL_A(b[2]), MUL_B(b[6]));
    int32x4_t t03 = vsubq_s32(MUL_B(b[2]), MUL_A(b[6]));
    int32x4_t s04 = vaddq_s32(b[0], b[4]);
    int32x4_t t0 = vaddq_s32(s04, t02);
    int32x4_t t1 = vsubq_s32(s04, t02);
    int32x4_t t2 = vsubq_s32(b[0], b[4]);
    int32x4_t t3 = vaddq_s32(b[1], t00);
    int32x4_t t4 = vsubq_s32(b[1], t00);
    int32x4_t t5 = vaddq_s32(t01, b[7]);
    int32x4_t t6 = vsubq_s32(t01, b[7]);
    int32x4_t t7 = vaddq_s32(t4, MUL_C(t6));
    int32x4_t t8 = vsubq_s32(MUL_C(t4), t6);
    int32x4_t t9 = vaddq_s32(MUL_D(t3), MUL_E(t5));
    int32x4_t t10 = vsubq_s32(MUL_E(t3), MUL_D(t5));
    int32x4_t t11 = vaddq_s32(t2, t03);
    int32x4_t t12 = vsubq_s32(t2, t03);

    o[0] = vaddq_s32(t0, t9);
    o[1] = vaddq_s32(t11, t7);
    o[2] = vaddq_s32(t12, t8);
    o[3] = vaddq_s32(t1, t10);
    o[4] = vsubq_s32(t1, t10);
    o[5] = vsubq_s32(t12, t8);
    o[6] = vsubq_s32(t11, t7);
    o[7] = vsubq_s32(t0, t9);
}

/* Transposes an 8x8 matrix of int16 held as eight row vectors. */
static void transpose8(int16x8_t r[8])
{
    int16x8x2_t a0 = vtrnq_s16(r[0], r[1]);
    int16x8x2_t a1 = vtrnq_s16(r[2], r[3]);
    int16x8x2_t a2 = vtrnq_s16(r[4], r[5]);
    int16x8x2_t a3 = vtrnq_s16(r[6], r[7]);
    int32x4x2_t b0 = vtrnq_s32(vreinterpretq_s32_s16(a0.val[0]),
                               vreinterpretq_s32_s16(a1.val[0]));
    int32x4x2_t b1 = vtrnq_s32(vreinterpretq_s32_s16(a0.val[1]),
                               vreinterpretq_s32_s16(a1.val[1]));
    int32x4x2_t b2 = vtrnq_s32(vreinterpretq_s32_s16(a2.val[0]),
                               vreinterpretq_s32_s16(a3.val[0]));
    int32x4x2_t b3 = vtrnq_s32(vreinterpretq_s32_s16(a2.val[1]),
                               vreinterpretq_s32_s16(a3.val[1]));

#define JOIN(lo, hi) vreinterpretq_s16_s32(vcombine_s32(lo, hi))
    r[0] = JOIN(vget_low_s32(b0.val[0]), vget_low_s32(b2.val[0]));
    r[1] = JOIN(vget_low_s32(b1.val[0]), vget_low_s32(b3.val[0]));
    r[2] = JOIN(vget_low_s32(b0.val[1]), vget_low_s32(b2.val[1]));
    r[3] = JOIN(vget_low_s32(b1.val[1]), vget_low_s32(b3.val[1]));
    r[4] = JOIN(vget_high_s32(b0.val[0]), vget_high_s32(b2.val[0]));
    r[5] = JOIN(vget_high_s32(b1.val[0]), vget_high_s32(b3.val[0]));
    r[6] = JOIN(vget_high_s32(b0.val[1]), vget_high_s32(b2.val[1]));
    r[7] = JOIN(vget_high_s32(b1.val[1]), vget_high_s32(b3.val[1]));
#undef JOIN
}

/* One IDCT pass over eight lanes held as int16 vectors r[k], then the
 * scalar's ">> shift" and truncation back to int16. */
static void idct_pass(int16x8_t r[8], int shift)
{
    int32x4_t lo[8], hi[8], olo[8], ohi[8];
    int k;

    for (k = 0; k < 8; k++) {
        lo[k] = vmovl_s16(vget_low_s16(r[k]));
        hi[k] = vmovl_s16(vget_high_s16(r[k]));
    }
    idct8(lo, olo);
    idct8(hi, ohi);
    for (k = 0; k < 8; k++) {
        if (shift) {
            olo[k] = vshrq_n_s32(olo[k], 6);
            ohi[k] = vshrq_n_s32(ohi[k], 6);
        }
        r[k] = vcombine_s16(vmovn_s32(olo[k]), vmovn_s32(ohi[k]));
    }
}

void bink2_idct_new_neon(int32_t block[64])
{
    int16x8_t r[8];
    int k;

    /* Rows, truncated to int16 as the scalar does. */
    for (k = 0; k < 8; k++)
        r[k] = vcombine_s16(vmovn_s32(vld1q_s32(block + 8 * k)),
                            vmovn_s32(vld1q_s32(block + 8 * k + 4)));
    idct_pass(r, 0);    /* columns: lanes are columns */
    transpose8(r);
    idct_pass(r, 1);    /* rows: lanes are rows */
    transpose8(r);
    for (k = 0; k < 8; k++) {
        vst1q_s32(block + 8 * k, vmovl_s16(vget_low_s16(r[k])));
        vst1q_s32(block + 8 * k + 4, vmovl_s16(vget_high_s16(r[k])));
    }
}

/* block[j * 8 + i] is output row i, column j. Saturating to int16 first
 * gives the same result as clamping the int32 to 0..255. */
static void load_transposed(int16x8_t r[8], const int32_t block[64])
{
    int k;

    for (k = 0; k < 8; k++)
        r[k] = vcombine_s16(vqmovn_s32(vld1q_s32(block + 8 * k)),
                            vqmovn_s32(vld1q_s32(block + 8 * k + 4)));
    transpose8(r);
}

void bink2_put_block_neon(unsigned char *dst, int stride,
                          const int32_t block[64])
{
    int16x8_t r[8];
    int i;

    load_transposed(r, block);
    for (i = 0; i < 8; i++)
        vst1_u8(dst + (long)i * stride, vqmovun_s16(r[i]));
}

void bink2_add_block_neon(unsigned char *dst, int stride,
                          const int32_t block[64])
{
    int16x8_t r[8];
    int i;

    load_transposed(r, block);
    for (i = 0; i < 8; i++) {
        unsigned char *d = dst + (long)i * stride;
        int16x8_t px = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(d)));

        vst1_u8(d, vqmovun_s16(vqaddq_s16(px, r[i])));
    }
}

/* Luma 6-tap half-pel filter on eight samples, taps at -2..3:
 * ((((p0 + p1) * 19) >> 1) - (m1 + p2) * 2 + ((m2 + p3) >> 1) + 8) >> 4,
 * clamped to 0..255. Every intermediate fits in int16. */
static uint8x8_t luma_tap(uint8x8_t m2, uint8x8_t m1, uint8x8_t p0,
                          uint8x8_t p1, uint8x8_t p2, uint8x8_t p3)
{
    int16x8_t a = vreinterpretq_s16_u16(vaddl_u8(p0, p1));
    int16x8_t b = vreinterpretq_s16_u16(vaddl_u8(m1, p2));
    int16x8_t c = vreinterpretq_s16_u16(vaddl_u8(m2, p3));
    int16x8_t r = vshrq_n_s16(vmulq_n_s16(a, 19), 1);

    r = vsubq_s16(r, vshlq_n_s16(b, 1));
    r = vaddq_s16(r, vshrq_n_s16(c, 1));
    r = vaddq_s16(r, vdupq_n_s16(8));
    return vqmovun_s16(vshrq_n_s16(r, 4));
}

static uint8x16_t luma_tap16(const uint8x16_t r[6])
{
    uint8x8_t lo = luma_tap(vget_low_u8(r[0]), vget_low_u8(r[1]),
                            vget_low_u8(r[2]), vget_low_u8(r[3]),
                            vget_low_u8(r[4]), vget_low_u8(r[5]));
    uint8x8_t hi = luma_tap(vget_high_u8(r[0]), vget_high_u8(r[1]),
                            vget_high_u8(r[2]), vget_high_u8(r[3]),
                            vget_high_u8(r[4]), vget_high_u8(r[5]));

    return vcombine_u8(lo, hi);
}

/* Horizontal filter of one 16-sample row. Reads s[-2..18] only. */
static uint8x16_t luma_hrow(const unsigned char *s)
{
    uint8x16_t lo = vld1q_u8(s - 2);    /* s[-2..13] */
    uint8x16_t hi = vld1q_u8(s + 3);    /* s[3..18] */
    uint8x8_t a = luma_tap(vget_low_u8(lo),
                           vget_low_u8(vextq_u8(lo, lo, 1)),
                           vget_low_u8(vextq_u8(lo, lo, 2)),
                           vget_low_u8(vextq_u8(lo, lo, 3)),
                           vget_low_u8(vextq_u8(lo, lo, 4)),
                           vget_low_u8(vextq_u8(lo, lo, 5)));
    uint8x8_t b = luma_tap(vget_low_u8(vextq_u8(hi, hi, 3)),
                           vget_low_u8(vextq_u8(hi, hi, 4)),
                           vget_low_u8(vextq_u8(hi, hi, 5)),
                           vget_low_u8(vextq_u8(hi, hi, 6)),
                           vget_low_u8(vextq_u8(hi, hi, 7)),
                           vget_low_u8(vextq_u8(hi, hi, 8)));

    return vcombine_u8(a, b);
}

void bink2_mc_luma_block_neon(unsigned char *dst, int dst_stride,
                              const unsigned char *src, int ss,
                              int hmode, int vmode)
{
    uint8x16_t rows[21];
    int i, j;

    if (hmode == 0 && vmode == 0) {
        for (j = 0; j < 16; j++)
            vst1q_u8(dst + (long)j * dst_stride,
                     vld1q_u8(src + (long)j * ss));
        return;
    }
    if (vmode == 0) {
        for (j = 0; j < 16; j++)
            vst1q_u8(dst + (long)j * dst_stride,
                     luma_hrow(src + (long)j * ss));
        return;
    }
    /* rows[i] is source row i - 2, filtered horizontally when hmode. */
    for (i = 0; i < 21; i++) {
        const unsigned char *row = src + (long)(i - 2) * ss;

        rows[i] = hmode ? luma_hrow(row) : vld1q_u8(row);
    }
    for (j = 0; j < 16; j++)
        vst1q_u8(dst + (long)j * dst_stride, luma_tap16(rows + j));
}

/* Chroma quarter-pel filter: (w0 * a + w1 * b + 4) >> 3 with w1 = 2 * mode
 * and w0 = 8 - w1. Mode 2's (a + b + 1) >> 1 is the same value. */
static uint8x8_t chroma_tap(uint8x8_t a, uint8x8_t b, int mode)
{
    uint16x8_t x = vmull_u8(a, vdup_n_u8((uint8_t)(8 - 2 * mode)));

    x = vmlal_u8(x, b, vdup_n_u8((uint8_t)(2 * mode)));
    return vrshrn_n_u16(x, 3);
}

void bink2_mc_chroma_block_neon(unsigned char *dst, int dst_stride,
                                const unsigned char *src, int ss,
                                int hmode, int vmode)
{
    uint8x8_t rows[9];
    int i, j;

    if (hmode == 0 && vmode == 0) {
        for (j = 0; j < 8; j++)
            vst1_u8(dst + (long)j * dst_stride, vld1_u8(src + (long)j * ss));
        return;
    }
    if (vmode == 0) {
        for (j = 0; j < 8; j++) {
            const unsigned char *row = src + (long)j * ss;

            vst1_u8(dst + (long)j * dst_stride,
                    chroma_tap(vld1_u8(row), vld1_u8(row + 1), hmode));
        }
        return;
    }
    for (i = 0; i < 9; i++) {
        const unsigned char *row = src + (long)i * ss;

        rows[i] = hmode ? chroma_tap(vld1_u8(row), vld1_u8(row + 1), hmode)
                        : vld1_u8(row);
    }
    for (j = 0; j < 8; j++)
        vst1_u8(dst + (long)j * dst_stride,
                chroma_tap(rows[j], rows[j + 1], vmode));
}

#if defined(BINK2_SIMD_FLOAT_BLOCK)
/* Eight floats rounded half to even (lrintf in the default mode) and
 * saturated to int16. */
static int16x8_t round8(const float *p)
{
    return vcombine_s16(vqmovn_s32(vcvtnq_s32_f32(vld1q_f32(p))),
                        vqmovn_s32(vcvtnq_s32_f32(vld1q_f32(p + 4))));
}

void bink2_put_block_f_neon(unsigned char *dst, int stride,
                            const float block[64])
{
    int i;

    for (i = 0; i < 8; i++)
        vst1_u8(dst + (long)i * stride, vqmovun_s16(round8(block + 8 * i)));
}

void bink2_add_block_f_neon(unsigned char *dst, int stride,
                            const float block[64])
{
    int i;

    for (i = 0; i < 8; i++) {
        unsigned char *d = dst + (long)i * stride;
        int16x8_t px = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(d)));

        vst1_u8(d, vqmovun_s16(vqaddq_s16(px, round8(block + 8 * i))));
    }
}
#endif

#else
typedef int bink2_dsp_neon_unused;
#endif
