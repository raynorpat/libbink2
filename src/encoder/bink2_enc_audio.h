#ifndef LIBBINK2_ENC_AUDIO_H
#define LIBBINK2_ENC_AUDIO_H

#include <stddef.h>

typedef struct Bink2EncAudio Bink2EncAudio;

/**
 * Open one Bink Audio 2 DCT track. `sample_count` is per channel. Audio runs
 * for the video duration; samples past it are ignored, missing samples are
 * silence. `max_decoded` receives the decoder's allocation bound.
 */
int bink2_enc_audio_open(Bink2EncAudio **out, unsigned long rate,
                         int channels, unsigned long sample_count,
                         unsigned long fps_num, unsigned long fps_den,
                         unsigned long frame_count, int quality,
                         unsigned long *max_decoded);
void bink2_enc_audio_close(Bink2EncAudio *audio);

/**
 * Encode the next video frame's audio chunk. `read_audio` fills interleaved
 * host-endian s16 for [start_sample, start_sample + count), where count is
 * per channel. A frame with no whole block returns a NULL payload.
 */
int bink2_enc_audio_frame(Bink2EncAudio *audio,
                          int (*read_audio)(void *user,
                                            unsigned long start_sample,
                                            unsigned long sample_count,
                                            short *dst),
                          void *user, int is_last,
                          unsigned char **payload, size_t *payload_size,
                          size_t *pcm_bytes);

#endif
