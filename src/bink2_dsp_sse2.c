/* SSE2 versions of the DSP routines. Each matches its scalar reference in
 * bink2_dsp.c bit for bit; tests/test_dsp_simd.c checks that. */
#include "bink2_dsp.h"

#include <stddef.h>

#if defined(BINK2_SIMD_SSE2)

#include <emmintrin.h>

/* The integer IDCT's multiplies, on four 32-bit lanes. */
#define MUL_A(x) _mm_add_epi32((x), _mm_srai_epi32((x), 2))
#define MUL_B(x) _mm_srai_epi32((x), 1)
#define MUL_C(x) _mm_sub_epi32(_mm_sub_epi32((x), _mm_srai_epi32((x), 2)), \
                               _mm_srai_epi32((x), 4))
#define MUL_D(x) _mm_sub_epi32(_mm_add_epi32((x), _mm_srai_epi32((x), 2)), \
                               _mm_srai_epi32((x), 4))
#define MUL_E(x) _mm_srai_epi32((x), 2)

/* One 1-D IDCT on four lanes: b[k] holds coefficient k of each lane. */
static void idct8(const __m128i b[8], __m128i o[8])
{
    __m128i t00 = _mm_add_epi32(b[3], b[5]);
    __m128i t01 = _mm_sub_epi32(b[3], b[5]);
    __m128i t02 = _mm_add_epi32(MUL_A(b[2]), MUL_B(b[6]));
    __m128i t03 = _mm_sub_epi32(MUL_B(b[2]), MUL_A(b[6]));
    __m128i s04 = _mm_add_epi32(b[0], b[4]);
    __m128i t0 = _mm_add_epi32(s04, t02);
    __m128i t1 = _mm_sub_epi32(s04, t02);
    __m128i t2 = _mm_sub_epi32(b[0], b[4]);
    __m128i t3 = _mm_add_epi32(b[1], t00);
    __m128i t4 = _mm_sub_epi32(b[1], t00);
    __m128i t5 = _mm_add_epi32(t01, b[7]);
    __m128i t6 = _mm_sub_epi32(t01, b[7]);
    __m128i t7 = _mm_add_epi32(t4, MUL_C(t6));
    __m128i t8 = _mm_sub_epi32(MUL_C(t4), t6);
    __m128i t9 = _mm_add_epi32(MUL_D(t3), MUL_E(t5));
    __m128i t10 = _mm_sub_epi32(MUL_E(t3), MUL_D(t5));
    __m128i t11 = _mm_add_epi32(t2, t03);
    __m128i t12 = _mm_sub_epi32(t2, t03);

    o[0] = _mm_add_epi32(t0, t9);
    o[1] = _mm_add_epi32(t11, t7);
    o[2] = _mm_add_epi32(t12, t8);
    o[3] = _mm_add_epi32(t1, t10);
    o[4] = _mm_sub_epi32(t1, t10);
    o[5] = _mm_sub_epi32(t12, t8);
    o[6] = _mm_sub_epi32(t11, t7);
    o[7] = _mm_sub_epi32(t0, t9);
}

static void transpose8(__m128i r[8])
{
    __m128i a0 = _mm_unpacklo_epi16(r[0], r[1]);
    __m128i a1 = _mm_unpackhi_epi16(r[0], r[1]);
    __m128i a2 = _mm_unpacklo_epi16(r[2], r[3]);
    __m128i a3 = _mm_unpackhi_epi16(r[2], r[3]);
    __m128i a4 = _mm_unpacklo_epi16(r[4], r[5]);
    __m128i a5 = _mm_unpackhi_epi16(r[4], r[5]);
    __m128i a6 = _mm_unpacklo_epi16(r[6], r[7]);
    __m128i a7 = _mm_unpackhi_epi16(r[6], r[7]);
    __m128i b0 = _mm_unpacklo_epi32(a0, a2);
    __m128i b1 = _mm_unpackhi_epi32(a0, a2);
    __m128i b2 = _mm_unpacklo_epi32(a1, a3);
    __m128i b3 = _mm_unpackhi_epi32(a1, a3);
    __m128i b4 = _mm_unpacklo_epi32(a4, a6);
    __m128i b5 = _mm_unpackhi_epi32(a4, a6);
    __m128i b6 = _mm_unpacklo_epi32(a5, a7);
    __m128i b7 = _mm_unpackhi_epi32(a5, a7);

    r[0] = _mm_unpacklo_epi64(b0, b4);
    r[1] = _mm_unpackhi_epi64(b0, b4);
    r[2] = _mm_unpacklo_epi64(b1, b5);
    r[3] = _mm_unpackhi_epi64(b1, b5);
    r[4] = _mm_unpacklo_epi64(b2, b6);
    r[5] = _mm_unpackhi_epi64(b2, b6);
    r[6] = _mm_unpacklo_epi64(b3, b7);
    r[7] = _mm_unpackhi_epi64(b3, b7);
}

