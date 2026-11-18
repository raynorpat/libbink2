#include "bink2_enc_audio.h"
#include "bink2_audio.h"
#include "bink2_bits.h"
#include "test_common.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define M_PI_TEST 3.14159265358979323846

typedef struct {
    const short *pcm;   /* NULL means silence */
    unsigned long frames;
    int channels;
} PcmSource;

typedef struct {
    Bink2EncAudio *audio;
    unsigned char **payload;
    size_t *payload_size;
    size_t *pcm_bytes;
    unsigned long frames;
    unsigned long max_decoded;
} Encoded;

static int read_audio(void *user, unsigned long start, unsigned long count,
                      short *dst)
{
    const PcmSource *src = (const PcmSource *)user;
    unsigned long i;

    memset(dst, 0, (size_t)count * (size_t)src->channels * sizeof(short));
    if (src->pcm == NULL || start >= src->frames)
        return 0;
    if (start + count > src->frames)
        count = src->frames - start;
    for (i = 0; i < count * (unsigned long)src->channels; i++)
        dst[i] = src->pcm[start * (unsigned long)src->channels + i];
    return 0;
}

static short *make_sine(unsigned long rate, unsigned long frames, int channels,
                        double hz, double amplitude)
{
    short *pcm = (short *)malloc((size_t)frames * (size_t)channels *
                                 sizeof(short));
    unsigned long i;

    if (pcm == NULL)
        return NULL;
    for (i = 0; i < frames; i++) {
        double value = sin(2.0 * M_PI_TEST * hz * (double)i / (double)rate);
        short sample = (short)lrint(value * amplitude * 32767.0);
        int c;

        for (c = 0; c < channels; c++)
            pcm[i * (unsigned long)channels + (unsigned long)c] = sample;
    }
    return pcm;
}

static unsigned long frame_len_for(unsigned long rate)
{
    if (rate < 22050)
        return 512;
    if (rate < 44100)
        return 1024;
    return 2048;
}

static unsigned long block_out_for(unsigned long rate)
{
    return frame_len_for(rate) - frame_len_for(rate) / 16;
}

static unsigned long overlap_for(unsigned long rate)
{
    return frame_len_for(rate) / 16;
}

static int encode_all(unsigned long rate, int channels,
                      unsigned long sample_count, const short *pcm,
                      unsigned long fps_num, unsigned long fps_den,
                      unsigned long frames, int quality, Encoded *out)
{
    PcmSource source;
    unsigned long f;

    memset(out, 0, sizeof(*out));
    source.pcm = pcm;
    source.frames = sample_count;
    source.channels = channels;
    CHECK(bink2_enc_audio_open(&out->audio, rate, channels, sample_count,
                               fps_num, fps_den, frames, quality,
                               &out->max_decoded) == 0);
    out->payload = (unsigned char **)calloc(frames, sizeof(unsigned char *));
    out->payload_size = (size_t *)calloc(frames, sizeof(size_t));
    out->pcm_bytes = (size_t *)calloc(frames, sizeof(size_t));
    CHECK(out->payload != NULL && out->payload_size != NULL &&
          out->pcm_bytes != NULL);
    out->frames = frames;
    for (f = 0; f < frames; f++)
        CHECK(bink2_enc_audio_frame(out->audio, read_audio, &source,
                                    f + 1 == frames, &out->payload[f],
                                    &out->payload_size[f],
                                    &out->pcm_bytes[f]) == 0);
    return 0;
}

static void free_encoded(Encoded *encoded)
{
    unsigned long f;

    for (f = 0; f < encoded->frames; f++)
        free(encoded->payload[f]);
    free(encoded->payload);
    free(encoded->payload_size);
    free(encoded->pcm_bytes);
    bink2_enc_audio_close(encoded->audio);
    memset(encoded, 0, sizeof(*encoded));
}

