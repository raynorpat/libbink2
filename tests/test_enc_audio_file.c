#include "bink2.h"
#include "bink2_encoder.h"
#include "test_common.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 64
#define HEIGHT 48
#define FRAMES 2
#define RATE 48000UL
#define FPS 25UL

typedef struct {
    const short *pcm;
    unsigned long frames;
} AudioCtx;

static int read_yuv(void *user, unsigned long frame, unsigned char *y,
                    unsigned char *u, unsigned char *v)
{
    int cw = (WIDTH + 1) / 2;
    int ch = (HEIGHT + 1) / 2;
    int x;
    int row;

    (void)user;
    for (row = 0; row < HEIGHT; row++)
        for (x = 0; x < WIDTH; x++)
            y[row * WIDTH + x] =
                (unsigned char)((x + row + (int)frame * 7) & 0xFF);
    for (row = 0; row < ch; row++)
        for (x = 0; x < cw; x++) {
            u[row * cw + x] = (unsigned char)(128 + x);
            v[row * cw + x] = (unsigned char)(128 - row);
        }
    return 0;
}

static int read_audio(void *user, unsigned long start, unsigned long count,
                      short *dst)
{
    const AudioCtx *ctx = (const AudioCtx *)user;
    unsigned long i;
    unsigned long samples = count * 2UL;

    memset(dst, 0, (size_t)samples * sizeof(short));
    if (ctx->pcm == NULL || start >= ctx->frames)
        return 0;
    if (start + count > ctx->frames)
        count = ctx->frames - start;
    for (i = 0; i < count * 2UL; i++)
        dst[i] = ctx->pcm[start * 2UL + i];
    return 0;
}

static short *make_tone(unsigned long frames)
{
    short *pcm = (short *)malloc((size_t)frames * 2UL * sizeof(short));
    unsigned long i;

    if (pcm == NULL)
        return NULL;
    for (i = 0; i < frames; i++) {
        short sample = (short)lrint(
            sin(2.0 * 3.14159265358979323846 * 1000.0 *
                (double)i / (double)RATE) * 0.5 * 32767.0);

        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
    }
    return pcm;
}

static int encode_file(const char *path, const short *pcm,
                       unsigned long sample_count, int channels, int verify)
{
    bink2_enc_source source;
    bink2_enc_options options;
    AudioCtx audio;
    char err[256] = "";

    memset(&source, 0, sizeof(source));
    source.width = WIDTH;
    source.height = HEIGHT;
    source.frame_count = FRAMES;
    source.fps_num = FPS;
    source.fps_den = 1;
    source.read_frame_yuv = read_yuv;
    if (channels != 0) {
        audio.pcm = pcm;
        audio.frames = sample_count;
        source.audio.rate = RATE;
        source.audio.channels = channels;
        source.audio.sample_count = sample_count;
        source.read_audio = read_audio;
        source.user = &audio;
    }
    bink2_enc_options_default(&options);
    options.verify = verify;
    if (pcm == NULL && channels != 0) {
        /* Silence comes from the callback's zero fill. */
        audio.pcm = NULL;
        audio.pcm = NULL;
    }
    {
        bink2_enc_status status =
            bink2_enc_encode(&source, &options, path, err, sizeof(err));

        if (status != BINK2_ENC_OK)
            fprintf(stderr, "encode_file: %d %s\n", (int)status, err);
        CHECK(status == BINK2_ENC_OK);
    }
    return 0;
}

static unsigned long read_u32(const char *path, unsigned long offset)
{
    unsigned char bytes[4];
    FILE *file = fopen(path, "rb");
    unsigned long value;

    CHECK(file != NULL);
    CHECK(fseek(file, (long)offset, SEEK_SET) == 0);
    CHECK(fread(bytes, 1, 4, file) == 4);
    fclose(file);
    value = (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
            ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
    return value;
}

static int test_audio_file(void)
{
    const char *path = "test_enc_audio_file.bk2";
    short *pcm = make_tone(4800);
    bink2 b;
    unsigned char mask = 0;
    unsigned char channels[7] = { 0 };
    unsigned char depths[7] = { 0 };
    unsigned long rates[7] = { 0 };
    unsigned long total = 0;
    unsigned long f;

    CHECK(pcm != NULL);
    if (encode_file(path, pcm, 4800, 2, 1) != 0) {
        free(pcm);
        return 1;
    }
    b = bink2_open_file(path, BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_info_audio(b, &mask, channels, depths, rates) == 0);
    CHECK((mask & 1) != 0);
    CHECK(channels[0] == 2 && depths[0] == 16 && rates[0] == RATE);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_enable_audio(b, 0, 1) == 0);
    for (f = 0; f < FRAMES; f++) {
        CHECK((f == 0 ? bink2_first(b) : bink2_next(b)) != BINK2_ERROR);
        total += bink2_get_audio_size(b, 0);
    }
    CHECK(total == 3840UL * 2UL * 2UL);
    bink2_close(b);
    remove(path);
    free(pcm);
    return 0;
}