/* Sign-extends the low and high four int16 lanes to int32. */
static __m128i widen_lo(__m128i x)
{
    return _mm_srai_epi32(_mm_unpacklo_epi16(x, x), 16);
}

static __m128i widen_hi(__m128i x)
{
    return _mm_srai_epi32(_mm_unpackhi_epi16(x, x), 16);
}

/* Truncates eight int32 lanes to int16, like a C cast; SSE2 only has a
 * saturating pack, so sign-extend the low halves first. */
static __m128i narrow_trunc(__m128i lo, __m128i hi)
{
    lo = _mm_srai_epi32(_mm_slli_epi32(lo, 16), 16);
    hi = _mm_srai_epi32(_mm_slli_epi32(hi, 16), 16);
    return _mm_packs_epi32(lo, hi);
}

static void idct_pass(__m128i r[8], int shift)
{
    __m128i lo[8], hi[8], olo[8], ohi[8];
    int k;

    for (k = 0; k < 8; k++) {
        lo[k] = widen_lo(r[k]);
        hi[k] = widen_hi(r[k]);
    }
    idct8(lo, olo);
    idct8(hi, ohi);
    for (k = 0; k < 8; k++) {
        if (shift) {
            olo[k] = _mm_srai_epi32(olo[k], 6);
            ohi[k] = _mm_srai_epi32(ohi[k], 6);
        }
        r[k] = narrow_trunc(olo[k], ohi[k]);
    }
}

static __m128i load4(const int32_t *p)
{
    return _mm_loadu_si128((const __m128i *)(const void *)p);
}

void bink2_idct_new_sse2(int32_t block[64])
{
    __m128i r[8];
    int k;

    for (k = 0; k < 8; k++)
        r[k] = narrow_trunc(load4(block + 8 * k), load4(block + 8 * k + 4));
    idct_pass(r, 0);
    transpose8(r);
    idct_pass(r, 1);
    transpose8(r);
    for (k = 0; k < 8; k++) {
        _mm_storeu_si128((__m128i *)(void *)(block + 8 * k), widen_lo(r[k]));
        _mm_storeu_si128((__m128i *)(void *)(block + 8 * k + 4),
                         widen_hi(r[k]));
    }
}

/* block[j * 8 + i] is output row i, column j. Saturating to int16 first
 * gives the same result as clamping the int32 to 0..255. */
static void load_transposed(__m128i r[8], const int32_t block[64])
{
    int k;

    for (k = 0; k < 8; k++)
        r[k] = _mm_packs_epi32(load4(block + 8 * k), load4(block + 8 * k + 4));
    transpose8(r);
}

void bink2_put_block_sse2(unsigned char *dst, int stride,
                          const int32_t block[64])
{
    __m128i r[8];
    int i;

    load_transposed(r, block);
    for (i = 0; i < 8; i++)
        _mm_storel_epi64((__m128i *)(void *)(dst + (long)i * stride),
                         _mm_packus_epi16(r[i], r[i]));
}

void bink2_add_block_sse2(unsigned char *dst, int stride,
                          const int32_t block[64])
{
    const __m128i zero = _mm_setzero_si128();
    __m128i r[8];
    int i;

    load_transposed(r, block);
    for (i = 0; i < 8; i++) {
        unsigned char *d = dst + (long)i * stride;
        __m128i px = _mm_unpacklo_epi8(
            _mm_loadl_epi64((const __m128i *)(const void *)d), zero);

        px = _mm_adds_epi16(px, r[i]);
        _mm_storel_epi64((__m128i *)(void *)d, _mm_packus_epi16(px, px));
    }
}