static int decode_all(const Encoded *encoded, unsigned long rate, int channels,
                      short **pcm_out, unsigned long *frames_out)
{
    Bink2Audio *decoder = NULL;
    unsigned long total = 0;
    unsigned long f;
    unsigned long at = 0;
    short *pcm = NULL;

    *pcm_out = NULL;
    *frames_out = 0;
    for (f = 0; f < encoded->frames; f++)
        total += encoded->pcm_bytes[f];
    CHECK(bink2_audio_create(&decoder, rate, channels, 1, 1,
                             encoded->max_decoded) == 0);
    pcm = (short *)malloc(total != 0 ? total : sizeof(short));
    CHECK(pcm != NULL);
    for (f = 0; f < encoded->frames; f++) {
        CHECK(bink2_audio_decode(decoder, encoded->payload[f],
                                 encoded->payload_size[f]) == 0);
        CHECK(bink2_audio_pcm_bytes(decoder) == encoded->pcm_bytes[f]);
        if (encoded->pcm_bytes[f] != 0)
            memcpy((unsigned char *)pcm + at, bink2_audio_pcm(decoder),
                   encoded->pcm_bytes[f]);
        at += encoded->pcm_bytes[f];
    }
    bink2_audio_destroy(decoder);
    *pcm_out = pcm;
    *frames_out = at / ((unsigned long)channels * 2UL);
    return 0;
}

static double snr_between(const short *decoded, unsigned long decoded_frames,
                          const short *source, unsigned long source_frames,
                          int channels, unsigned long skip)
{
    double signal = 0.0;
    double noise = 0.0;
    unsigned long frames = decoded_frames < source_frames ? decoded_frames
                                                          : source_frames;
    unsigned long i;

    for (i = skip * (unsigned long)channels;
         i < frames * (unsigned long)channels; i++) {
        double s = (double)source[i];
        double d = (double)decoded[i] - s;

        signal += s * s;
        noise += d * d;
    }
    if (noise <= 0.0)
        return 999.0;
    if (signal <= 0.0)
        return 0.0;
    return 10.0 * log10(signal / noise);
}

static unsigned long payload_total(const Encoded *encoded)
{
    unsigned long total = 0;
    unsigned long f;

    for (f = 0; f < encoded->frames; f++)
        total += (unsigned long)encoded->payload_size[f];
    return total;
}

static int one_block_case(unsigned long rate, int channels, double hz,
                          double amplitude, int quality, double floor_db)
{
    unsigned long block_out = block_out_for(rate);
    short *pcm = make_sine(rate, block_out, channels, hz, amplitude);
    Encoded encoded;
    short *decoded = NULL;
    unsigned long decoded_frames = 0;
    double snr;

    CHECK(pcm != NULL);
    if (encode_all(rate, channels, block_out, pcm, rate, block_out, 1,
                   quality, &encoded) != 0) {
        free(pcm);
        return 1;
    }
    CHECK(encoded.pcm_bytes[0] == block_out * (unsigned long)channels * 2UL);
    if (decode_all(&encoded, rate, channels, &decoded, &decoded_frames) != 0) {
        free(pcm);
        free_encoded(&encoded);
        return 1;
    }
    snr = snr_between(decoded, decoded_frames, pcm, block_out, channels,
                      overlap_for(rate));
    CHECK(snr >= floor_db);
    free(decoded);
    free(pcm);
    free_encoded(&encoded);
    return 0;
}

static int test_one_block(void)
{
    CHECK(one_block_case(48000, 2, 1000.0, 0.5, 80, 25.0) == 0);
    CHECK(one_block_case(32000, 1, 1000.0, 0.5, 80, 25.0) == 0);
    CHECK(one_block_case(16000, 1, 1000.0, 0.5, 80, 25.0) == 0);
    return 0;
}

