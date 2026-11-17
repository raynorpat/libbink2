/**
 * Bink Audio decoder
 * Copyright (c) 2007-2011 Peter Ross (pross@xvid.org)
 * Copyright (c) 2009 Daniel Verkamp (daniel@drv.nu)
 *
 * This file is part of libbink, derived from FFmpeg's libavcodec/binkaudio.c.
 * It is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "bink2_audio.h"
#include "bink2_bits.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Copied unchanged from FFmpeg libavcodec/wma_freqs.c. */
static const unsigned short bink_wma_critical_freqs[25] = {
      100,   200,  300,  400,  510,  630,   770,   920,
     1080,  1270, 1480, 1720, 2000, 2320,  2700,  3150,
     3700,  4400, 5300, 6400, 7700, 9500, 12000, 15500,
    24500
};

static const unsigned char bink_rle_length_tab[16] = {
    2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64
};

#define BINK2_AUDIO_MAX_FRAME 4096

struct Bink2Audio {
    unsigned long rate;
    int channels;
    int use_dct;
    int version_2;
    unsigned long max_bytes;
    int first_block;
    int frame_len;
    int overlap_len;
    int num_bands;
    unsigned int bands[26];
    float root;
    float quant_table[96];
    float *previous;
    float *block;
    /* Grown on demand up to max_bytes: the header's figure is only a cap,
     * so a hostile file cannot make the decoder reserve it up front. */
    unsigned char *pcm;
    unsigned long pcm_cap;
    unsigned long pcm_bytes;
    double *fft_re;
    double *fft_im;
    /* Rotation factors, fixed by frame_len and computed once. Each entry
     * is the float the transform used to compute per sample, so the
     * output is unchanged. */
    float *rdft_cos; /* [i] = cos(2*pi*i/n), i < n/4 */
    float *rdft_sin;
    float *dct_cos;  /* [i/2] = cos(pi*i/(2n)), even i < n */
    float *dct_sin;
    float *dct_csc;  /* [i] = 0.5/sin(pi*(2i+1)/(2n)), i < n/2 */
    /* Overlap saved at the start of a packet, restored if it fails. */
    float *saved_prev;
};

/* Decimation-in-time radix-2. inverse=0 is the forward sign. No 1/n. */
static void fft_transform(double *re, double *im, int n, int inverse)
{
    int i;
    int len;

    for (i = 1, len = 0; i < n; i++) {
        int bit = n >> 1;

        for (; len & bit; bit >>= 1)
            len ^= bit;
        len ^= bit;
        if (i < len) {
            double tr = re[i];
            double ti = im[i];

            re[i] = re[len];
            im[i] = im[len];
            re[len] = tr;
            im[len] = ti;
        }
    }

    for (len = 2; len <= n; len <<= 1) {
        double ang = (inverse ? 2.0 : -2.0) * M_PI / (double)len;
        double wlen_re = cos(ang);
        double wlen_im = sin(ang);
        int start;

        for (start = 0; start < n; start += len) {
            double w_re = 1.0;
            double w_im = 0.0;
            int half = len >> 1;
            int k;

            for (k = 0; k < half; k++) {
                int u = start + k;
                int v = u + half;
                double vr = re[v] * w_re - im[v] * w_im;
                double vi = re[v] * w_im + im[v] * w_re;
                double next_re = w_re * wlen_re - w_im * wlen_im;

                re[v] = re[u] - vr;
                im[v] = im[u] - vi;
                re[u] += vr;
                im[u] += vi;
                w_im = w_re * wlen_im + w_im * wlen_re;
                w_re = next_re;
            }
        }
    }
}

/*
 * Inverse real DFT, IDFT_C2R, matching FFmpeg 4.4 rdft_calc_c for the
 * transform DCT-III builds on. n is the number of real samples.
 */