static __m128i load8(const unsigned char *p)
{
    return _mm_loadl_epi64((const __m128i *)(const void *)p);
}

/* Eight samples at p, zero-extended to 16-bit lanes. */
static __m128i load8w(const unsigned char *p)
{
    return _mm_unpacklo_epi8(load8(p), _mm_setzero_si128());
}

/* Luma 6-tap half-pel filter on eight 16-bit lanes, taps at -2..3:
 * ((((p0 + p1) * 19) >> 1) - (m1 + p2) * 2 + ((m2 + p3) >> 1) + 8) >> 4.
 * Every intermediate fits in int16; the caller packs with saturation. */
static __m128i luma_tap(__m128i m2, __m128i m1, __m128i p0, __m128i p1,
                        __m128i p2, __m128i p3)
{
    __m128i a = _mm_add_epi16(p0, p1);
    __m128i b = _mm_add_epi16(m1, p2);
    __m128i c = _mm_add_epi16(m2, p3);
    __m128i r = _mm_srai_epi16(_mm_mullo_epi16(a, _mm_set1_epi16(19)), 1);

    r = _mm_sub_epi16(r, _mm_slli_epi16(b, 1));
    r = _mm_add_epi16(r, _mm_srai_epi16(c, 1));
    r = _mm_add_epi16(r, _mm_set1_epi16(8));
    return _mm_srai_epi16(r, 4);
}

/* Horizontal filter of one 16-sample row. Reads s[-2..18] only. */
static __m128i luma_hrow(const unsigned char *s)
{
    __m128i lo = luma_tap(load8w(s - 2), load8w(s - 1), load8w(s),
                          load8w(s + 1), load8w(s + 2), load8w(s + 3));
    __m128i hi = luma_tap(load8w(s + 6), load8w(s + 7), load8w(s + 8),
                          load8w(s + 9), load8w(s + 10), load8w(s + 11));

    return _mm_packus_epi16(lo, hi);
}

static __m128i load16(const unsigned char *p)
{
    return _mm_loadu_si128((const __m128i *)(const void *)p);
}

static void store16(unsigned char *p, __m128i v)
{
    _mm_storeu_si128((__m128i *)(void *)p, v);
}

static __m128i luma_tap16(const __m128i r[6])
{
    const __m128i zero = _mm_setzero_si128();
    __m128i lo = luma_tap(_mm_unpacklo_epi8(r[0], zero),
                          _mm_unpacklo_epi8(r[1], zero),
                          _mm_unpacklo_epi8(r[2], zero),
                          _mm_unpacklo_epi8(r[3], zero),
                          _mm_unpacklo_epi8(r[4], zero),
                          _mm_unpacklo_epi8(r[5], zero));
    __m128i hi = luma_tap(_mm_unpackhi_epi8(r[0], zero),
                          _mm_unpackhi_epi8(r[1], zero),
                          _mm_unpackhi_epi8(r[2], zero),
                          _mm_unpackhi_epi8(r[3], zero),
                          _mm_unpackhi_epi8(r[4], zero),
                          _mm_unpackhi_epi8(r[5], zero));

    return _mm_packus_epi16(lo, hi);
}

void bink2_mc_luma_block_sse2(unsigned char *dst, int dst_stride,
                              const unsigned char *src, int ss,
                              int hmode, int vmode)
{
    __m128i rows[21];
    int i, j;

    if (hmode == 0 && vmode == 0) {
        for (j = 0; j < 16; j++)
            store16(dst + (long)j * dst_stride, load16(src + (long)j * ss));
        return;
    }
    if (vmode == 0) {
        for (j = 0; j < 16; j++)
            store16(dst + (long)j * dst_stride,
                    luma_hrow(src + (long)j * ss));
        return;
    }
    /* rows[i] is source row i - 2, filtered horizontally when hmode. */
    for (i = 0; i < 21; i++) {
        const unsigned char *row = src + (long)(i - 2) * ss;

        rows[i] = hmode ? luma_hrow(row) : load16(row);
    }
    for (j = 0; j < 16; j++)
        store16(dst + (long)j * dst_stride, luma_tap16(rows + j));
}

