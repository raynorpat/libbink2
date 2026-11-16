#include "bink2.h"
#include "bink2_internal.h"
#include "test_common.h"

static void w32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static size_t build(unsigned char *buf, const unsigned char *p0,
                    const unsigned char *p1, const unsigned char *p2,
                    unsigned long s0, unsigned long s1, unsigned long s2)
{
    unsigned long off = 44;
    unsigned long index_end;
    unsigned long data;
    unsigned long offsets[3];
    unsigned long i;
    const unsigned char *packets[3];
    unsigned long sizes[3];

    packets[0] = p0; packets[1] = p1; packets[2] = p2;
    sizes[0] = s0; sizes[1] = s1; sizes[2] = s2;

    memset(buf, 0, 1024);
    memcpy(buf, "KB2g", 4);
    w32(buf + 8, 3);
    w32(buf + 12, 64);
    w32(buf + 16, 3);
    w32(buf + 20, 128);
    w32(buf + 24, 96);
    w32(buf + 28, 30);
    w32(buf + 32, 1);
    w32(buf + 36, 0x10);
    w32(buf + 40, 2);

    for (i = 0; i < 2; i++)
        w32(buf + off + 4 * i, 69120);
    off += 8;
    for (i = 0; i < 2; i++) {
        buf[off + 0] = 0x44;
        buf[off + 1] = 0xAC;
        buf[off + 2] = 0x00;
        buf[off + 3] = 0x50;
        off += 4;
    }
    for (i = 0; i < 2; i++) {
        w32(buf + off, i + 1);
        off += 4;
    }

    index_end = off + 4UL * 4;
    data = index_end;
    for (i = 0; i < 3; i++) {
        offsets[i] = data;
        data += sizes[i];
    }
    w32(buf + 4, data - 8);
    for (i = 0; i < 3; i++)
        w32(buf + off + 4 * i, offsets[i] | (i == 0 ? 1UL : 0UL));
    w32(buf + off + 12, data);

    memcpy(buf + offsets[0], p0, s0);
    memcpy(buf + offsets[1], p1, s1);
    memcpy(buf + offsets[2], p2, s2);
    return (size_t)data;
}

static void make_packets(unsigned char *p0, unsigned char *p1,
                         unsigned char *p2)
{
    memset(p0, 0, 20);
    w32(p0, 4);
    p0[4] = 1; p0[5] = 2; p0[6] = 3; p0[7] = 4;
    w32(p0 + 8, 0);
    p0[12] = 9; p0[13] = 9; p0[14] = 9; p0[15] = 9;
    p0[16] = 8; p0[17] = 8; p0[18] = 8; p0[19] = 8;

    memset(p1, 0, 16);
    w32(p1, 0);
    w32(p1 + 4, 2);
    p1[8] = 5; p1[9] = 6;
    p1[10] = 7; p1[11] = 7; p1[12] = 7; p1[13] = 7;
    p1[14] = 0; p1[15] = 0;

    memset(p2, 0, 16);
    w32(p2, 0);
    w32(p2 + 4, 0);
    memset(p2 + 8, 3, 8);
}

static int test_packets(void)
{
    unsigned char buf[1024];
    unsigned char p0[20], p1[16], p2[16];
    size_t size;
    bink2 b;

    make_packets(p0, p1, p2);
    size = build(buf, p0, p1, p2, 20, 16, 16);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);

    CHECK(bink2_internal_load_frame(b, 0) == BINK2_OK);
    CHECK(b->audio[0].payload_size == 4);
    CHECK(b->audio[0].payload[0] == 1);
    CHECK(b->audio[1].payload_size == 0);
    CHECK(b->video_size == 8 && b->video_data[0] == 9);
    CHECK(b->video_data[4] == 8);

    CHECK(bink2_internal_load_frame(b, 1) == BINK2_OK);
    CHECK(b->audio[0].payload_size == 0);
    CHECK(b->audio[1].payload_size == 2);
    CHECK(b->audio[1].payload[0] == 5);
    CHECK(b->video_size == 6 && b->video_data[0] == 7);

    CHECK(bink2_internal_load_frame(b, 2) == BINK2_OK);
    CHECK(b->video_size == 8 && b->video_data[0] == 3);
    CHECK(bink2_internal_load_frame(b, 3) == BINK2_LAST);
    bink2_close(b);
    return 0;
}

static int test_truncation(void)
{
    unsigned char buf[1024];
    unsigned char p0[20], p1[16], p2[16];
    size_t size;
    bink2 b;

    make_packets(p0, p1, p2);
    size = build(buf, p0, p1, p2, 20, 16, 16);

    b = bink2_open_memory(buf, 115);
    CHECK(b != NULL);
    CHECK(bink2_internal_load_frame(b, 0) == BINK2_OK);
    CHECK(bink2_internal_load_frame(b, 1) == BINK2_ERROR);
    CHECK(bink2_internal_load_frame(b, 2) == BINK2_LAST);
    bink2_close(b);

    b = bink2_open_memory(buf, 95);
    CHECK(b != NULL);
    CHECK(bink2_internal_load_frame(b, 0) == BINK2_ERROR);
    bink2_close(b);
    return 0;
}

static int test_bad_audio_and_index(void)
{
    unsigned char buf[1024];
    unsigned char p0[20], p1[16], p2[16];
    size_t size;
    bink2 b;

    make_packets(p0, p1, p2);
    size = build(buf, p0, p1, p2, 20, 16, 16);
    w32(buf + 84, 1000);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    CHECK(bink2_internal_load_frame(b, 0) == BINK2_ERROR);
    bink2_close(b);

    make_packets(p0, p1, p2);
    size = build(buf, p0, p1, p2, 20, 16, 16);
    w32(buf + 76, 0x1000);
    w32(buf + 80, 0x1010);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    CHECK(bink2_internal_load_frame(b, 2) == BINK2_LAST);
    bink2_close(b);
    return 0;
}

int main(void)
{
    CHECK(test_packets() == 0);
    CHECK(test_truncation() == 0);
    CHECK(test_bad_audio_and_index() == 0);
    return 0;
}
