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
                                 unsigned long *index_offset)
{
    unsigned long i;

    if (file == NULL || frame_count == 0)
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
    if (write_u32(file, 0) != 0)           /* audio tracks */
        return -1;
    if (write_u32(file, 0x87e70800UL) != 0)
        return -1;
    for (i = 0; i < frame_count + 1; i++) {
        if (write_u32(file, 0) != 0)
            return -1;
    }
    if (index_offset != NULL)
        *index_offset = BINK2_HEADER_SIZE;
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