/* Chroma quarter-pel filter on 16-bit lanes: (w0 * a + w1 * b + 4) >> 3
 * with w1 = 2 * mode and w0 = 8 - w1. Mode 2's (a + b + 1) >> 1 is the
 * same value. */
static __m128i chroma_tap(__m128i a, __m128i b, int mode)
{
    __m128i x = _mm_mullo_epi16(a, _mm_set1_epi16((short)(8 - 2 * mode)));

    x = _mm_add_epi16(x, _mm_mullo_epi16(b, _mm_set1_epi16((short)(2 * mode))));
    return _mm_srli_epi16(_mm_add_epi16(x, _mm_set1_epi16(4)), 3);
}

static void store8w(unsigned char *p, __m128i v)
{
    _mm_storel_epi64((__m128i *)(void *)p, _mm_packus_epi16(v, v));
}

void bink2_mc_chroma_block_sse2(unsigned char *dst, int dst_stride,
                                const unsigned char *src, int ss,
                                int hmode, int vmode)
{
    __m128i rows[9];
    int i, j;

    if (hmode == 0 && vmode == 0) {
        for (j = 0; j < 8; j++)
            store8w(dst + (long)j * dst_stride, load8w(src + (long)j * ss));
        return;
    }
    if (vmode == 0) {
        for (j = 0; j < 8; j++) {
            const unsigned char *row = src + (long)j * ss;

            store8w(dst + (long)j * dst_stride,
                    chroma_tap(load8w(row), load8w(row + 1), hmode));
        }
        return;
    }
    for (i = 0; i < 9; i++) {
        const unsigned char *row = src + (long)i * ss;

        rows[i] = hmode ? chroma_tap(load8w(row), load8w(row + 1), hmode)
                        : load8w(row);
    }
    for (j = 0; j < 8; j++)
        store8w(dst + (long)j * dst_stride,
                chroma_tap(rows[j], rows[j + 1], vmode));
}

/* (a * ca + b * cb) for four pixels: lanes 0..3 or 4..7 of a and b, via
 * the pairwise multiply-add, since SSE2 has no 32-bit multiply. */
static __m128i madd_lo(__m128i a, __m128i b, __m128i coef)
{
    return _mm_madd_epi16(_mm_unpacklo_epi16(a, b), coef);
}

static __m128i madd_hi(__m128i a, __m128i b, __m128i coef)
{
    return _mm_madd_epi16(_mm_unpackhi_epi16(a, b), coef);
}

static __m128i pair(short ca, short cb)
{
    return _mm_set_epi16(cb, ca, cb, ca, cb, ca, cb, ca);
}

/* Eight pixels: y in 16-bit lanes, u and v - 128 in 16-bit lanes. The
 * formulas are bink2_yuv_row_c's, term for term. */
static void yuv8(__m128i y, __m128i u, __m128i v, int studio,
                 __m128i *r, __m128i *g, __m128i *b)
{
    const __m128i one = _mm_set1_epi16(1);
    const __m128i half = _mm_set1_epi32(128);
    __m128i rl, rh, gl, gh, bl, bh;

    if (!studio) {
        __m128i yl = madd_lo(y, one, pair(1, 0));
        __m128i yh = madd_hi(y, one, pair(1, 0));

        rl = _mm_add_epi32(yl, _mm_srai_epi32(madd_lo(v, one, pair(359, 128)),
                                              8));
        rh = _mm_add_epi32(yh, _mm_srai_epi32(madd_hi(v, one, pair(359, 128)),
                                              8));
        gl = _mm_sub_epi32(yl, _mm_srai_epi32(_mm_add_epi32(
                 madd_lo(u, v, pair(88, 183)), half), 8));
        gh = _mm_sub_epi32(yh, _mm_srai_epi32(_mm_add_epi32(
                 madd_hi(u, v, pair(88, 183)), half), 8));
        bl = _mm_add_epi32(yl, _mm_srai_epi32(madd_lo(u, one, pair(454, 128)),
                                              8));
        bh = _mm_add_epi32(yh, _mm_srai_epi32(madd_hi(u, one, pair(454, 128)),
                                              8));
    } else {
        __m128i sy = _mm_sub_epi16(y, _mm_set1_epi16(16));

        rl = _mm_srai_epi32(_mm_add_epi32(madd_lo(sy, v, pair(298, 409)),
                                          half), 8);
        rh = _mm_srai_epi32(_mm_add_epi32(madd_hi(sy, v, pair(298, 409)),
                                          half), 8);
        gl = _mm_srai_epi32(_mm_add_epi32(madd_lo(sy, u, pair(298, -100)),
                                          madd_lo(v, one, pair(-208, 128))),
                            8);
        gh = _mm_srai_epi32(_mm_add_epi32(madd_hi(sy, u, pair(298, -100)),
                                          madd_hi(v, one, pair(-208, 128))),
                            8);
        bl = _mm_srai_epi32(_mm_add_epi32(madd_lo(sy, u, pair(298, 516)),
                                          half), 8);
        bh = _mm_srai_epi32(_mm_add_epi32(madd_hi(sy, u, pair(298, 516)),
                                          half), 8);
    }
    *r = _mm_packs_epi32(rl, rh);
    *g = _mm_packs_epi32(gl, gh);
    *b = _mm_packs_epi32(bl, bh);
}

