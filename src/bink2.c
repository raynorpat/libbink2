#include "bink2_internal.h"

#include <stdlib.h>

bink2 bink2_open_file(const char *filename, unsigned char mode)
{
    (void)filename;
    (void)mode;
    return NULL;
}

bink2 bink2_open_filepointer(FILE *file, unsigned char mode)
{
    (void)file;
    (void)mode;
    return NULL;
}

bink2 bink2_open_memory(const unsigned char *buffer, unsigned long size)
{
    (void)buffer;
    (void)size;
    return NULL;
}

void bink2_close(bink2 object)
{
    if (object == NULL)
        return;
    free(object->frame_offsets);
    free(object->frame_sizes);
    free(object->frame_keyframes);
    free(object->frame_data);
    free(object->mem);
    if (object->owns_file && object->file != NULL)
        fclose(object->file);
    free(object);
}

char bink2_info_all(const bink2 object, unsigned long *frame,
                    unsigned long *frame_count, double *usf)
{
    if (object == NULL)
        return BINK2_ERROR;
    if (frame != NULL)
        *frame = object->current_frame;
    if (frame_count != NULL)
        *frame_count = object->frame_count;
    if (usf != NULL)
        *usf = 0.0;
    return 0;
}

char bink2_info_video(const bink2 object, unsigned long *w, unsigned long *h,
                      unsigned long *flags)
{
    if (object == NULL)
        return BINK2_ERROR;
    if (w != NULL)
        *w = object->width;
    if (h != NULL)
        *h = object->height;
    if (flags != NULL)
        *flags = object->flags;
    return 0;
}

char bink2_info_audio(const bink2 object, unsigned char *track_mask,
                      unsigned char channels[7], unsigned char bitdepth[7],
                      unsigned long audio_rate[7])
{
    int i;

    if (object == NULL)
        return BINK2_ERROR;
    if (track_mask != NULL) {
        *track_mask = 0;
        for (i = 0; i < object->audio_tracks; i++)
            *track_mask |= (unsigned char)(1u << i);
    }
    for (i = 0; i < BINK2_MAX_AUDIO_TRACKS; i++) {
        if (channels != NULL)
            channels[i] = object->audio[i].channels;
        if (bitdepth != NULL)
            bitdepth[i] = object->audio[i].bitdepth;
        if (audio_rate != NULL)
            audio_rate[i] = object->audio[i].rate;
    }
    return 0;
}

char bink2_enable_video(bink2 object, unsigned char enable)
{
    if (object == NULL)
        return BINK2_ERROR;
    object->video_enabled = enable ? 1 : 0;
    return 0;
}

char bink2_enable_audio(bink2 object, unsigned char track,
                        unsigned char enable)
{
    (void)enable;
    if (object == NULL || track >= object->audio_tracks)
        return BINK2_ERROR;
    return 0;
}

char bink2_first(bink2 object)
{
    if (object == NULL)
        return BINK2_ERROR;
    return BINK2_ERROR;
}

char bink2_next(bink2 object)
{
    if (object == NULL)
        return BINK2_ERROR;
    return BINK2_ERROR;
}

char bink2_seek_keyframe(bink2 object, unsigned long frame)
{
    (void)frame;
    if (object == NULL)
        return BINK2_ERROR;
    return BINK2_ERROR;
}

const unsigned char *bink2_get_video(const bink2 object)
{
    (void)object;
    return NULL;
}

const unsigned char *bink2_get_plane(const bink2 object, int plane)
{
    (void)object;
    (void)plane;
    return NULL;
}

int bink2_get_stride(const bink2 object, int plane)
{
    (void)object;
    (void)plane;
    return 0;
}

const unsigned char *bink2_get_audio(const bink2 object, unsigned char track)
{
    (void)object;
    (void)track;
    return NULL;
}

unsigned long bink2_get_audio_size(const bink2 object, unsigned char track)
{
    (void)object;
    (void)track;
    return 0;
}

int bink2_get_motion(const bink2 object, unsigned long *mb_w,
                     unsigned long *mb_h, const int16_t **mv_x,
                     const int16_t **mv_y, const unsigned char **mv_count)
{
    (void)object;
    (void)mb_w;
    (void)mb_h;
    (void)mv_x;
    (void)mv_y;
    (void)mv_count;
    return 0;
}
