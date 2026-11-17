#include "bink2_audio.h"
#include "test_common.h"

static int test_describe(void)
{
    Bink2AudioInfo info;

    CHECK(bink2_audio_describe(44100, 1, 1, 1, &info) == 0);
    CHECK(info.use_dct == 1);
    CHECK(info.channels == 1);
    CHECK(info.frame_len == 2048);
    CHECK(info.overlap_len == 128);
    CHECK(info.num_bands > 0 && info.num_bands <= 26);

    CHECK(bink2_audio_describe(48000, 2, 1, 1, &info) == 0);
    CHECK(info.channels == 2);
    CHECK(info.frame_len == 2048);

    /* RDFT folds stereo into one channel and doubles the transform size. */
    CHECK(bink2_audio_describe(48000, 2, 0, 1, &info) == 0);
    CHECK(info.use_dct == 0);
    CHECK(info.channels == 1);
    CHECK(info.frame_len == 4096);
    CHECK(info.overlap_len == 256);

    CHECK(bink2_audio_describe(44100, 3, 1, 1, &info) != 0);
    return 0;
}

static int test_chunk_lengths(void)
{
    Bink2Audio *audio = NULL;
    unsigned char tiny[2] = { 0, 0 };

    CHECK(bink2_audio_create(&audio, 48000, 2, 1, 1, 1UL << 20) == 0);
    CHECK(bink2_audio_decode(audio, NULL, 0) == 0);
    CHECK(bink2_audio_pcm_bytes(audio) == 0);
    CHECK(bink2_audio_decode(audio, tiny, sizeof(tiny)) != 0);
    bink2_audio_destroy(audio);
    return 0;
}

static int test_rdft_silence(void)
{
    Bink2Audio *audio = NULL;
    unsigned char zeros[256];
    unsigned long bytes;
    const unsigned char *pcm;
    unsigned long block_bytes;
    unsigned long i;

    /* A zero bitstream codes silence: zero DC floats, zero quantiser
     * indices, no run, zero coefficient width. This executes the RDFT
     * transform, which no shipped sample uses. */
    memset(zeros, 0, sizeof(zeros));
    CHECK(bink2_audio_create(&audio, 48000, 2, 0, 1, 1UL << 20) == 0);
    CHECK(bink2_audio_decode(audio, zeros, sizeof(zeros)) == 0);
    pcm = bink2_audio_pcm(audio);
    bytes = bink2_audio_pcm_bytes(audio);
    block_bytes = (4096UL - 256UL) * 2UL;
    CHECK(pcm != NULL);
    CHECK(bytes > 0);
    CHECK(bytes % block_bytes == 0);
    for (i = 0; i < bytes; i++)
        CHECK(pcm[i] == 0);
    CHECK(bink2_audio_decode(audio, NULL, 0) == 0);
    CHECK(bink2_audio_pcm_bytes(audio) == 0);
    bink2_audio_destroy(audio);
    return 0;
}

int main(void)
{
    CHECK(test_describe() == 0);
    CHECK(test_chunk_lengths() == 0);
    CHECK(test_rdft_silence() == 0);
    return 0;
}
