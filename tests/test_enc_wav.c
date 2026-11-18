#include "bink2_enc_wav.h"
#include "test_common.h"

#include <stdio.h>

#define TMP_WAV "test_enc_wav_tmp.wav"

typedef struct {
    unsigned tag;
    unsigned bits;
    unsigned channels;
    unsigned long rate;
    unsigned long claimed_data;
    const unsigned char *payload;
    size_t payload_size;
    int with_fmt;
    int junk;
    int second_fmt;
    unsigned second_tag;
    int oversized_tail;
} WavSpec;

static void put16(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)(value & 0xFF);
    p[1] = (unsigned char)((value >> 8) & 0xFF);
}

static void put32(unsigned char *p, unsigned long value)
{
    p[0] = (unsigned char)(value & 0xFF);
    p[1] = (unsigned char)((value >> 8) & 0xFF);
    p[2] = (unsigned char)((value >> 16) & 0xFF);
    p[3] = (unsigned char)((value >> 24) & 0xFF);
}

static int write_at(FILE *file, unsigned long offset, const void *data,
                    size_t size)
{
    if (fseek(file, (long)offset, SEEK_SET) != 0)
        return -1;
    return fwrite(data, 1, size, file) == size ? 0 : -1;
}

static int write_wav(const WavSpec *spec)
{
    FILE *file = fopen(TMP_WAV, "wb");
    unsigned long riff;
    unsigned long offset = 12;
    unsigned char header[4];
    unsigned pad;

    if (file == NULL)
        return -1;
    riff = 4;
    if (spec->with_fmt)
        riff += 8 + 16;
    if (spec->second_fmt)
        riff += 8 + 16;
    if (spec->junk)
        riff += 8 + 4;
    riff += 8 + spec->claimed_data + (spec->claimed_data & 1);
    if (spec->oversized_tail)
        riff += 8;
    if (fwrite("RIFF", 1, 4, file) != 4) {
        fclose(file);
        return -1;
    }
    put32(header, riff);
    if (write_at(file, 4, header, 4) != 0) {
        fclose(file);
        return -1;
    }
    if (fwrite("WAVE", 1, 4, file) != 4) {
        fclose(file);
        return -1;
    }
    if (spec->with_fmt) {
        unsigned block = spec->channels * (spec->bits / 8);
        unsigned char fmt[16];

        put16(fmt + 0, spec->tag);
        put16(fmt + 2, spec->channels);
        put32(fmt + 4, spec->rate);
        put32(fmt + 8, spec->rate * block);
        put16(fmt + 12, block);
        put16(fmt + 14, spec->bits);
        if (write_at(file, offset, "fmt ", 4) != 0) {
            fclose(file);
            return -1;
        }
        put32(header, 16);
        if (write_at(file, offset + 4, header, 4) != 0 ||
            write_at(file, offset + 8, fmt, 16) != 0) {
            fclose(file);
            return -1;
        }
        offset += 24;
    }
    if (spec->second_fmt) {
        unsigned block = spec->channels * (spec->bits / 8);
        unsigned char fmt[16];

        put16(fmt + 0, spec->second_tag);
        put16(fmt + 2, spec->channels);
        put32(fmt + 4, spec->rate);
        put32(fmt + 8, spec->rate * block);
        put16(fmt + 12, block);
        put16(fmt + 14, spec->bits);
        if (write_at(file, offset, "fmt ", 4) != 0) {
            fclose(file);
            return -1;
        }
        put32(header, 16);
        if (write_at(file, offset + 4, header, 4) != 0 ||
            write_at(file, offset + 8, fmt, 16) != 0) {
            fclose(file);
            return -1;
        }
        offset += 24;
    }
    if (spec->junk) {
        static const unsigned char junk[4] = { 'x', 'y', 'z', 0 };

        if (write_at(file, offset, "JUNK", 4) != 0) {
            fclose(file);
            return -1;
        }
        put32(header, 3);
        if (write_at(file, offset + 4, header, 4) != 0 ||
            write_at(file, offset + 8, junk, 4) != 0) {
            fclose(file);
            return -1;
        }
        offset += 12;
    }
    if (write_at(file, offset, "data", 4) != 0) {
        fclose(file);
        return -1;
    }
    {
        unsigned char size[4];

        put32(size, spec->claimed_data);
        if (write_at(file, offset + 4, size, 4) != 0)
            goto fail;
    }
    offset += 8;
    if (spec->payload_size != 0 &&
        write_at(file, offset, spec->payload, spec->payload_size) != 0)
        goto fail;
    offset += (unsigned long)spec->payload_size;
    pad = (unsigned)((spec->payload_size & 1) != 0);
    if (pad) {
        unsigned char zero = 0;

        if (write_at(file, offset, &zero, 1) != 0)
            goto fail;
        offset++;
    }
    if (spec->oversized_tail) {
        if (write_at(file, offset, "JUNK", 4) != 0)
            goto fail;
        put32(header, 0xFFFFFF00UL);
        if (write_at(file, offset + 4, header, 4) != 0)
            goto fail;
    }
    return fclose(file) == 0 ? 0 : -1;

fail:
    fclose(file);
    return -1;
}