static int test_silence(void)
{
    unsigned long block_out = block_out_for(48000);
    Encoded encoded;
    short *decoded = NULL;
    unsigned long decoded_frames = 0;
    unsigned long i;

    if (encode_all(48000, 2, block_out, NULL, 25, 1, 1, 80, &encoded) != 0)
        return 1;
    if (decode_all(&encoded, 48000, 2, &decoded, &decoded_frames) != 0) {
        free_encoded(&encoded);
        return 1;
    }
    CHECK(decoded_frames == block_out);
    for (i = 0; i < block_out * 2; i++)
        CHECK(decoded[i] == 0);
    free(decoded);
    free_encoded(&encoded);
    return 0;
}

static int test_dc(void)
{
    unsigned long block_out = block_out_for(48000);
    short *pcm = (short *)malloc((size_t)block_out * 2 * sizeof(short));
    Encoded encoded;
    short *decoded = NULL;
    unsigned long decoded_frames = 0;
    unsigned long i;
    double snr;

    CHECK(pcm != NULL);
    for (i = 0; i < block_out * 2; i++)
        pcm[i] = 10000;
    if (encode_all(48000, 2, block_out, pcm, 25, 1, 1, 80, &encoded) != 0) {
        free(pcm);
        return 1;
    }
    if (decode_all(&encoded, 48000, 2, &decoded, &decoded_frames) != 0) {
        free(pcm);
        free_encoded(&encoded);
        return 1;
    }
    snr = snr_between(decoded, decoded_frames, pcm, block_out, 2, 128);
    CHECK(snr >= 25.0);
    free(decoded);
    free(pcm);
    free_encoded(&encoded);
    return 0;
}

static int test_three_frames(void)
{
    unsigned long source_frames = 6000;
    short *pcm = make_sine(48000, source_frames, 2, 1000.0, 0.5);
    Encoded encoded;
    short *decoded = NULL;
    unsigned long decoded_frames = 0;
    double snr;

    CHECK(pcm != NULL);
    if (encode_all(48000, 2, source_frames, pcm, 24, 1, 3, 80,
                   &encoded) != 0) {
        free(pcm);
        return 1;
    }
    CHECK(encoded.pcm_bytes[0] == 3840UL * 2UL * 2UL);
    CHECK(encoded.pcm_bytes[1] == 1920UL * 2UL * 2UL);
    CHECK(encoded.pcm_bytes[2] == 1920UL * 2UL * 2UL);
    if (decode_all(&encoded, 48000, 2, &decoded, &decoded_frames) != 0) {
        free(pcm);
        free_encoded(&encoded);
        return 1;
    }
    CHECK(decoded_frames == 7680);
    snr = snr_between(decoded, decoded_frames, pcm, source_frames, 2, 128);
    CHECK(snr >= 25.0);
    free(decoded);
    free(pcm);
    free_encoded(&encoded);
    return 0;
}

static int boundary_case(unsigned long rate, unsigned long expected_block)
{
    unsigned long block_out = block_out_for(rate);
    short *pcm = make_sine(rate, block_out, 1, 1000.0, 0.5);
    Encoded encoded;

    CHECK(block_out == expected_block);
    CHECK(pcm != NULL);
    if (encode_all(rate, 1, block_out, pcm, rate, block_out, 1, 80,
                   &encoded) != 0) {
        free(pcm);
        return 1;
    }
    CHECK(encoded.pcm_bytes[0] == block_out * 2UL);
    free(pcm);
    free_encoded(&encoded);
    return 0;
}

static int test_rate_boundaries(void)
{
    CHECK(boundary_case(22049, 480) == 0);
    CHECK(boundary_case(22050, 960) == 0);
    CHECK(boundary_case(44099, 960) == 0);
    CHECK(boundary_case(44100, 1920) == 0);
    return 0;
}