static void rdft_idft_c2r(float *data, int n, double *re, double *im,
                          const float *tcos, const float *tsin)
{
    int i;
    int n4 = n >> 2;
    int fft_n = n >> 1;
    const float k1 = 0.5f;
    const float k2 = -0.5f;
    float ev_re = data[0];

    data[0] = ev_re + data[1];
    data[1] = ev_re - data[1];

    for (i = 1; i < n4; i++) {
        int i1 = 2 * i;
        int i2 = n - i1;
        float od_im = k2 * (data[i2] - data[i1]);
        float ev_im = k1 * (data[i1 + 1] - data[i2 + 1]);
        float od_re = k2 * (data[i1 + 1] + data[i2 + 1]);
        float tc = tcos[i];
        /* ff_cos_tabs mirrors so tsin[i] = sin(2*pi*i/n), not -sin. */
        float ts = tsin[i];
        float sum_re;
        float sum_im;

        ev_re = k1 * (data[i1] + data[i2]);
        sum_re = od_re * tc - od_im * ts;
        sum_im = od_im * tc + od_re * ts;
        data[i1] = ev_re + sum_re;
        data[i1 + 1] = ev_im + sum_im;
        data[i2] = ev_re - sum_re;
        data[i2 + 1] = sum_im - ev_im;
    }

    data[n / 2 + 1] = -data[n / 2 + 1];
    data[0] *= k1;
    data[1] *= k1;

    for (i = 0; i < fft_n; i++) {
        re[i] = (double)data[2 * i];
        im[i] = (double)data[2 * i + 1];
    }
    fft_transform(re, im, fft_n, 1);
    for (i = 0; i < fft_n; i++) {
        data[2 * i] = (float)re[i];
        data[2 * i + 1] = (float)im[i];
    }
}

/*
 * DFT_C2R, the transform FFmpeg 4.4 runs on a Bink RDFT block.
 * inverse unmangle with negative_sin, sign convention +1, forward FFT.
 */
static void rdft_dft_c2r(float *data, int n, double *re, double *im,
                         const float *tcos, const float *tsin)
{
    int i;
    int n4 = n >> 2;
    int fft_n = n >> 1;
    const float k1 = 0.5f;
    const float k2 = -0.5f;
    float ev_re = data[0];

    data[0] = ev_re + data[1];
    data[1] = ev_re - data[1];

    for (i = 1; i < n4; i++) {
        int i1 = 2 * i;
        int i2 = n - i1;
        float od_im = k2 * (data[i2] - data[i1]);
        float ev_im = k1 * (data[i1 + 1] - data[i2 + 1]);
        float od_re = k2 * (data[i1 + 1] + data[i2 + 1]);
        float tc = tcos[i];
        float ts = tsin[i];
        float sum_re;
        float sum_im;

        ev_re = k1 * (data[i1] + data[i2]);
        sum_re = od_re * tc + od_im * ts;
        sum_im = od_im * tc - od_re * ts;
        data[i1] = ev_re + sum_re;
        data[i1 + 1] = ev_im + sum_im;
        data[i2] = ev_re - sum_re;
        data[i2 + 1] = sum_im - ev_im;
    }

    data[0] *= k1;
    data[1] *= k1;

    for (i = 0; i < fft_n; i++) {
        re[i] = (double)data[2 * i];
        im[i] = (double)data[2 * i + 1];
    }
    fft_transform(re, im, fft_n, 0);
    for (i = 0; i < fft_n; i++) {
        data[2 * i] = (float)re[i];
        data[2 * i + 1] = (float)im[i];
    }
}

/* FFmpeg dct_calc_III. n is the full frame length. src is consumed. */
static void dct_iii(float *dst, float *src, int n, const Bink2Audio *audio)
{
    int i;
    float next = src[n - 1];
    float inv_n = 1.0f / (float)n;

    for (i = n - 2; i >= 2; i -= 2) {
        float val1 = src[i];
        float val2 = src[i - 1] - src[i + 1];
        float c = audio->dct_cos[i >> 1];
        float s = audio->dct_sin[i >> 1];

        src[i] = c * val1 + s * val2;
        src[i + 1] = s * val1 - c * val2;
    }
    src[1] = 2.0f * next;

    rdft_idft_c2r(src, n, audio->fft_re, audio->fft_im, audio->rdft_cos,
                  audio->rdft_sin);

    for (i = 0; i < n / 2; i++) {
        float tmp1 = src[i] * inv_n;
        float tmp2 = src[n - i - 1] * inv_n;
        float csc = audio->dct_csc[i] * (tmp1 - tmp2);

        tmp1 += tmp2;
        dst[i] = tmp1 + csc;
        dst[n - i - 1] = tmp1 - csc;
    }
}

/* Samples are in the unit interval. FFmpeg's C converter is lrintf(x * 32768). */
static int16_t float_to_s16(float x)
{
    float scaled = x * 32768.0f;

    /* Clamp before rounding: lrintf is undefined for a value that does
     * not fit a long, which a corrupt stream can produce where long is
     * 32 bits. NaN fails both comparisons and becomes silence. */
    if (scaled >= 32767.0f)
        return 32767;
    if (scaled <= -32768.0f)
        return -32768;
    if (scaled != scaled)
        return 0;
    return (int16_t)lrintf(scaled);
}

