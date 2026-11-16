#include "bink2_internal.h"

#include <stdlib.h>
#include <string.h>

#define BINK2_HEADER_SIZE 44UL

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] |
           ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static int read_at(const bink2 b, unsigned long off, void *dst,
                   unsigned long count)
{
    if (b->mode == BINK2_MODE_MEMORY) {
        if (off > b->mem_size || count > b->mem_size - off)
            return -1;
        memcpy(dst, b->mem + off, count);
        return 0;
    }
    if (b->file == NULL)
        return -1;
    if (fseek(b->file, (long)off, SEEK_SET) != 0)
        return -1;
    return fread(dst, 1, count, b->file) == count ? 0 : -1;
}

static int read_u32(const bink2 b, unsigned long off, unsigned long *value)
{
    unsigned char buf[4];

    if (read_at(b, off, buf, sizeof(buf)) != 0)
        return -1;
    *value = le32(buf);
    return 0;
}

static int parse_container(bink2 b)
{
    unsigned char hdr[BINK2_HEADER_SIZE];
    unsigned long declared;
    unsigned long off = BINK2_HEADER_SIZE;
    unsigned long index_end;
    unsigned long end_offset = 0;
    unsigned long prev = 0;
    unsigned long i;
    unsigned t;

    if (read_at(b, 0, hdr, sizeof(hdr)) != 0)
        return -1;

    if (hdr[0] != 'K' || hdr[1] != 'B' || hdr[2] != '2')
        return -1;
    if (hdr[3] < 'f' || hdr[3] > 'n')
        return -1;
    b->version = hdr[3];

    declared = le32(hdr + 4) + 8UL;
    b->file_size = declared;
    b->frame_count = le32(hdr + 8);
    b->largest_frame = le32(hdr + 12);
    b->width = le32(hdr + 20);
    b->height = le32(hdr + 24);
    b->fps_num = le32(hdr + 28);
    b->fps_den = le32(hdr + 32);
    b->flags = le32(hdr + 36);
    b->audio_tracks = (int)le32(hdr + 40);

    if (declared < BINK2_HEADER_SIZE)
        return -1;
    if (b->width == 0 || b->width > BINK2_MAX_WIDTH ||
        b->height == 0 || b->height > BINK2_MAX_HEIGHT)
        return -1;
    if (b->frame_count == 0 || b->frame_count > BINK2_MAX_FRAMES)
        return -1;
    if (b->fps_num == 0 || b->fps_den == 0)
        return -1;
    if (b->audio_tracks < 0 || b->audio_tracks > BINK2_MAX_AUDIO_TRACKS)
        return -1;

    if (b->version >= 'i')
        off += 4;
    if (off > b->actual_size)
        return -1;

    for (t = 0; t < (unsigned)b->audio_tracks; t++) {
        unsigned long value;

        if (read_u32(b, off, &value) != 0)
            return -1;
        b->audio[t].max_decoded = value;
        b->audio[t].bitdepth = 16;
        off += 4;
    }
    for (t = 0; t < (unsigned)b->audio_tracks; t++) {
        unsigned char desc[4];

        if (read_at(b, off, desc, sizeof(desc)) != 0)
            return -1;
        b->audio[t].rate = (unsigned long)desc[0] |
                           ((unsigned long)desc[1] << 8) |
                           ((unsigned long)desc[2] << 16);
        b->audio[t].flags = desc[3];
        b->audio[t].channels = (desc[3] & 0x20) ? 2 : 1;
        if (b->audio[t].rate == 0)
            return -1;
        off += 4;
    }
    off += 4UL * (unsigned long)b->audio_tracks;

    index_end = off + 4UL * (b->frame_count + 1UL);
    if (index_end < off)
        return -1;

    b->frame_offsets = (unsigned long *)calloc(b->frame_count,
                                               sizeof(unsigned long));
    b->frame_sizes = (unsigned long *)calloc(b->frame_count,
                                             sizeof(unsigned long));
    b->frame_keyframes = (unsigned char *)calloc(b->frame_count, 1);
    if (b->frame_offsets == NULL || b->frame_sizes == NULL ||
        b->frame_keyframes == NULL)
        return -1;

    for (i = 0; i <= b->frame_count; i++) {
        unsigned long value;
        unsigned long start;

        if (read_u32(b, off + 4UL * i, &value) != 0)
            return -1;
        start = value & ~1UL;
        if (i == 0) {
            if (start < index_end)
                return -1;
        } else if (start < prev) {
            return -1;
        }
        if (i < b->frame_count) {
            b->frame_offsets[i] = start;
            b->frame_keyframes[i] = (unsigned char)(value & 1UL);
        } else {
            end_offset = start;
        }
        prev = start;
    }

    for (i = 0; i < b->frame_count; i++) {
        unsigned long end = (i + 1 < b->frame_count)
                                ? b->frame_offsets[i + 1]
                                : end_offset;

        b->frame_sizes[i] = end > b->frame_offsets[i]
                                ? end - b->frame_offsets[i]
                                : 0;
    }

    b->current_frame = 0;
    b->video_enabled = 0;
    return 0;
}

bink2 bink2_open_memory(const unsigned char *buffer, unsigned long size)
{
    bink2 object;

    if (buffer == NULL || size < BINK2_HEADER_SIZE)
        return NULL;
    object = (bink2)calloc(1, sizeof(*object));
    if (object == NULL)
        return NULL;
    object->mem = (unsigned char *)buffer;
    object->mem_size = size;
    object->actual_size = size;
    object->mode = BINK2_MODE_MEMORY;
    if (parse_container(object) != 0) {
        bink2_close(object);
        return NULL;
    }
    return object;
}

bink2 bink2_open_filepointer(FILE *file, unsigned char mode)
{
    bink2 object;
    long end;

    (void)mode;
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0)
        return NULL;
    end = ftell(file);
    if (end < 0)
        return NULL;
    if (fseek(file, 0, SEEK_SET) != 0)
        return NULL;

    object = (bink2)calloc(1, sizeof(*object));
    if (object == NULL)
        return NULL;
    object->file = file;
    object->mode = BINK2_MODE_DISK;
    object->owns_file = 0;
    object->actual_size = (unsigned long)end;
    if (parse_container(object) != 0) {
        bink2_close(object);
        return NULL;
    }
    return object;
}

bink2 bink2_open_file(const char *filename, unsigned char mode)
{
    FILE *file;
    bink2 object;

    if (filename == NULL)
        return NULL;
    file = fopen(filename, "rb");
    if (file == NULL)
        return NULL;
    object = bink2_open_filepointer(file, mode);
    if (object == NULL) {
        fclose(file);
        return NULL;
    }
    object->owns_file = 1;
    return object;
}

void bink2_close(bink2 object)
{
    if (object == NULL)
        return;
    free(object->frame_offsets);
    free(object->frame_sizes);
    free(object->frame_keyframes);
    free(object->frame_data);
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
    if (usf != NULL) {
        if (object->fps_num != 0)
            *usf = (double)object->fps_den * 1000000.0 /
                   (double)object->fps_num;
        else
            *usf = 0.0;
    }
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
    if (object == NULL || track >= object->audio_tracks)
        return BINK2_ERROR;
    object->audio[track].enabled = enable ? 1 : 0;
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