static int quality_case(int quality, unsigned long *bytes, double *snr)
{
    unsigned long source_frames = 48000;
    short *pcm = make_sine(48000, source_frames, 2, 1000.0, 0.5);
    Encoded encoded;
    short *decoded = NULL;
    unsigned long decoded_frames = 0;

    CHECK(pcm != NULL);
    if (encode_all(48000, 2, source_frames, pcm, 24, 1, 24, quality,
                   &encoded) != 0) {
        free(pcm);
        return 1;
    }
    *bytes = payload_total(&encoded);
    if (decode_all(&encoded, 48000, 2, &decoded, &decoded_frames) != 0) {
        free(pcm);
        free_encoded(&encoded);
        return 1;
    }
    *snr = snr_between(decoded, decoded_frames, pcm, source_frames, 2, 128);
    free(decoded);
    free(pcm);
    free_encoded(&encoded);
    return 0;
}

static int test_quality(void)
{
    unsigned long low_bytes = 0;
    unsigned long high_bytes = 0;
    double low_snr = 0.0;
    double high_snr = 0.0;

    CHECK(quality_case(20, &low_bytes, &low_snr) == 0);
    CHECK(quality_case(80, &high_bytes, &high_snr) == 0);
    CHECK(low_bytes <= high_bytes);
    CHECK(low_snr >= 10.0);
    CHECK(high_snr >= 25.0);
    return 0;
}

static int test_silence_size(void)
{
    unsigned long source_frames = 48000;
    short *tone = make_sine(48000, source_frames, 2, 1000.0, 0.5);
    Encoded encoded;
    unsigned long tone_bytes;
    unsigned long silence_bytes;

    CHECK(tone != NULL);
    if (encode_all(48000, 2, source_frames, tone, 24, 1, 24, 80,
                   &encoded) != 0) {
        free(tone);
        return 1;
    }
    tone_bytes = payload_total(&encoded);
    free_encoded(&encoded);
    if (encode_all(48000, 2, source_frames, NULL, 24, 1, 24, 80,
                   &encoded) != 0) {
        free(tone);
        return 1;
    }
    silence_bytes = payload_total(&encoded);
    free_encoded(&encoded);
    free(tone);
    CHECK(silence_bytes * 4UL <= tone_bytes);
    return 0;
}

static int test_full_scale_hf(void)
{
    unsigned long source_frames = 48000;
    short *pcm = make_sine(48000, source_frames, 2, 20000.0, 0.95);
    Encoded encoded;
    short *decoded = NULL;
    unsigned long decoded_frames = 0;
    double snr;

    CHECK(pcm != NULL);
    if (encode_all(48000, 2, source_frames, pcm, 24, 1, 24, 80,
                   &encoded) != 0) {
        free(pcm);
        return 1;
    }
    if (decode_all(&encoded, 48000, 2, &decoded, &decoded_frames) != 0) {
        free(pcm);
        free_encoded(&encoded);
        return 1;
    }
    CHECK(decoded_frames == 48000);
    snr = snr_between(decoded, decoded_frames, pcm, source_frames, 2, 128);
    CHECK(snr >= 8.0);
    free(decoded);
    free(pcm);
    free_encoded(&encoded);
    return 0;
}

