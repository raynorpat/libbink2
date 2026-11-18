#include "bink2_enc_audio.h"
#include "bink2_audio.h"
#include "test_common.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEST_RATE 48000UL
#define TEST_CHANNELS 2
#define TEST_FRAMES 1920UL
#define TEST_BLOCK_OUT 1920UL
#define TEST_OVERLAP 128UL

typedef struct {
    const short *pcm;
    unsigned long frames;
    int channels;
} PcmSource;

static int read_audio(void *user, unsigned long start, unsigned long count,
                      short *dst)
{
    const PcmSource *src = (const PcmSource *)user;
    unsigned long i;

    memset(dst, 0, (size_t)count * (size_t)src->channels * sizeof(short));
    if (start >= src->frames)
        return 0;
    if (start + count > src->frames)
        count = src->frames - start;
    for (i = 0; i < count * (unsigned long)src->channels; i++)
        dst[i] = src->pcm[start * (unsigned long)src->channels + i];
    return 0;
}

static double snr_db(const short *decoded, const short *source,
                     unsigned long skip_frames, unsigned long frames,
                     int channels)
{
    double signal = 0.0;
    double noise = 0.0;
    unsigned long i;

    for (i = skip_frames * (unsigned long)channels;
         i < frames * (unsigned long)channels; i++) {
        double s = (double)source[i];
        double d = (double)((double)decoded[i] - s);

        signal += s * s;
        noise += d * d;
    }
    if (noise <= 0.0)
        return 999.0;
    if (signal <= 0.0)
        return 0.0;
    return 10.0 * log10(signal / noise);
}

static short *make_sine(unsigned long frames, int channels, double hz,
                        double amplitude)
{
    short *pcm = (short *)malloc((size_t)frames * (size_t)channels *
                                 sizeof(short));
    unsigned long i;

    if (pcm == NULL)
        return NULL;
    for (i = 0; i < frames; i++) {
        double value = sin(2.0 * 3.14159265358979323846 * hz *
                           (double)i / (double)TEST_RATE);
        short sample = (short)lrint(value * amplitude * 32767.0);
        int c;

        for (c = 0; c < channels; c++)
            pcm[i * (unsigned long)channels + (unsigned long)c] = sample;
    }
    return pcm;
}

static int decode_chunk(const unsigned char *payload, size_t payload_size,
                        unsigned long max_decoded, short **out,
                        unsigned long *out_bytes)
{
    Bink2Audio *decoder = NULL;

    *out = NULL;
    *out_bytes = 0;
    CHECK(bink2_audio_create(&decoder, TEST_RATE, TEST_CHANNELS, 1, 1,
                             max_decoded) == 0);
    if (bink2_audio_decode(decoder, payload, payload_size) != 0) {
        bink2_audio_destroy(decoder);
        return 1;
    }
    *out_bytes = bink2_audio_pcm_bytes(decoder);
    if (*out_bytes != 0) {
        *out = (short *)malloc(*out_bytes);
        if (*out == NULL) {
            bink2_audio_destroy(decoder);
            return 1;
        }
        memcpy(*out, bink2_audio_pcm(decoder), *out_bytes);
    }
    bink2_audio_destroy(decoder);
    return 0;
}

static int encode_and_decode(PcmSource *source, Bink2EncAudio **audio,
                             unsigned char **payload, size_t *payload_size,
                             size_t *pcm_bytes, unsigned long *max_decoded)
{
    CHECK(bink2_enc_audio_open(audio, TEST_RATE, TEST_CHANNELS,
                               source->frames, 25, 1, 1, 80,
                               max_decoded) == 0);
    CHECK(bink2_enc_audio_frame(*audio, read_audio, source, 1, payload,
                                payload_size, pcm_bytes) == 0);
    CHECK(*payload != NULL);
    CHECK(*payload_size > 4);
    CHECK(*pcm_bytes == TEST_BLOCK_OUT * TEST_CHANNELS * 2);
    CHECK((*payload)[0] == (unsigned char)(*pcm_bytes & 0xFF));
    return 0;
}