static int read_bits(Bink2Bits *bits, int count, int *err)
{
    int value;

    if (*err)
        return 0;
    value = bink2_bits_read(bits, count);
    if (value < 0)
        return 0;
    return value;
}

static float read_float(Bink2Bits *bits, int *err)
{
    int power = read_bits(bits, 5, err);
    int mantissa = read_bits(bits, 23, err);
    int sign = read_bits(bits, 1, err);
    float value = ldexpf((float)mantissa, power - 23);

    if (sign)
        value = -value;
    return value;
}

static int align32_exact(Bink2Bits *bits)
{
    size_t total = bits->size * 8;
    size_t next = (bits->bitpos + 31) & ~(size_t)31;

    if (next > total)
        return -1;
    bits->bitpos = next;
    return 0;
}

static void setup_bands(Bink2AudioInfo *info)
{
    unsigned long half = (info->rate + 1UL) / 2UL;
    int i;

    info->num_bands = 1;
    while (info->num_bands < 25) {
        if (half <= bink_wma_critical_freqs[info->num_bands - 1])
            break;
        info->num_bands++;
    }
    info->bands[0] = 2;
    for (i = 1; i < info->num_bands; i++)
        info->bands[i] =
            (bink_wma_critical_freqs[i - 1] * (unsigned)info->frame_len /
             (unsigned)half) & ~1U;
    info->bands[info->num_bands] = (unsigned)info->frame_len;
}

int bink2_audio_describe(unsigned long rate, int channels, int use_dct,
                         int version_2, Bink2AudioInfo *info)
{
    int bits;
    int bits_extra = 0;
    int i;

    if (info == NULL || channels < 1 || channels > 2)
        return -1;
    memset(info, 0, sizeof(*info));
    info->use_dct = use_dct;
    info->version_2 = version_2;
    if (!use_dct) {
        /* Bands see rate * channels. The transform itself is one channel. */
        info->rate = rate * (unsigned long)channels;
        if (channels == 2)
            bits_extra = 1;
        info->channels = 1;
    } else {
        info->rate = rate;
        info->channels = channels;
    }

    if (rate < 22050)
        bits = 9;
    else if (rate < 44100)
        bits = 10;
    else
        bits = 11;
    bits += bits_extra;
    info->frame_len = 1 << bits;
    if (info->frame_len > BINK2_AUDIO_MAX_FRAME)
        return -1;
    info->overlap_len = info->frame_len / 16;
    if (use_dct)
        info->root = (float)((double)info->frame_len /
                             (sqrt((double)info->frame_len) * 32768.0));
    else
        info->root = (float)(2.0 / (sqrt((double)info->frame_len) * 32768.0));
    for (i = 0; i < 96; i++)
        info->quant[i] =
            expf((float)i * 0.15289164787221953823f) * info->root;
    setup_bands(info);
    return 0;
}

int bink2_audio_create(Bink2Audio **out, unsigned long rate, int channels,
                       int use_dct, int version_2, unsigned long max_bytes)
{
    Bink2Audio *audio;
    Bink2AudioInfo info;

    if (out == NULL ||
        bink2_audio_describe(rate, channels, use_dct, version_2, &info) != 0)
        return -1;
    audio = (Bink2Audio *)calloc(1, sizeof(*audio));
    if (audio == NULL)
        return -1;
    audio->use_dct = info.use_dct;
    audio->version_2 = info.version_2;
    audio->max_bytes = max_bytes;
    audio->rate = info.rate;
    audio->channels = info.channels;
    audio->frame_len = info.frame_len;
    audio->overlap_len = info.overlap_len;
    audio->num_bands = info.num_bands;
    audio->root = info.root;
    memcpy(audio->quant_table, info.quant, sizeof(audio->quant_table));
    memcpy(audio->bands, info.bands, sizeof(audio->bands));

    audio->previous = (float *)calloc(
        (size_t)audio->channels * (size_t)audio->overlap_len, sizeof(float));
    audio->block = (float *)calloc(
        (size_t)audio->channels * (size_t)audio->frame_len, sizeof(float));
    audio->fft_re = (double *)malloc((size_t)audio->frame_len * sizeof(double));
    audio->fft_im = (double *)malloc((size_t)audio->frame_len * sizeof(double));
    audio->saved_prev = (float *)malloc(
        (size_t)audio->channels * (size_t)audio->overlap_len * sizeof(float));
    {
        size_t quarter = (size_t)audio->frame_len / 4;
        size_t half = (size_t)audio->frame_len / 2;

        audio->rdft_cos = (float *)malloc(quarter * sizeof(float));
        audio->rdft_sin = (float *)malloc(quarter * sizeof(float));
        audio->dct_cos = (float *)malloc(half * sizeof(float));
        audio->dct_sin = (float *)malloc(half * sizeof(float));
        audio->dct_csc = (float *)malloc(half * sizeof(float));
    }
    if (audio->previous == NULL || audio->block == NULL ||
        audio->fft_re == NULL || audio->fft_im == NULL ||
        audio->saved_prev == NULL || audio->rdft_cos == NULL ||
        audio->rdft_sin == NULL || audio->dct_cos == NULL ||
        audio->dct_sin == NULL || audio->dct_csc == NULL) {
        bink2_audio_destroy(audio);
        return -1;
    }
    {
        int n = audio->frame_len;
        int i;

        for (i = 0; i < n / 4; i++) {
            double angle = 2.0 * M_PI * (double)i / (double)n;

            audio->rdft_cos[i] = (float)cos(angle);
            audio->rdft_sin[i] = (float)sin(angle);
        }
        for (i = 0; i < n; i += 2) {
            double angle = M_PI * (double)i / (double)(2 * n);

            audio->dct_cos[i >> 1] = (float)cos(angle);
            audio->dct_sin[i >> 1] = (float)sin(angle);
        }
        for (i = 0; i < n / 2; i++)
            audio->dct_csc[i] =
                0.5f / (float)sin(M_PI / (2.0 * n) * (double)(2 * i + 1));
    }
    bink2_audio_reset(audio);
    *out = audio;
    return 0;
}