static int parse_chunk(const unsigned char *payload, size_t size,
                       unsigned long rate, int channels)
{
    static const unsigned char rle_length[16] = {
        2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64
    };
    Bink2AudioInfo info;
    Bink2Bits bits;
    unsigned long blocks = 0;

    CHECK(bink2_audio_describe(rate, channels, 1, 1, &info) == 0);
    bink2_bits_init(&bits, payload, size);
    CHECK(bink2_bits_left(&bits) >= 32);
    bink2_bits_skip(&bits, 32);
    while (bink2_bits_left(&bits) > 0) {
        int ch;

        CHECK(bink2_bits_read(&bits, 2) == 0);
        for (ch = 0; ch < channels; ch++) {
            int band;
            int i;

            bink2_bits_skip(&bits, 5 + 23 + 1);
            bink2_bits_skip(&bits, 5 + 23 + 1);
            for (band = 0; band < info.num_bands; band++)
                CHECK(bink2_bits_read(&bits, 7) >= 0);
            i = 2;
            while (i < info.frame_len) {
                unsigned long magnitudes[8];
                int j;
                int width;
                int k;

                if (bink2_bits_read(&bits, 1)) {
                    int entry = bink2_bits_read(&bits, 4);

                    CHECK(entry >= 0);
                    j = i + rle_length[entry] * 8;
                } else {
                    j = i + 8;
                }
                if (j > info.frame_len)
                    j = info.frame_len;
                width = bink2_bits_read(&bits, 4);
                CHECK(width >= 0);
                if (width == 0) {
                    i = j;
                    continue;
                }
                for (k = i; k < j; k++) {
                    magnitudes[k - i] =
                        (unsigned long)bink2_bits_read(&bits, width);
                    CHECK(!bink2_bits_failed(&bits));
                }
                for (k = i; k < j; k++)
                    if (magnitudes[k - i] != 0)
                        CHECK(bink2_bits_read(&bits, 1) >= 0);
                i = j;
            }
        }
        while ((bits.bitpos & 31) != 0)
            CHECK(bink2_bits_read(&bits, 1) == 0);
        blocks++;
        if (bink2_bits_left(&bits) == 0)
            break;
    }
    CHECK(!bink2_bits_failed(&bits));
    CHECK(bits.bitpos == size * 8);
    CHECK(blocks != 0);
    return 0;
}

static int test_bit_positions(void)
{
    unsigned long source_frames = 6000;
    short *pcm = make_sine(48000, source_frames, 2, 1000.0, 0.5);
    Encoded encoded;

    CHECK(pcm != NULL);
    if (encode_all(48000, 2, source_frames, pcm, 24, 1, 3, 80,
                   &encoded) != 0) {
        free(pcm);
        return 1;
    }
    CHECK(parse_chunk(encoded.payload[0], encoded.payload_size[0], 48000,
                      2) == 0);
    free(pcm);
    free_encoded(&encoded);
    return 0;
}

static int test_zero_blocks(void)
{
    unsigned long source_frames = 600;
    short *pcm = make_sine(8000, source_frames, 1, 440.0, 0.5);
    Encoded encoded;
    short *decoded = NULL;
    unsigned long decoded_frames = 0;
    double snr;

    CHECK(pcm != NULL);
    if (encode_all(8000, 1, source_frames, pcm, 60, 1, 4, 80,
                   &encoded) != 0) {
        free(pcm);
        return 1;
    }
    CHECK(encoded.payload[1] == NULL);
    CHECK(encoded.payload_size[1] == 0);
    CHECK(encoded.pcm_bytes[1] == 0);
    CHECK(encoded.payload[2] == NULL);
    CHECK(encoded.payload_size[2] == 0);
    CHECK(encoded.pcm_bytes[2] == 0);
    if (decode_all(&encoded, 8000, 1, &decoded, &decoded_frames) != 0) {
        free(pcm);
        free_encoded(&encoded);
        return 1;
    }
    CHECK(decoded_frames == 960);
    snr = snr_between(decoded, decoded_frames, pcm, source_frames, 1,
                      overlap_for(8000));
    CHECK(snr >= 25.0);
    free(decoded);
    free(pcm);
    free_encoded(&encoded);
    return 0;
}

int main(void)
{
    CHECK(test_one_block() == 0);
    CHECK(test_silence() == 0);
    CHECK(test_dc() == 0);
    CHECK(test_three_frames() == 0);
    CHECK(test_rate_boundaries() == 0);
    CHECK(test_quality() == 0);
    CHECK(test_silence_size() == 0);
    CHECK(test_full_scale_hf() == 0);
    CHECK(test_bit_positions() == 0);
    CHECK(test_zero_blocks() == 0);
    return 0;
}