static int test_sine(void)
{
    short *pcm = make_sine(TEST_FRAMES, TEST_CHANNELS, 1000.0, 0.5);
    PcmSource source;
    Bink2EncAudio *audio = NULL;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    size_t pcm_bytes = 0;
    unsigned long max_decoded = 0;
    short *decoded = NULL;
    unsigned long decoded_bytes = 0;
    double snr;

    CHECK(pcm != NULL);
    source.pcm = pcm;
    source.frames = TEST_FRAMES;
    source.channels = TEST_CHANNELS;
    if (encode_and_decode(&source, &audio, &payload, &payload_size,
                          &pcm_bytes, &max_decoded) != 0) {
        free(pcm);
        return 1;
    }
    if (decode_chunk(payload, payload_size, max_decoded, &decoded,
                     &decoded_bytes) != 0) {
        free(pcm);
        free(payload);
        bink2_enc_audio_close(audio);
        return 1;
    }
    CHECK(decoded_bytes == pcm_bytes);
    snr = snr_db(decoded, pcm, TEST_OVERLAP, TEST_FRAMES, TEST_CHANNELS);
    CHECK(snr >= 25.0);
    free(decoded);
    free(payload);
    free(pcm);
    bink2_enc_audio_close(audio);
    return 0;
}

static int test_silence(void)
{
    short *pcm = (short *)calloc((size_t)TEST_FRAMES * TEST_CHANNELS,
                                 sizeof(short));
    PcmSource source;
    Bink2EncAudio *audio = NULL;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    size_t pcm_bytes = 0;
    unsigned long max_decoded = 0;
    short *decoded = NULL;
    unsigned long decoded_bytes = 0;
    unsigned long i;

    CHECK(pcm != NULL);
    source.pcm = pcm;
    source.frames = TEST_FRAMES;
    source.channels = TEST_CHANNELS;
    if (encode_and_decode(&source, &audio, &payload, &payload_size,
                          &pcm_bytes, &max_decoded) != 0) {
        free(pcm);
        return 1;
    }
    if (decode_chunk(payload, payload_size, max_decoded, &decoded,
                     &decoded_bytes) != 0) {
        free(pcm);
        free(payload);
        bink2_enc_audio_close(audio);
        return 1;
    }
    CHECK(decoded_bytes == pcm_bytes);
    for (i = 0; i < decoded_bytes / sizeof(short); i++)
        CHECK(decoded[i] == 0);
    free(decoded);
    free(payload);
    free(pcm);
    bink2_enc_audio_close(audio);
    return 0;
}

static int test_dc(void)
{
    short *pcm = (short *)malloc((size_t)TEST_FRAMES * TEST_CHANNELS *
                                 sizeof(short));
    PcmSource source;
    Bink2EncAudio *audio = NULL;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    size_t pcm_bytes = 0;
    unsigned long max_decoded = 0;
    short *decoded = NULL;
    unsigned long decoded_bytes = 0;
    unsigned long i;
    double snr;

    CHECK(pcm != NULL);
    for (i = 0; i < TEST_FRAMES * TEST_CHANNELS; i++)
        pcm[i] = 10000;
    source.pcm = pcm;
    source.frames = TEST_FRAMES;
    source.channels = TEST_CHANNELS;
    if (encode_and_decode(&source, &audio, &payload, &payload_size,
                          &pcm_bytes, &max_decoded) != 0) {
        free(pcm);
        return 1;
    }
    if (decode_chunk(payload, payload_size, max_decoded, &decoded,
                     &decoded_bytes) != 0) {
        free(pcm);
        free(payload);
        bink2_enc_audio_close(audio);
        return 1;
    }
    CHECK(decoded_bytes == pcm_bytes);
    snr = snr_db(decoded, pcm, TEST_OVERLAP, TEST_FRAMES, TEST_CHANNELS);
    CHECK(snr >= 25.0);
    free(decoded);
    free(payload);
    free(pcm);
    bink2_enc_audio_close(audio);
    return 0;
}

int main(void)
{
    CHECK(test_sine() == 0);
    CHECK(test_silence() == 0);
    CHECK(test_dc() == 0);
    return 0;
}