static int load_or_fail(const WavSpec *spec, Bink2EncWav *wav)
{
    char err[128] = "";

    CHECK(write_wav(spec) == 0);
    memset(wav, 0, sizeof(*wav));
    CHECK(bink2_enc_wav_load(TMP_WAV, wav, err, sizeof(err)) == 0);
    CHECK(err[0] == '\0');
    CHECK(wav->pcm != NULL);
    return 0;
}

static int finish(Bink2EncWav *wav)
{
    bink2_enc_wav_free(wav);
    remove(TMP_WAV);
    return 0;
}

static int test_16_bit_mono(void)
{
    static const unsigned char payload[10] = {
        0x00, 0x80, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0xFF, 0x7F
    };
    WavSpec spec = { 1, 16, 1, 44100, 10, payload, 10, 1, 0 };
    Bink2EncWav wav;

    if (load_or_fail(&spec, &wav) != 0)
        return 1;
    CHECK(wav.rate == 44100);
    CHECK(wav.channels == 1);
    CHECK(wav.sample_count == 5);
    CHECK(wav.pcm[0] == -32768);
    CHECK(wav.pcm[1] == -1);
    CHECK(wav.pcm[2] == 0);
    CHECK(wav.pcm[3] == 1);
    CHECK(wav.pcm[4] == 32767);
    return finish(&wav);
}

static int test_8_bit_stereo(void)
{
    static const unsigned char payload[4] = { 0x00, 0x80, 0xFF, 0x40 };
    WavSpec spec = { 1, 8, 2, 22050, 4, payload, 4, 1, 0 };
    Bink2EncWav wav;

    if (load_or_fail(&spec, &wav) != 0)
        return 1;
    CHECK(wav.rate == 22050);
    CHECK(wav.channels == 2);
    CHECK(wav.sample_count == 2);
    CHECK(wav.pcm[0] == -32768);
    CHECK(wav.pcm[1] == 0);
    CHECK(wav.pcm[2] == 32512);
    CHECK(wav.pcm[3] == -16384);
    return finish(&wav);
}

static int test_24_bit_stereo(void)
{
    static const unsigned char payload[24] = {
        0x00, 0x00, 0x00,  0xFF, 0xFF, 0x7F,
        0x00, 0x00, 0x80,  0x00, 0xFF, 0xFF,
        0x00, 0x01, 0x00,  0x00, 0xFF, 0xFF,
        0x00, 0x00, 0x80,  0xFF, 0xFF, 0x7F
    };
    WavSpec spec = { 1, 24, 2, 48000, 24, payload, 24, 1, 0 };
    Bink2EncWav wav;

    if (load_or_fail(&spec, &wav) != 0)
        return 1;
    CHECK(wav.rate == 48000);
    CHECK(wav.channels == 2);
    CHECK(wav.sample_count == 4);
    CHECK(wav.pcm[0] == 0);
    CHECK(wav.pcm[1] == 32767);
    CHECK(wav.pcm[2] == -32768);
    CHECK(wav.pcm[3] == -1);      /* -256 rounds to -1 */
    CHECK(wav.pcm[4] == 1);
    CHECK(wav.pcm[5] == -1);
    CHECK(wav.pcm[6] == -32768);
    CHECK(wav.pcm[7] == 32767);
    return finish(&wav);
}

