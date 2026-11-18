#ifndef LIBBINK2_ENC_WAV_H
#define LIBBINK2_ENC_WAV_H

#include <stddef.h>

/**
 * PCM WAV loaded as interleaved host-endian signed 16-bit samples.
 * `sample_count` is per channel.
 */
typedef struct {
    unsigned long rate;
    int channels;
    unsigned long sample_count;
    short *pcm;
} Bink2EncWav;

/**
 * Load a PCM RIFF WAV. Accepts 8, 16, or 24-bit integer samples, mono or
 * stereo, at 1..65535 Hz. Returns 0 on success or -1 with a message in `err`.
 */
int bink2_enc_wav_load(const char *path, Bink2EncWav *wav, char *err,
                       size_t errlen);
void bink2_enc_wav_free(Bink2EncWav *wav);

#endif