void bink2_audio_reset(Bink2Audio *audio)
{
    if (audio == NULL)
        return;
    audio->first_block = 1;
    audio->pcm_bytes = 0;
    if (audio->previous != NULL)
        memset(audio->previous, 0,
               (size_t)audio->channels * (size_t)audio->overlap_len *
                   sizeof(float));
}

void bink2_audio_destroy(Bink2Audio *audio)
{
    if (audio == NULL)
        return;
    free(audio->previous);
    free(audio->block);
    free(audio->pcm);
    free(audio->fft_re);
    free(audio->fft_im);
    free(audio->rdft_cos);
    free(audio->rdft_sin);
    free(audio->dct_cos);
    free(audio->dct_sin);
    free(audio->dct_csc);
    free(audio->saved_prev);
    free(audio);
}

static int decode_dct_block(Bink2Audio *audio, Bink2Bits *bits, int16_t *dst,
                            int *err)
{
    float coeffs[BINK2_AUDIO_MAX_FRAME + 2];
    float quant[25];
    int ch;
    int block_out = audio->frame_len - audio->overlap_len;
    int count = audio->overlap_len * audio->channels;

    memset(coeffs, 0, sizeof(coeffs));
    if (audio->use_dct)
        read_bits(bits, 2, err);
    for (ch = 0; ch < audio->channels; ch++) {
        float *out = audio->block + ch * audio->frame_len;
        float *prev = audio->previous + ch * audio->overlap_len;
        int k = 0;
        float q;
        int i;

        coeffs[0] = read_float(bits, err) * audio->root;
        coeffs[1] = read_float(bits, err) * audio->root;
        for (i = 0; i < audio->num_bands; i++) {
            int value = read_bits(bits, audio->version_2 ? 7 : 8, err);

            if (value > 95)
                value = 95;
            quant[i] = audio->quant_table[value];
        }
        q = quant[0];
        i = 2;
        while (i < audio->frame_len && !*err) {
            int j;
            int width;

            if (read_bits(bits, 1, err))
                j = i + bink_rle_length_tab[read_bits(bits, 4, err)] * 8;
            else
                j = i + 8;
            if (j > audio->frame_len)
                j = audio->frame_len;
            width = read_bits(bits, 4, err);
            if (width == 0) {
                memset(coeffs + i, 0, (size_t)(j - i) * sizeof(float));
                i = j;
                while (k < audio->num_bands && (int)audio->bands[k] < i)
                    q = quant[k++];
            } else if (audio->version_2) {
                int m;

                for (m = i; m < j; m++)
                    coeffs[m] = (float)read_bits(bits, width, err);
                while (i < j && !*err) {
                    if (k < audio->num_bands && (int)audio->bands[k] == i)
                        q = quant[k++];
                    if (coeffs[i] > 0.0f) {
                        if (read_bits(bits, 1, err))
                            coeffs[i] *= -q;
                        else
                            coeffs[i] *= q;
                    }
                    i++;
                }
            } else {
                while (i < j && !*err) {
                    int coeff;

                    if (k < audio->num_bands && (int)audio->bands[k] == i)
                        q = quant[k++];
                    coeff = read_bits(bits, width, err);
                    if (coeff) {
                        if (read_bits(bits, 1, err))
                            coeffs[i] = -q * (float)coeff;
                        else
                            coeffs[i] = q * (float)coeff;
                    } else {
                        coeffs[i] = 0.0f;
                    }
                    i++;
                }
            }
        }
        if (*err)
            return -1;
        if (audio->use_dct) {
            coeffs[0] *= 2.0f;
            dct_iii(out, coeffs, audio->frame_len, audio);
        } else {
            memcpy(out, coeffs, (size_t)audio->frame_len * sizeof(float));
            rdft_dft_c2r(out, audio->frame_len, audio->fft_re, audio->fft_im,
                         audio->rdft_cos, audio->rdft_sin);
        }
        if (!audio->first_block) {
            int j = ch;

            for (i = 0; i < audio->overlap_len; i++, j += audio->channels)
                out[i] = (prev[i] * (float)(count - j) +
                          out[i] * (float)j) / (float)count;
        }
        memcpy(prev, out + audio->frame_len - audio->overlap_len,
               (size_t)audio->overlap_len * sizeof(float));
    }

    for (ch = 0; ch < block_out; ch++) {
        int c;

        for (c = 0; c < audio->channels; c++) {
            float sample = audio->block[c * audio->frame_len + ch];

            dst[ch * audio->channels + c] = float_to_s16(sample);
        }
    }
    audio->first_block = 0;
    return 0;
}