static int test_odd_chunks(void)
{
    static const unsigned char payload[4] = { 0x34, 0x12, 0x78, 0x56 };
    WavSpec spec = { 1, 16, 1, 32000, 4, payload, 4, 1, 1 };
    Bink2EncWav wav;

    if (load_or_fail(&spec, &wav) != 0)
        return 1;
    CHECK(wav.sample_count == 2);
    CHECK(wav.pcm[0] == 0x1234);
    CHECK(wav.pcm[1] == 0x5678);
    return finish(&wav);
}

static int test_truncated_data(void)
{
    static const unsigned char payload[16] = { 0 };
    WavSpec spec = { 1, 16, 1, 44100, 64, payload, 16, 1, 0 };
    Bink2EncWav wav;
    char err[128] = "";

    CHECK(write_wav(&spec) == 0);
    memset(&wav, 0, sizeof(wav));
    CHECK(bink2_enc_wav_load(TMP_WAV, &wav, err, sizeof(err)) == -1);
    CHECK(err[0] != '\0');
    CHECK(wav.pcm == NULL);
    remove(TMP_WAV);
    return 0;
}

static int expect_reject(const WavSpec *spec)
{
    Bink2EncWav wav;
    char err[128] = "";

    CHECK(write_wav(spec) == 0);
    memset(&wav, 0, sizeof(wav));
    CHECK(bink2_enc_wav_load(TMP_WAV, &wav, err, sizeof(err)) == -1);
    CHECK(err[0] != '\0');
    CHECK(wav.pcm == NULL);
    remove(TMP_WAV);
    return 0;
}

static int test_second_fmt_ignored(void)
{
    static const unsigned char payload[4] = { 0x34, 0x12, 0x78, 0x56 };
    WavSpec spec = { 1, 16, 1, 44100, 4, payload, 4, 1, 0, 1, 3, 0 };
    Bink2EncWav wav;

    if (load_or_fail(&spec, &wav) != 0)
        return 1;
    CHECK(wav.sample_count == 2);
    CHECK(wav.pcm[0] == 0x1234);
    CHECK(wav.pcm[1] == 0x5678);
    return finish(&wav);
}

static int test_oversized_unknown_chunk(void)
{
    static const unsigned char payload[4] = { 0x34, 0x12, 0x78, 0x56 };
    WavSpec spec = { 1, 16, 1, 44100, 4, payload, 4, 1, 0, 0, 0, 1 };
    Bink2EncWav wav;
    char err[128] = "";

    CHECK(write_wav(&spec) == 0);
    memset(&wav, 0, sizeof(wav));
    CHECK(bink2_enc_wav_load(TMP_WAV, &wav, err, sizeof(err)) == -1);
    CHECK(err[0] != '\0');
    CHECK(wav.pcm == NULL);
    remove(TMP_WAV);
    return 0;
}

static int test_rejects(void)
{
    static const unsigned char payload[16] = { 0 };
    WavSpec spec = { 1, 16, 1, 44100, 16, payload, 16, 1, 0 };

    spec.tag = 3;
    if (expect_reject(&spec) != 0)
        return 1;
    spec.tag = 1;
    spec.channels = 3;
    if (expect_reject(&spec) != 0)
        return 1;
    spec.channels = 1;
    spec.rate = 0;
    if (expect_reject(&spec) != 0)
        return 1;
    spec.rate = 65536;
    if (expect_reject(&spec) != 0)
        return 1;
    spec.rate = 44100;
    spec.claimed_data = 0;
    spec.payload_size = 0;
    if (expect_reject(&spec) != 0)
        return 1;
    spec.claimed_data = 16;
    spec.payload_size = 16;
    spec.with_fmt = 0;
    return expect_reject(&spec);
}

int main(void)
{
    CHECK(test_16_bit_mono() == 0);
    CHECK(test_8_bit_stereo() == 0);
    CHECK(test_24_bit_stereo() == 0);
    CHECK(test_odd_chunks() == 0);
    CHECK(test_truncated_data() == 0);
    CHECK(test_second_fmt_ignored() == 0);
    CHECK(test_oversized_unknown_chunk() == 0);
    CHECK(test_rejects() == 0);
    return 0;
}