int bink2_yuv_row16_sse2(unsigned char *rgb, const unsigned char *y,
                         const unsigned char *u, const unsigned char *v,
                         int width, int studio_range)
{
    const __m128i zero = _mm_setzero_si128();
    const __m128i bias = _mm_set1_epi16(128);
    int x;

    for (x = 0; x + 16 <= width; x += 16) {
        __m128i ly = load16(y + x);
        __m128i u8 = load8(u + x / 2);
        __m128i v8 = load8(v + x / 2);
        __m128i du = _mm_unpacklo_epi8(u8, u8);
        __m128i dv = _mm_unpacklo_epi8(v8, v8);
        unsigned char r[16], g[16], b[16];
        __m128i r0, g0, b0, r1, g1, b1;
        unsigned char *out = rgb + (size_t)x * 3;
        int i;

        yuv8(_mm_unpacklo_epi8(ly, zero),
             _mm_sub_epi16(_mm_unpacklo_epi8(du, zero), bias),
             _mm_sub_epi16(_mm_unpacklo_epi8(dv, zero), bias),
             studio_range, &r0, &g0, &b0);
        yuv8(_mm_unpackhi_epi8(ly, zero),
             _mm_sub_epi16(_mm_unpackhi_epi8(du, zero), bias),
             _mm_sub_epi16(_mm_unpackhi_epi8(dv, zero), bias),
             studio_range, &r1, &g1, &b1);
        store16(r, _mm_packus_epi16(r0, r1));
        store16(g, _mm_packus_epi16(g0, g1));
        store16(b, _mm_packus_epi16(b0, b1));
        for (i = 0; i < 16; i++) {
            out[i * 3 + 0] = r[i];
            out[i * 3 + 1] = g[i];
            out[i * 3 + 2] = b[i];
        }
    }
    return x;
}

/* Eight floats rounded in the current mode (half to even by default, as
 * lrintf) and saturated to int16. */
static __m128i round8(const float *p)
{
    return _mm_packs_epi32(_mm_cvtps_epi32(_mm_loadu_ps(p)),
                           _mm_cvtps_epi32(_mm_loadu_ps(p + 4)));
}

void bink2_put_block_f_sse2(unsigned char *dst, int stride,
                            const float block[64])
{
    int i;

    for (i = 0; i < 8; i++) {
        __m128i r = round8(block + 8 * i);

        _mm_storel_epi64((__m128i *)(void *)(dst + (long)i * stride),
                         _mm_packus_epi16(r, r));
    }
}

void bink2_add_block_f_sse2(unsigned char *dst, int stride,
                            const float block[64])
{
    const __m128i zero = _mm_setzero_si128();
    int i;

    for (i = 0; i < 8; i++) {
        unsigned char *d = dst + (long)i * stride;
        __m128i px = _mm_unpacklo_epi8(
            _mm_loadl_epi64((const __m128i *)(const void *)d), zero);

        px = _mm_adds_epi16(px, round8(block + 8 * i));
        _mm_storel_epi64((__m128i *)(void *)d, _mm_packus_epi16(px, px));
    }
}

#else
typedef int bink2_dsp_sse2_unused;
#endif
