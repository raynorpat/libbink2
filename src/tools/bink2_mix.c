#include "bink2_mix.h"

#include <string.h>

unsigned long bink2_mix_frame(const bink2 object, short *out,
                              unsigned long capacity, unsigned char tracks)
{
    unsigned long frames = 0;
    unsigned char channels[7] = { 0 };
    unsigned t;

    memset(out, 0, capacity * 2 * sizeof(short));
    bink2_info_audio(object, NULL, channels, NULL, NULL);

    for (t = 0; t < tracks; t++) {
        const unsigned char *pcm = bink2_get_audio(object, (unsigned char)t);
        unsigned long bytes = bink2_get_audio_size(object, (unsigned char)t);
        unsigned long samples, source_frames, i;
        int ch = channels[t] ? channels[t] : 1;

        if (pcm == NULL || bytes < 2)
            continue;
        samples = bytes / 2;
        source_frames = samples / (unsigned long)ch;
        if (source_frames > capacity)
            source_frames = capacity;
        if (source_frames > frames)
            frames = source_frames;
        for (i = 0; i < source_frames; i++) {
            int left, right;

            if (ch == 1) {
                left = right = (short)(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
            } else {
                left = (short)(pcm[i * 4] | (pcm[i * 4 + 1] << 8));
                right = (short)(pcm[i * 4 + 2] | (pcm[i * 4 + 3] << 8));
            }
            left += out[i * 2];
            right += out[i * 2 + 1];
            if (left > 32767)
                left = 32767;
            else if (left < -32768)
                left = -32768;
            if (right > 32767)
                right = 32767;
            else if (right < -32768)
                right = -32768;
            out[i * 2] = (short)left;
            out[i * 2 + 1] = (short)right;
        }
    }
    return frames;
}
