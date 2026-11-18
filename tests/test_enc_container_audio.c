#include "bink2_enc_container.h"
#include "test_common.h"

#include <stdio.h>

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int read_header(FILE *file, unsigned char *out, size_t size)
{
    if (fflush(file) != 0 || fseek(file, 0, SEEK_SET) != 0)
        return -1;
    return fread(out, 1, size, file) == size ? 0 : -1;
}

static int test_one_track(void)
{
    Bink2EncAudioTrack track;
    unsigned long index_offset = 0;
    unsigned char header[68];
    FILE *file = tmpfile();

    CHECK(file != NULL);
    memset(&track, 0, sizeof(track));
    track.rate = 48000;
    track.channels = 2;
    track.max_decoded = 12345;
    track.id = 1;
    track.use_dct = 1;
    CHECK(bink2_container_write_header(file, 64, 48, 24, 1, 1, 0, &track, 1,
                                       &index_offset) == 0);
    CHECK(index_offset == 60);
    CHECK(read_header(file, header, sizeof(header)) == 0);
    CHECK(memcmp(header, "KB2j", 4) == 0);
    CHECK(le32(header + 40) == 1);
    CHECK(le32(header + 44) == 0x87e70800UL);
    CHECK(le32(header + 48) == 12345);
    CHECK(header[52] == 0x80);
    CHECK(header[53] == 0xBB);
    CHECK(header[54] == 0x00);
    CHECK(header[55] == 0x30);
    CHECK(le32(header + 56) == 1);
    CHECK(le32(header + 60) == 0);
    fclose(file);
    return 0;
}

static int test_no_track(void)
{
    unsigned long index_offset = 0;
    unsigned char header[52];
    FILE *file = tmpfile();

    CHECK(file != NULL);
    CHECK(bink2_container_write_header(file, 64, 48, 30, 1, 1, 0, NULL, 0,
                                       &index_offset) == 0);
    CHECK(index_offset == 48);
    CHECK(read_header(file, header, sizeof(header)) == 0);
    CHECK(le32(header + 40) == 0);
    CHECK(le32(header + 44) == 0x87e70800UL);
    CHECK(le32(header + 48) == 0);
    fclose(file);
    return 0;
}

static int test_mono_flags(void)
{
    Bink2EncAudioTrack track;
    unsigned long index_offset = 0;
    unsigned char header[60];
    FILE *file = tmpfile();

    CHECK(file != NULL);
    memset(&track, 0, sizeof(track));
    track.rate = 44100;
    track.channels = 1;
    track.max_decoded = 7;
    track.id = 3;
    track.use_dct = 1;
    CHECK(bink2_container_write_header(file, 32, 32, 30, 1, 1, 0, &track, 1,
                                       &index_offset) == 0);
    CHECK(read_header(file, header, sizeof(header)) == 0);
    CHECK(header[52] == 0x44);
    CHECK(header[53] == 0xAC);
    CHECK(header[54] == 0x00);
    CHECK(header[55] == 0x10);
    CHECK(le32(header + 56) == 3);
    fclose(file);
    return 0;
}

int main(void)
{
    CHECK(test_one_track() == 0);
    CHECK(test_no_track() == 0);
    CHECK(test_mono_flags() == 0);
    return 0;
}
