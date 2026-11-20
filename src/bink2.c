#include "bink2_internal.h"
#include "bink2_audio.h"
#include "bink2_video.h"

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

static int parse_container(bink2 b, int report_alpha)
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
    /* The KB2g+ alpha syntax is not ported yet; reject the stream instead
     * of decoding it with the alpha plane half-written. */
    if ((b->flags & BINK2_FLAG_ALPHA) && b->version > 'f') {
        if (report_alpha)
            fputs("libbink2: KB2g+ alpha streams are not supported\n", stderr);
        return -1;
    }

    if (declared < BINK2_HEADER_SIZE)
        return -1;
    if (b->width == 0 || b->width > BINK2_MAX_WIDTH ||
        b->height == 0 || b->height > BINK2_MAX_HEIGHT)
        return -1;
    if (b->frame_count == 0 || b->frame_count > BINK2_MAX_FRAMES)
        return -1;
    if (b->fps_num == 0 || b->fps_den == 0)
        return -1;
    {
        int tracks = (int)le32(hdr + 40);

        if (tracks < 0 || tracks > BINK2_MAX_AUDIO_TRACKS)
            return -1;
        b->audio_tracks = tracks;
    }

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
        b->audio[t].bitdepth = 16;
        if (b->audio[t].rate == 0)
            return -1;
        if (bink2_audio_create(&b->audio[t].decoder, b->audio[t].rate,
                               b->audio[t].channels,
                               (desc[3] & 0x10) ? 1 : 0, 1,
                               b->audio[t].max_decoded) != 0)
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
    if (parse_container(object, 0) != 0) {
        bink2_close(object);
        return NULL;
    }
    return object;
}

static bink2 open_stream(FILE *file, unsigned char mode, int report_alpha)
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
    if (parse_container(object, report_alpha) != 0) {
        bink2_close(object);
        return NULL;
    }
    return object;
}