static int test_short_wav_pads(void)
{
    const char *path = "test_enc_audio_short.bk2";
    short *pcm = make_tone(1000);
    bink2 b;
    unsigned char *collected;
    unsigned long total = 3840UL * 2UL * 2UL;
    unsigned long at = 0;
    unsigned long f;
    short *samples;
    unsigned long i;

    CHECK(pcm != NULL);
    if (encode_file(path, pcm, 1000, 2, 1) != 0) {
        free(pcm);
        return 1;
    }
    b = bink2_open_file(path, BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_enable_audio(b, 0, 1) == 0);
    collected = (unsigned char *)malloc(total);
    CHECK(collected != NULL);
    for (f = 0; f < FRAMES; f++) {
        const unsigned char *pcm_data;

        CHECK((f == 0 ? bink2_first(b) : bink2_next(b)) != BINK2_ERROR);
        pcm_data = bink2_get_audio(b, 0);
        CHECK(pcm_data != NULL);
        CHECK(bink2_get_audio_size(b, 0) <= total - at);
        memcpy(collected + at, pcm_data, bink2_get_audio_size(b, 0));
        at += bink2_get_audio_size(b, 0);
    }
    CHECK(at == total);
    samples = (short *)collected;
    /* The second block fades out the first block's transform tail over the
     * 128-sample overlap; after that it is exactly silent. */
    for (i = (1920UL + 128UL) * 2UL; i < total / sizeof(short); i++)
        CHECK(samples[i] == 0);
    bink2_close(b);
    free(collected);
    remove(path);
    free(pcm);
    return 0;
}

static int test_no_audio(void)
{
    const char *path = "test_enc_audio_none.bk2";
    bink2 b;
    unsigned char mask = 0xFF;

    CHECK(encode_file(path, NULL, 0, 0, 1) == 0);
    b = bink2_open_file(path, BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_info_audio(b, &mask, NULL, NULL, NULL) == 0);
    CHECK(mask == 0);
    bink2_close(b);
    remove(path);
    return 0;
}

static int test_packet_sizes(void)
{
    const char *audio_path = "test_enc_audio_size.bk2";
    const char *plain_path = "test_enc_audio_size_plain.bk2";
    short *pcm = make_tone(4800);
    unsigned long audio_index;
    unsigned long plain_index;
    unsigned long audio_delta[FRAMES];
    unsigned long plain_delta[FRAMES];
    unsigned long largest;
    unsigned long f;

    CHECK(pcm != NULL);
    if (encode_file(audio_path, pcm, 4800, 2, 1) != 0 ||
        encode_file(plain_path, NULL, 0, 0, 1) != 0) {
        free(pcm);
        return 1;
    }
    audio_index = 60;
    plain_index = 48;
    for (f = 0; f < FRAMES; f++) {
        unsigned long a0 = read_u32(audio_path, audio_index + 4 * f);
        unsigned long a1 = read_u32(audio_path, audio_index + 4 * (f + 1));
        unsigned long p0 = read_u32(plain_path, plain_index + 4 * f);
        unsigned long p1 = read_u32(plain_path, plain_index + 4 * (f + 1));

        audio_delta[f] = (a1 & ~1UL) - (a0 & ~1UL);
        plain_delta[f] = (p1 & ~1UL) - (p0 & ~1UL);
        CHECK(audio_delta[f] > plain_delta[f]);
    }
    largest = read_u32(audio_path, 12);
    CHECK(largest >= audio_delta[0] && largest >= audio_delta[1]);
    remove(audio_path);
    remove(plain_path);
    free(pcm);
    return 0;
}

int main(void)
{
    CHECK(test_audio_file() == 0);
    CHECK(test_short_wav_pads() == 0);
    CHECK(test_no_audio() == 0);
    CHECK(test_packet_sizes() == 0);
    return 0;
}