/* Make room for `need` bytes of PCM, never beyond max_bytes. */
static int pcm_reserve(Bink2Audio *audio, unsigned long need)
{
    unsigned long cap;
    unsigned char *grown;

    if (need > audio->max_bytes)
        return -1;
    if (need <= audio->pcm_cap)
        return 0;
    cap = audio->pcm_cap != 0 ? audio->pcm_cap : need;
    while (cap < need) {
        if (cap > audio->max_bytes / 2) {
            cap = audio->max_bytes;
            break;
        }
        cap *= 2;
    }
    grown = (unsigned char *)realloc(audio->pcm, (size_t)cap);
    if (grown == NULL)
        return -1;
    audio->pcm = grown;
    audio->pcm_cap = cap;
    return 0;
}

int bink2_audio_decode(Bink2Audio *audio, const unsigned char *data,
                      unsigned long size)
{
    Bink2Bits bits;
    float *saved_prev;
    unsigned long produced = 0;
    int saved_first;
    int block_bytes;
    int err = 0;

    if (audio == NULL)
        return -1;
    if (size == 0) {
        audio->pcm_bytes = 0;
        return 0;
    }

    saved_first = audio->first_block;
    saved_prev = audio->saved_prev;
    memcpy(saved_prev, audio->previous,
           (size_t)audio->channels * (size_t)audio->overlap_len * sizeof(float));

    bink2_bits_init(&bits, data, (size_t)size);
    if (bink2_bits_left(&bits) < 32)
        err = 1;
    else
        bink2_bits_skip(&bits, 32);

    block_bytes = (audio->frame_len - audio->overlap_len) * audio->channels *
                  (int)sizeof(int16_t);
    while (!err && bink2_bits_left(&bits) > 0) {
        if ((unsigned long)block_bytes > audio->max_bytes - produced ||
            pcm_reserve(audio, produced + (unsigned long)block_bytes) != 0) {
            err = 1;
            break;
        }
        if (decode_dct_block(audio, &bits,
                             (int16_t *)(audio->pcm + produced), &err) != 0) {
            err = 1;
            break;
        }
        produced += (unsigned long)block_bytes;
        if (bink2_bits_left(&bits) == 0)
            break;
        if (align32_exact(&bits) != 0)
            break;
    }

    if (err || produced == 0) {
        memcpy(audio->previous, saved_prev,
               (size_t)audio->channels * (size_t)audio->overlap_len *
                   sizeof(float));
        audio->first_block = saved_first;
        audio->pcm_bytes = 0;
        return -1;
    }
    audio->pcm_bytes = produced;
    return 0;
}

const unsigned char *bink2_audio_pcm(const Bink2Audio *audio)
{
    if (audio == NULL || audio->pcm_bytes == 0)
        return NULL;
    return audio->pcm;
}

unsigned long bink2_audio_pcm_bytes(const Bink2Audio *audio)
{
    if (audio == NULL)
        return 0;
    return audio->pcm_bytes;
}