bink2 bink2_open_filepointer(FILE *file, unsigned char mode)
{
    return open_stream(file, mode, 0);
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
    object = open_stream(file, mode, 1);
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
    {
        int i;
        int tracks = object->audio_tracks;

        if (tracks < 0 || tracks > BINK2_MAX_AUDIO_TRACKS)
            tracks = 0;
        for (i = 0; i < tracks; i++)
            bink2_audio_destroy(object->audio[i].decoder);
    }
    free(object->frame_offsets);
    free(object->frame_sizes);
    free(object->frame_keyframes);
    free(object->frame_data);
    if (object->video != NULL) {
        bink2_video_uninit(object->video);
        free(object->video);
    }
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

int bink2_internal_load_frame(bink2 object, unsigned long frame)
{
    unsigned long start;
    unsigned long nominal_end;
    unsigned long size;
    unsigned long pos;
    int t;

    if (object == NULL)
        return BINK2_ERROR;
    if (frame >= object->frame_count)
        return BINK2_LAST;

    start = object->frame_offsets[frame];
    if (start >= object->actual_size)
        return BINK2_LAST;
    if (object->frame_sizes[frame] == 0)
        return BINK2_ERROR;

    nominal_end = start + object->frame_sizes[frame];
    if (nominal_end < start || nominal_end > object->actual_size)
        return BINK2_ERROR;
    size = nominal_end - start;
    if (size == 0)
        return BINK2_ERROR;

    if (size > object->frame_data_size) {
        unsigned char *grown = (unsigned char *)realloc(object->frame_data,
                                                        (size_t)size);

        if (grown == NULL)
            return BINK2_ERROR;
        object->frame_data = grown;
        object->frame_data_size = size;
    }
    if (read_at(object, start, object->frame_data, size) != 0)
        return BINK2_ERROR;

    pos = 0;
    for (t = 0; t < object->audio_tracks; t++) {
        unsigned long chunk;

        if (pos + 4 > size)
            return BINK2_ERROR;
        chunk = le32(object->frame_data + pos);
        pos += 4;
        if (chunk > size - pos)
            return BINK2_ERROR;
        object->audio[t].payload = chunk != 0 ? object->frame_data + pos : NULL;
        object->audio[t].payload_size = chunk;
        pos += chunk;
    }

    object->video_data = object->frame_data + pos;
    object->video_size = size - pos;
    if (object->video_size == 0)
        return BINK2_ERROR;
    return BINK2_OK;
}

static int ensure_video(bink2 object)
{
    if (!object->video_enabled)
        return -1;
    if (object->video == NULL) {
        object->video = (Bink2Video *)calloc(1, sizeof(Bink2Video));
        if (object->video == NULL)
            return -1;
        if (bink2_video_init(object->video, object->width, object->height,
                             object->flags, object->version) != 0) {
            free(object->video);
            object->video = NULL;
            return -1;
        }
        bink2_video_set_threads(object->video, object->thread_limit);
    }
    return 0;
}

void bink2_internal_set_threads(bink2 object, int n)
{
    if (object == NULL || n < 0)
        return;
    object->thread_limit = n;
    if (object->video != NULL)
        bink2_video_set_threads(object->video, n);
}

static void reset_audio(bink2 object)
{
    int i;

    for (i = 0; i < object->audio_tracks; i++)
        bink2_audio_reset(object->audio[i].decoder);
}

static char decode_frame(bink2 object, unsigned long frame)
{
    int status = bink2_internal_load_frame(object, frame);

    if (status != BINK2_OK)
        return (char)status;
    if (object->video_enabled) {
        if (ensure_video(object) != 0)
            return BINK2_ERROR;
        if (bink2_video_decode(object->video, object->video_data,
                               object->video_size,
                               object->frame_keyframes[frame]) != 0)
            return BINK2_ERROR;
    }
    {
        int i;

        for (i = 0; i < object->audio_tracks; i++) {
            Bink2AudioTrack *track = &object->audio[i];

            if (!track->enabled || track->decoder == NULL)
                continue;
            if (bink2_audio_decode(track->decoder, track->payload,
                                   track->payload_size) != 0) {
                /* Leave the other tracks and the picture running. */
            }
        }
    }
    object->current_frame = frame;
    return frame + 1 < object->frame_count ? BINK2_MORE : BINK2_LAST;
}

char bink2_first(bink2 object)
{
    if (object == NULL || object->frame_count == 0)
        return BINK2_ERROR;
    reset_audio(object);
    return decode_frame(object, 0);
}

char bink2_next(bink2 object)
{
    unsigned long next;
    char status;

    if (object == NULL)
        return BINK2_ERROR;
    if (object->current_frame + 1 >= object->frame_count)
        return BINK2_LAST;
    next = object->current_frame + 1;
    status = decode_frame(object, next);
    if (status == BINK2_ERROR)
        object->current_frame = next;
    return status;
}

char bink2_seek_keyframe(bink2 object, unsigned long frame)
{
    unsigned long target;
    unsigned long max_frame;

    if (object == NULL || object->frame_count == 0)
        return BINK2_ERROR;
    max_frame = object->frame_count;
    while (max_frame > 0) {
        unsigned long idx = max_frame - 1;
        unsigned long off = object->frame_offsets[idx];
        unsigned long sz = object->frame_sizes[idx];

        if (off < object->actual_size &&
            sz <= object->actual_size - off)
            break;
        max_frame--;
    }
    if (max_frame == 0)
        return BINK2_ERROR;
    if (frame >= max_frame)
        frame = max_frame - 1;
    target = frame;
    while (target > 0 && !object->frame_keyframes[target])
        target--;
    reset_audio(object);
    return decode_frame(object, target);
}

const unsigned char *bink2_get_video(const bink2 object)
{
    if (object == NULL || object->video == NULL)
        return NULL;
    return bink2_video_rgb(object->video);
}

const unsigned char *bink2_get_plane(const bink2 object, int plane)
{
    if (object == NULL || object->video == NULL)
        return NULL;
    return bink2_video_plane(object->video, plane);
}

int bink2_get_stride(const bink2 object, int plane)
{
    if (object == NULL || object->video == NULL)
        return 0;
    return bink2_video_stride(object->video, plane);
}

const unsigned char *bink2_get_audio(const bink2 object, unsigned char track)
{
    if (object == NULL || track >= object->audio_tracks ||
        !object->audio[track].enabled || object->audio[track].decoder == NULL)
        return NULL;
    return bink2_audio_pcm(object->audio[track].decoder);
}

unsigned long bink2_get_audio_size(const bink2 object, unsigned char track)
{
    if (object == NULL || track >= object->audio_tracks ||
        !object->audio[track].enabled || object->audio[track].decoder == NULL)
        return 0;
    return bink2_audio_pcm_bytes(object->audio[track].decoder);
}

int bink2_get_motion(const bink2 object, unsigned long *mb_w,
                     unsigned long *mb_h, const int16_t **mv_x,
                     const int16_t **mv_y, const unsigned char **mv_count)
{
    if (object == NULL || object->video == NULL)
        return 0;
    return bink2_video_motion(object->video, mb_w, mb_h, mv_x, mv_y,
                              mv_count);
}
