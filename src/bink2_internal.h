#ifndef LIBBINK2_INTERNAL_H
#define LIBBINK2_INTERNAL_H

#include "bink2.h"

#define BINK2_MAX_AUDIO_TRACKS 7
#define BINK2_MAX_FRAMES 1000000UL
#define BINK2_MAX_WIDTH 7680UL
#define BINK2_MAX_HEIGHT 4800UL

struct Bink2Audio;
struct Bink2Video;

typedef struct {
    unsigned long rate;
    unsigned char channels;
    unsigned char bitdepth;
    unsigned char flags;
    unsigned char enabled;
    unsigned long max_decoded;
    struct Bink2Audio *decoder;
    const unsigned char *payload;
    unsigned long payload_size;
} Bink2AudioTrack;

struct bink2_t {
    /* input source */
    FILE *file;
    unsigned char *mem;
    unsigned long mem_size;
    unsigned char mode;
    unsigned char owns_file;

    /* container metadata */
    unsigned long file_size;
    unsigned long actual_size;
    int version; /* 'f' .. 'n' */
    unsigned long width;
    unsigned long height;
    unsigned long frame_count;
    unsigned long largest_frame;
    unsigned long fps_num;
    unsigned long fps_den;
    unsigned long flags;
    int audio_tracks;
    Bink2AudioTrack audio[BINK2_MAX_AUDIO_TRACKS];

    /* frame index */
    unsigned long *frame_offsets;
    unsigned long *frame_sizes;
    unsigned char *frame_keyframes;

    /* current frame packet */
    unsigned char *frame_data;
    unsigned long frame_data_size;
    const unsigned char *video_data;
    unsigned long video_size;

    /* playback state */
    unsigned long current_frame;
    unsigned char video_enabled;
    struct Bink2Video *video;
    int thread_limit;   /* see bink2_internal_set_threads */
};

int bink2_internal_load_frame(bink2 object, unsigned long frame);
/* Slice threads per frame: 0 = automatic (BINK2_THREADS, else the CPU
 * count), 1 = serial, n > 1 = up to n. For tests and tools. */
void bink2_internal_set_threads(bink2 object, int n);

#endif
