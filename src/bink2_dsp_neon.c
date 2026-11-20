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

#else
typedef int bink2_dsp_neon_unused;
#endif
