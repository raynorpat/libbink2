/**
    libbink2 - PCM WAV input for the encoder
    Copyright (C) 2026 libbink2 contributors

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
*/

#include "bink2_enc_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t errlen, const char *message)
{
    if (err != NULL && errlen > 0)
        snprintf(err, errlen, "%s", message);
    return -1;
}

static unsigned read_u16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long read_u32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

int bink2_enc_wav_load(const char *path, Bink2EncWav *wav, char *err,
                       size_t errlen)
{
    FILE *file;
    unsigned char header[12];
    long file_len;
    unsigned long rate = 0;
    unsigned channels = 0;
    unsigned bits = 0;
    int have_fmt = 0;
    unsigned char *data = NULL;
    unsigned long data_size = 0;
    unsigned long frames;
    unsigned long i;
    unsigned long total;
    short *pcm;

    if (wav == NULL)
        return fail(err, errlen, "missing WAV output");
    memset(wav, 0, sizeof(*wav));
    if (path == NULL)
        return fail(err, errlen, "missing WAV path");

    file = fopen(path, "rb");
    if (file == NULL)
        return fail(err, errlen, "cannot open WAV");
    if (fseek(file, 0, SEEK_END) != 0 || (file_len = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return fail(err, errlen, "cannot read WAV");
    }
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        fclose(file);
        return fail(err, errlen, "not a WAV file");
    }

    for (;;) {
        unsigned char chunk[8];
        unsigned long size;
        long body;
        long next;

        if (fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk))
            break;
        size = read_u32(chunk + 4);
        body = ftell(file);
        if (body < 0 || body > file_len)
            break;
        /* A chunk that claims more than the file holds must not wrap a
         * 32-bit length and seek backwards into the same chunk. */
        if (size > (unsigned long)(file_len - body)) {
            int is_data = memcmp(chunk, "data", 4) == 0;

            fclose(file);
            free(data);
            return fail(err, errlen,
                        is_data ? "truncated WAV data"
                                : "truncated WAV chunk");
        }
        next = body + (long)size;
        if ((size & 1UL) != 0 && next < file_len)
            next++;
        if (memcmp(chunk, "fmt ", 4) == 0 && !have_fmt) {
            unsigned char fmt[16];

            if (size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), file) !=
                                          sizeof(fmt)) {
                fclose(file);
                free(data);
                return fail(err, errlen, "truncated WAV fmt chunk");
            }
            if (read_u16(fmt) != 1) {
                fclose(file);
                free(data);
                return fail(err, errlen, "WAV must be PCM");
            }
            channels = read_u16(fmt + 2);
            rate = read_u32(fmt + 4);
            bits = read_u16(fmt + 14);
            have_fmt = 1;
        } else if (memcmp(chunk, "data", 4) == 0 && data == NULL) {
            data = (unsigned char *)malloc(size != 0 ? size : 1);
            if (data == NULL) {
                fclose(file);
                return fail(err, errlen, "out of memory");
            }
            if (size != 0 && fread(data, 1, size, file) != size) {
                free(data);
                fclose(file);
                return fail(err, errlen, "truncated WAV data");
            }
            data_size = size;
        }
        if (fseek(file, next, SEEK_SET) != 0)
            break;
    }
    fclose(file);

    if (!have_fmt || data == NULL) {
        free(data);
        return fail(err, errlen, "WAV is missing fmt or data");
    }
    if ((channels != 1 && channels != 2) || rate == 0 || rate > 65535 ||
        (bits != 8 && bits != 16 && bits != 24)) {
        free(data);
        return fail(err, errlen, "unsupported WAV format");
    }
    {
        unsigned long block = (unsigned long)channels * (bits / 8);

        if (block == 0 || data_size < block) {
            free(data);
            return fail(err, errlen, "WAV data is empty");
        }
        frames = data_size / block;
    }
    total = frames * channels;
    pcm = (short *)malloc((size_t)total * sizeof(short));
    if (pcm == NULL) {
        free(data);
        return fail(err, errlen, "out of memory");
    }
    if (bits == 8) {
        for (i = 0; i < total; i++)
            pcm[i] = (short)(((int)data[i] - 128) * 256);
    } else if (bits == 16) {
        for (i = 0; i < total; i++) {
            int value = (int)data[i * 2] | ((int)data[i * 2 + 1] << 8);

            if (value & 0x8000)
                value -= 0x10000;
            pcm[i] = (short)value;
        }
    } else {
        for (i = 0; i < total; i++) {
            int value = (int)data[i * 3] | ((int)data[i * 3 + 1] << 8) |
                        ((int)data[i * 3 + 2] << 16);
            int rounded;

            if (value & 0x800000)
                value -= 0x1000000;
            rounded = value >= 0 ? (value + 128) >> 8
                                 : -((-value + 128) >> 8);
            if (rounded > 32767)
                rounded = 32767;
            if (rounded < -32768)
                rounded = -32768;
            pcm[i] = (short)rounded;
        }
    }
    free(data);
    wav->rate = rate;
    wav->channels = (int)channels;
    wav->sample_count = frames;
    wav->pcm = pcm;
    return 0;
}

void bink2_enc_wav_free(Bink2EncWav *wav)
{
    if (wav == NULL)
        return;
    free(wav->pcm);
    memset(wav, 0, sizeof(*wav));
}
