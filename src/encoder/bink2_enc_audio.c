/**
    libbink2 - Bink Audio 2 encoder
    Copyright (C) 2026 libbink2 contributors

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
*/

#include "bink2_enc_audio.h"

#include "bink2_audio.h"
#include "bink2_bits.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int frame_len;
    double *re;
    double *im;
} Bink2EncDct;

struct Bink2EncAudio {
    Bink2AudioInfo info;
    unsigned long rate;
    unsigned long fps_num;
    unsigned long fps_den;
    unsigned long sample_count;
    uint64_t acc;
    uint64_t target;
    uint64_t produced;
    unsigned long source_pos;
    unsigned long video_end;
    int channels;
    int block_out;
    int quality;
    int verbose;
    Bink2EncDct dct;
    float *frame;
    short *samples;
    double *coef;
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

/* DCT-II: the inverse of the decoder's dct_calc_III plus DC scaling. */
static void dct_forward(Bink2EncDct *dct, const float *src, double *coeffs)
{
    int n = dct->frame_len;
    int i;
    int k;

    for (i = 0; i < n; i++) {
        dct->re[i] = (double)src[i];
        dct->im[i] = 0.0;
    }
    memset(dct->re + n, 0, (size_t)n * sizeof(*dct->re));
    memset(dct->im + n, 0, (size_t)n * sizeof(*dct->im));
    fft_transform(dct->re, dct->im, n * 2, 0);
    for (k = 0; k < n; k++) {
        double theta = M_PI * (double)k / (2.0 * (double)n);

        coeffs[k] = dct->re[k] * cos(theta) + dct->im[k] * sin(theta);
    }
}

static int write_float(Bink2BitWriter *bw, float value)
{
    int sign = 0;
    int exponent;
    int mantissa;
    float magnitude;

    if (value < 0.0f) {
        sign = 1;
        value = -value;
    }
    if (value == 0.0f)
        return bink2_bw_put(bw, 0, 29);
    magnitude = frexpf(value, &exponent);
    mantissa = (int)lrintf(magnitude * 8388608.0f);
    if (mantissa >= 8388608) {
        mantissa >>= 1;
        exponent++;
    }
    while (exponent < 0 && mantissa > 0) {
        mantissa >>= 1;
        exponent++;
    }
    if (exponent < 0)
        exponent = 0;
    if (exponent > 31) {
        exponent = 31;
        mantissa = 0x7fffff;
    }
    if (bink2_bw_put(bw, (unsigned)exponent, 5) != 0)
        return -1;
    if (bink2_bw_put(bw, (unsigned)mantissa, 23) != 0)
        return -1;
    return bink2_bw_put(bw, (unsigned)sign, 1);
}

static int band_of(const Bink2AudioInfo *info, int index)
{
    int band = 0;

    while (band + 1 < info->num_bands &&
           (int)info->bands[band + 1] <= index)
        band++;
    return band;
}

static int choose_q(const Bink2AudioInfo *info, double peak, int target)
{
    int q = 0;

    if (peak <= 0.0)
        return 0;
    while (q < 95 && peak / (double)info->quant[q] > (double)target)
        q++;
    return q;
}

static int bit_length(unsigned long value)
{
    int bits = 0;

    while (value != 0) {
        bits++;
        value >>= 1;
    }
    return bits;
}

int bink2_enc_audio_clamp(unsigned long magnitude, unsigned long *out)
{
    if (magnitude > 32767UL) {
        if (out != NULL)
            *out = 32767UL;
        return 1;
    }
    if (out != NULL)
        *out = magnitude;
    return 0;
}

static int quantised(const Bink2AudioInfo *info, int band, double value)
{
    double step = (double)info->quant[band];

    return step > 0.0 ? (int)lround(value / step) : 0;
}

static int write_block(Bink2EncAudio *audio, Bink2BitWriter *bw)
{
    static const unsigned char rle_length[16] = {
        2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64
    };
    const Bink2AudioInfo *info = &audio->info;
    int target = 8;
    int band_q[25];
    int shift;
    int clipped = 0;
    int ch;

    shift = audio->quality * 10 / 100;
    target <<= shift;
    if (target > 32767)
        target = 32767;
    if (bink2_bw_put(bw, 0, 2) != 0)
        return -1;

    for (ch = 0; ch < audio->channels; ch++) {
        const float *src = audio->frame + (size_t)ch * (size_t)info->frame_len;
        int floor_q;
        int band;
        int i;

        dct_forward(&audio->dct, src, audio->coef);
        audio->coef[0] *= 0.5;
        if (write_float(bw, (float)(audio->coef[0] / (double)info->root)) != 0 ||
            write_float(bw, (float)(audio->coef[1] / (double)info->root)) != 0)
            return -1;

        {
            double peak = 0.0;

            for (i = 2; i < info->frame_len; i++) {
                double magnitude = fabs(audio->coef[i]);

                if (magnitude > peak)
                    peak = magnitude;
            }
            floor_q = choose_q(info, peak, target) - 12;
            if (floor_q < 0)
                floor_q = 0;
        }

        for (band = 0; band < info->num_bands; band++) {
            int lo = (int)info->bands[band];
            int hi = band + 1 < info->num_bands
                         ? (int)info->bands[band + 1]
                         : info->frame_len;
            double peak = 0.0;
            int q;

            if (lo < 2)
                lo = 2;
            if (hi > info->frame_len)
                hi = info->frame_len;
            for (i = lo; i < hi; i++) {
                double magnitude = fabs(audio->coef[i]);

                if (magnitude > peak)
                    peak = magnitude;
            }
            q = choose_q(info, peak, target);
            if (q < floor_q)
                q = floor_q;
            band_q[band] = q;
            if (bink2_bw_put(bw, (unsigned)q, 7) != 0)
                return -1;
        }

        i = 2;
        while (i < info->frame_len) {            int run = -1;
            int entry;

            for (entry = 15; entry >= 0; entry--) {
                int span = (int)rle_length[entry] * 8;
                int m;

                if (i + span > info->frame_len)
                    continue;
                for (m = i; m < i + span; m++) {
                    int band = band_of(info, m);

                    if (quantised(info, band_q[band],
                                  audio->coef[m]) != 0)
                        break;
                }
                if (m == i + span) {
                    run = entry;
                    break;
                }
            }
            if (run >= 0) {
                if (bink2_bw_put(bw, 1, 1) != 0 ||
                    bink2_bw_put(bw, (unsigned)run, 4) != 0 ||
                    bink2_bw_put(bw, 0, 4) != 0)
                    return -1;
                i += (int)rle_length[run] * 8;
            } else {
                int j = i + 8;
                int width = 0;
                int k;

                if (j > info->frame_len)
                    j = info->frame_len;
                for (k = i; k < j; k++) {
                    int band = band_of(info, k);
                    long value = quantised(info, band_q[band],
                                           audio->coef[k]);
                    unsigned long magnitude =
                        value < 0 ? (unsigned long)(-value)
                                  : (unsigned long)value;
                    int bits = bit_length(magnitude);

                    if (bits > width)
                        width = bits;
                }
                if (width > 15)
                    width = 15;
                if (bink2_bw_put(bw, 0, 1) != 0 ||
                    bink2_bw_put(bw, (unsigned)width, 4) != 0)
                    return -1;
                {
                    unsigned long magnitudes[8];

                    for (k = i; k < j; k++) {
                        int band = band_of(info, k);
                        long value = quantised(info, band_q[band],
                                               audio->coef[k]);
                        unsigned long magnitude =
                            value < 0 ? (unsigned long)(-value)
                                      : (unsigned long)value;

                        if (bink2_enc_audio_clamp(magnitude, &magnitude))
                            clipped = 1;
                        magnitudes[k - i] = magnitude;
                        if (bink2_bw_put(bw, (unsigned)magnitude, width) != 0)
                            return -1;
                    }
                    for (k = i; k < j; k++) {
                        int band = band_of(info, k);
                        long value = quantised(info, band_q[band],
                                               audio->coef[k]);

                        if (magnitudes[k - i] != 0 &&
                            bink2_bw_put(bw, value < 0 ? 1u : 0u, 1) != 0)
                            return -1;
                    }
                }
                i = j;
            }
        }
    }
    if (clipped && audio->verbose)
        fprintf(stderr, "bink2 encoder: audio block clipped to 15 bits\n");
    return 0;
}

static int read_pcm(Bink2EncAudio *audio,
                    int (*read_audio)(void *user, unsigned long start_sample,
                                      unsigned long sample_count, short *dst),
                    void *user, unsigned long start, unsigned long count,
                    short *dst)
{
    size_t samples = (size_t)count * (size_t)audio->channels;
    unsigned long limit = audio->sample_count;

    memset(dst, 0, samples * sizeof(short));
    if (audio->video_end < limit)
        limit = audio->video_end;
    if (start >= limit)
        return 0;
    if (count > limit - start)
        count = limit - start;
    if (count == 0)
        return 0;
    return read_audio(user, start, count, dst) == 0 ? 0 : -1;
}

int bink2_enc_audio_open(Bink2EncAudio **out, unsigned long rate,
                         int channels, unsigned long sample_count,
                         unsigned long fps_num, unsigned long fps_den,
                         unsigned long frame_count, int quality, int verbose,
                         unsigned long *max_decoded)
{
    Bink2EncAudio *audio;
    uint64_t acc = 0;
    uint64_t video_samples = 0;
    uint64_t bytes;
    unsigned long i;

    if (out == NULL || max_decoded == NULL)
        return -1;
    *out = NULL;
    *max_decoded = 0;
    if (rate == 0 || rate > 65535 || (channels != 1 && channels != 2) ||
        sample_count == 0 || fps_num == 0 || fps_den == 0 || frame_count == 0 ||
        quality < 0 || quality > 100)
        return -1;
    audio = (Bink2EncAudio *)calloc(1, sizeof(*audio));
    if (audio == NULL)
        return -1;
    if (bink2_audio_describe(rate, channels, 1, 1, &audio->info) != 0) {
        free(audio);
        return -1;
    }
    audio->rate = rate;
    audio->fps_num = fps_num;
    audio->fps_den = fps_den;
    audio->sample_count = sample_count;
    audio->channels = channels;
    audio->block_out = audio->info.frame_len - audio->info.overlap_len;
    audio->quality = quality;
    audio->verbose = verbose;
    audio->dct.frame_len = audio->info.frame_len;
    audio->dct.re = (double *)calloc((size_t)audio->info.frame_len * 2u,
                                     sizeof(double));
    audio->dct.im = (double *)calloc((size_t)audio->info.frame_len * 2u,
                                     sizeof(double));
    audio->frame = (float *)calloc(
        (size_t)channels * (size_t)audio->info.frame_len, sizeof(float));
    audio->samples = (short *)calloc(
        (size_t)channels * (size_t)audio->info.frame_len, sizeof(short));
    audio->coef = (double *)calloc((size_t)audio->info.frame_len,
                                   sizeof(double));
    if (audio->block_out <= 0 || audio->dct.re == NULL ||
        audio->dct.im == NULL || audio->frame == NULL ||
        audio->samples == NULL || audio->coef == NULL) {
        bink2_enc_audio_close(audio);
        return -1;
    }

    for (i = 0; i < frame_count; i++) {
        acc += (uint64_t)rate * fps_den;
        video_samples += acc / fps_num;
        acc %= fps_num;
    }
    if (video_samples > 0xffffffffUL) {
        bink2_enc_audio_close(audio);
        return -1;
    }
    audio->video_end = (unsigned long)video_samples;
    video_samples += (uint64_t)audio->block_out;
    if (video_samples > UINT64_MAX / ((uint64_t)channels * 2u)) {
        bink2_enc_audio_close(audio);
        return -1;
    }
    bytes = video_samples * (uint64_t)channels * 2u;
    if (bytes > 0xffffffffULL) {
        bink2_enc_audio_close(audio);
        return -1;
    }
    *max_decoded = (unsigned long)bytes;
    *out = audio;
    return 0;
}

void bink2_enc_audio_close(Bink2EncAudio *audio)
{
    if (audio == NULL)
        return;
    free(audio->dct.re);
    free(audio->dct.im);
    free(audio->frame);
    free(audio->samples);
    free(audio->coef);
    free(audio);
}

static int take_blocks(Bink2EncAudio *audio, int *blocks, size_t *pcm_bytes)
{
    uint64_t acc = audio->acc + (uint64_t)audio->rate * audio->fps_den;
    uint64_t quota = acc / audio->fps_num;
    uint64_t end = audio->produced;
    int count = 0;

    audio->acc = acc % audio->fps_num;
    audio->target += quota;
    while (end < audio->target) {
        count++;
        end += (uint64_t)audio->block_out;
        if (count > 1000000)
            return -1;
    }
    if ((uint64_t)count * (uint64_t)audio->block_out *
            (uint64_t)audio->channels * 2u >
        0xffffffffULL)
        return -1;
    audio->produced = end;
    *blocks = count;
    *pcm_bytes = (size_t)count * (size_t)audio->block_out *
                 (size_t)audio->channels * 2u;
    return 0;
}

int bink2_enc_audio_frame(Bink2EncAudio *audio,
                          int (*read_audio)(void *user,
                                            unsigned long start_sample,
                                            unsigned long sample_count,
                                            short *dst),
                          void *user, int is_last,
                          unsigned char **payload, size_t *payload_size,
                          size_t *pcm_bytes)
{
    Bink2BitWriter writer;
    int blocks;
    size_t pcm;
    int block;

    (void)is_last;
    if (audio == NULL || read_audio == NULL || payload == NULL ||
        payload_size == NULL || pcm_bytes == NULL)
        return -1;
    *payload = NULL;
    *payload_size = 0;
    *pcm_bytes = 0;
    if (take_blocks(audio, &blocks, &pcm) != 0)
        return -1;
    if (blocks == 0)
        return 0;

    bink2_bw_init(&writer);
    if (bink2_bw_put(&writer, (unsigned)pcm, 32) != 0) {
        bink2_bw_free(&writer);
        return -1;
    }
    for (block = 0; block < blocks; block++) {
        unsigned long main_start = audio->source_pos;
        unsigned long look_start = audio->source_pos + (unsigned long)
                                   audio->block_out;
        int c;
        int i;

        if (read_pcm(audio, read_audio, user, main_start,
                     (unsigned long)audio->block_out, audio->samples) != 0 ||
            read_pcm(audio, read_audio, user, look_start,
                     (unsigned long)audio->info.overlap_len,
                     audio->samples + (size_t)audio->block_out *
                                          (size_t)audio->channels) != 0) {
            bink2_bw_free(&writer);
            return BINK2_ENC_AUDIO_INPUT;
        }
        for (c = 0; c < audio->channels; c++) {
            float *dst = audio->frame + (size_t)c *
                                          (size_t)audio->info.frame_len;

            for (i = 0; i < audio->block_out; i++)
                dst[i] = (float)audio->samples[
                             (size_t)i * (size_t)audio->channels +
                             (size_t)c] / 32768.0f;
            for (i = 0; i < audio->info.overlap_len; i++)
                dst[audio->block_out + i] = (float)audio->samples[
                    (size_t)(audio->block_out + i) *
                        (size_t)audio->channels +
                    (size_t)c] / 32768.0f;
        }
        if (write_block(audio, &writer) != 0 ||
            bink2_bw_align32(&writer) != 0) {
            bink2_bw_free(&writer);
            return -1;
        }
        audio->source_pos += (unsigned long)audio->block_out;
    }

    *payload_size = bink2_bw_bytes(&writer);
    *payload = (unsigned char *)malloc(*payload_size != 0 ? *payload_size : 1);
    if (*payload == NULL) {
        bink2_bw_free(&writer);
        *payload_size = 0;
        return -1;
    }
    memcpy(*payload, bink2_bw_data(&writer), *payload_size);
    *pcm_bytes = pcm;
    bink2_bw_free(&writer);
    return 0;
}
