/* SSE2 versions of the DSP routines. Each matches its scalar reference in
 * bink2_dsp.c bit for bit; tests/test_dsp_simd.c checks that. */
#include "bink2_dsp.h"

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

#else
typedef int bink2_dsp_sse2_unused;
#endif
