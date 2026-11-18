#include "bink2.h"
#include "bink2_encoder.h"
#include "test_common.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 64
#define HEIGHT 48
#define FRAMES 24
#define RATE 48000UL
#define FPS 24UL
#define SAMPLES 48000UL

typedef struct {
    const short *pcm;
} AudioCtx;

static int read_yuv(void *user, unsigned long frame, unsigned char *y,
                    unsigned char *u, unsigned char *v)
{
    int cw = (WIDTH + 1) / 2;
    int ch = (HEIGHT + 1) / 2;
    int row;
    int x;

    (void)user;
    for (row = 0; row < HEIGHT; row++)
        for (x = 0; x < WIDTH; x++)
            y[row * WIDTH + x] =
                (unsigned char)((x + row + (int)frame * 5) & 0xFF);
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

    memset(dst, 0, (size_t)count * 2UL * sizeof(short));
    if (start >= SAMPLES)
        return 0;
    if (start + count > SAMPLES)
        count = SAMPLES - start;
    for (i = 0; i < count * 2UL; i++)
        dst[i] = ctx->pcm[start * 2UL + i];
    return 0;
}

static int read_file(const char *path, unsigned char **data, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long end;

    *data = NULL;
    *size = 0;
    if (file == NULL)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0 || (end = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    *data = (unsigned char *)malloc(end != 0 ? (size_t)end : 1);
    if (*data == NULL ||
        fread(*data, 1, (size_t)end, file) != (size_t)end) {
        free(*data);
        *data = NULL;
        fclose(file);
        return -1;
    }
    fclose(file);
    *size = (size_t)end;
    return 0;
}

int main(void)
{
    const char *oracle = getenv("BINK2_ORACLE");
    const char *movie = "enc_audio_oracle.bk2";
    const char *ours_path = "enc_audio_oracle_ours.s16";
    const char *theirs_path = "enc_audio_oracle_ffmpeg.s16";
    short *tone;
    AudioCtx audio;
    bink2_enc_source source;
    bink2_enc_options options;
    bink2 b;
    FILE *ours = NULL;
    unsigned char *our_data = NULL;
    unsigned char *their_data = NULL;
    size_t our_size = 0;
    size_t their_size = 0;
    double signal = 0.0;
    double noise = 0.0;
    unsigned long f;
    unsigned long i;
    char command[4096];
    int status;

    if (oracle == NULL || oracle[0] == '\0')
        return 77;
    tone = (short *)malloc((size_t)SAMPLES * 2UL * sizeof(short));
    CHECK(tone != NULL);
    for (i = 0; i < SAMPLES; i++) {
        short sample = (short)lrint(
            sin(2.0 * 3.14159265358979323846 * 1000.0 * (double)i /
                (double)RATE) * 0.5 * 32767.0);

        tone[i * 2] = sample;
        tone[i * 2 + 1] = sample;
    }
    audio.pcm = tone;
    memset(&source, 0, sizeof(source));
    source.width = WIDTH;
    source.height = HEIGHT;
    source.frame_count = FRAMES;
    source.fps_num = FPS;
    source.fps_den = 1;
    source.read_frame_yuv = read_yuv;
    source.audio.rate = RATE;
    source.audio.channels = 2;
    source.audio.sample_count = SAMPLES;
    source.read_audio = read_audio;
    source.user = &audio;
    bink2_enc_options_default(&options);
    {
        char err[256] = "";

        CHECK(bink2_enc_encode(&source, &options, movie, err, sizeof(err)) ==
              BINK2_ENC_OK);
    }

    b = bink2_open_file(movie, BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_enable_audio(b, 0, 1) == 0);
    ours = fopen(ours_path, "wb");
    CHECK(ours != NULL);
    for (f = 0; f < FRAMES; f++) {
        const unsigned char *pcm;
        unsigned long size;

        CHECK((f == 0 ? bink2_first(b) : bink2_next(b)) != BINK2_ERROR);
        pcm = bink2_get_audio(b, 0);
        size = bink2_get_audio_size(b, 0);
        CHECK(pcm != NULL && size != 0);
        CHECK(fwrite(pcm, 1, size, ours) == size);
    }
    bink2_close(b);
    CHECK(fclose(ours) == 0);
    ours = NULL;

    snprintf(command, sizeof(command),
             "\"%s\" -v error -y -i \"%s\" -map 0:a:0 -c:a pcm_s16le "
             "-f s16le \"%s\"",
             oracle, movie, theirs_path);
    status = system(command);
    CHECK(status == 0);
    CHECK(read_file(ours_path, &our_data, &our_size) == 0);
    CHECK(read_file(theirs_path, &their_data, &their_size) == 0);
    CHECK(our_size == their_size);
    {
        const short *a = (const short *)our_data;
        const short *b = (const short *)their_data;
        size_t samples = our_size / sizeof(short);
        size_t k;
        int worst = 0;

        for (k = 0; k < samples; k++) {
            int diff = (int)a[k] - (int)b[k];

            if (diff < 0)
                diff = -diff;
            if (diff > worst)
                worst = diff;
        }
        /* FFmpeg's float transform and this decoder's double transform
         * round about half the samples in opposite directions by one LSB. */
        CHECK(worst <= 1);
    }

    for (i = 128UL * 2UL; i < SAMPLES * 2UL; i++) {
        double s = (double)tone[i];
        double d = (double)((const short *)our_data)[i] - s;

        signal += s * s;
        noise += d * d;
    }
    CHECK(noise > 0.0);
    CHECK(10.0 * log10(signal / noise) >= 25.0);

    free(our_data);
    free(their_data);
    free(tone);
    remove(movie);
    remove(ours_path);
    remove(theirs_path);
    return 0;
}
