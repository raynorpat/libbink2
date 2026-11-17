#ifndef BINK2_PLAYBACK_H
#define BINK2_PLAYBACK_H

/* Audio output and seek glue shared by every player. The device layer is
 * provided per backend by bink2_playback_sdl.c or bink2_playback_win32.c. */

#include "bink2.h"

typedef struct {
    int ready;
    int rate;
    int channels;
    void *device; /* backend context */
} Bink2Playback;

int bink2_playback_audio_subsystem(void);
int bink2_playback_audio_open(Bink2Playback *p, int rate, int channels);
void bink2_playback_audio_close(Bink2Playback *p);
void bink2_playback_audio_clear(Bink2Playback *p);
unsigned long bink2_playback_queued(const Bink2Playback *p);
void bink2_playback_queue(Bink2Playback *p, const void *data, unsigned long bytes);
void bink2_playback_set_paused(Bink2Playback *p, int pause_on, int *device_paused);
/* Mix the current frame's enabled tracks and queue them. track_channels and
 * track_rates come from bink2_info_audio. total_bytes may be NULL. */
void bink2_playback_publish(bink2 object, const int enabled[7],
                           const unsigned char track_channels[7],
                           const unsigned long track_rates[7],
                           Bink2Playback *p, unsigned long *total_bytes);
/* Decode forward to target, rebuilding the audio queue from the seek origin. */
int bink2_playback_seek(bink2 object, unsigned long target, char *status,
                       Bink2Playback *p, double usf, const int enabled[7],
                       const unsigned char track_channels[7],
                       const unsigned long track_rates[7]);
/* Release the mix buffer; call once at exit. */
void bink2_playback_shutdown(void);

#endif
