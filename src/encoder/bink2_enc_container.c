#include "bink2_enc_container.h"

#include <stdint.h>

#define BINK2_HEADER_SIZE 48UL

static int write_u32(FILE *file, unsigned long value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xFF);
    bytes[1] = (unsigned char)((value >> 8) & 0xFF);
    bytes[2] = (unsigned char)((value >> 16) & 0xFF);
    bytes[3] = (unsigned char)((value >> 24) & 0xFF);
    return fwrite(bytes, 1, 4, file) == 4 ? 0 : -1;
}

int bink2_container_write_header(FILE *file, unsigned long width,
                                 unsigned long height, unsigned long fps_num,
                                 unsigned long fps_den,
                                 unsigned long frame_count,
                                 unsigned long flags,
                                 const Bink2EncAudioTrack *tracks,
                                 unsigned long track_count,
                                 unsigned long *index_offset)
{
    unsigned long i;

    if (file == NULL || frame_count == 0)
        return -1;
    if (track_count != 0 && tracks == NULL)
        return -1;
    if (track_count > (0xFFFFFFFFUL - BINK2_HEADER_SIZE) / 12UL)
        return -1;
    if (fwrite("KB2j", 1, 4, file) != 4)
        return -1;
    if (write_u32(file, 0) != 0)           /* file size, patched later */
        return -1;
    if (write_u32(file, frame_count) != 0) /* frames */
        return -1;
    if (write_u32(file, 0) != 0)           /* largest packet, patched */
        return -1;
    if (write_u32(file, frame_count) != 0) /* reserved */
        return -1;
    if (write_u32(file, width) != 0)
        return -1;
    if (write_u32(file, height) != 0)
        return -1;
    if (write_u32(file, fps_num) != 0)
        return -1;
    if (write_u32(file, fps_den) != 0)
        return -1;
    if (write_u32(file, flags) != 0)
        return -1;
    if (write_u32(file, track_count) != 0)
        return -1;
    if (write_u32(file, 0x87e70800UL) != 0)
        return -1;
    for (i = 0; i < track_count; i++)
        if (write_u32(file, tracks[i].max_decoded) != 0)
            return -1;
    for (i = 0; i < track_count; i++) {
        unsigned long track_flags =
            (tracks[i].use_dct ? 0x10UL : 0UL) |
            (tracks[i].channels == 2 ? 0x20UL : 0UL);
        unsigned long descriptor =
            (tracks[i].rate & 0xFFFFFFUL) | (track_flags << 24);

        if (write_u32(file, descriptor) != 0)
            return -1;
    }
    for (i = 0; i < track_count; i++)
        if (write_u32(file, tracks[i].id) != 0)
            return -1;
    if (index_offset != NULL)
        *index_offset = BINK2_HEADER_SIZE + 12UL * track_count;
    for (i = 0; i < frame_count + 1; i++) {
        if (write_u32(file, 0) != 0)
            return -1;
    }
    return 0;
}

int bink2_container_patch_index(FILE *file, unsigned long index_offset,
                                const unsigned long *offsets,
                                const unsigned char *keys,
                                unsigned long count)
{
    unsigned long i;

    if (file == NULL || offsets == NULL || count == 0)
        return -1;
    if (fseek(file, (long)index_offset, SEEK_SET) != 0)
        return -1;
    for (i = 0; i < count; i++) {
        unsigned long value = offsets[i];

        if (keys != NULL && keys[i])
            value |= 1;
        if (write_u32(file, value) != 0)
            return -1;
    }
    if (write_u32(file, offsets[count]) != 0)
        return -1;
    return 0;
}
