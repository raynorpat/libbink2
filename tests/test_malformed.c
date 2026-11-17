#include "bink2.h"
#include "test_common.h"

static void w32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static size_t build_video(unsigned char *buf, unsigned long width,
                          unsigned long height, unsigned tracks,
                          const unsigned char *payload, unsigned long size)
{
    unsigned long off = 44;
    unsigned long index_end;
    unsigned long data;
    unsigned t;

    memset(buf, 0, 4096);
    memcpy(buf, "KB2g", 4);
    w32(buf + 8, 1);
    w32(buf + 12, size);
    w32(buf + 16, 1);
    w32(buf + 20, width);
    w32(buf + 24, height);
    w32(buf + 28, 30);
    w32(buf + 32, 1);
    w32(buf + 36, 0x10);
    w32(buf + 40, tracks);
    for (t = 0; t < tracks; t++)
        w32(buf + off + 4 * t, 69120);
    off += 4 * tracks;
    for (t = 0; t < tracks; t++) {
        buf[off + 0] = 0x44;
        buf[off + 1] = 0xAC;
        buf[off + 2] = 0x00;
        buf[off + 3] = 0x10;
        off += 4;
    }
    for (t = 0; t < tracks; t++) {
        w32(buf + off, t + 1);
        off += 4;
    }
    index_end = off + 8;
    data = index_end;
    w32(buf + 4, data + size - 8);
    w32(buf + off, data | 1);
    w32(buf + off + 4, data + size);
    if (size != 0)
        memcpy(buf + data, payload, size);
    return (size_t)(data + size);
}

static int test_header_rejects(void)
{
    unsigned char buf[4096];
    unsigned char payload[8] = { 0 };
    size_t size;

    size = build_video(buf, 7681, 64, 0, payload, sizeof(payload));
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);
    size = build_video(buf, 64, 0, 0, payload, sizeof(payload));
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);
    size = build_video(buf, 64, 64, 0, payload, sizeof(payload));
    w32(buf + 8, 0);
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);
    CHECK(bink2_open_memory(buf, 20) == NULL);
    return 0;
}

static int test_truncated_and_offsets(void)
{
    unsigned char buf[4096];
    unsigned char payload[8] = { 0 };
    size_t size;
    bink2 b;

    size = build_video(buf, 64, 64, 0, payload, sizeof(payload));
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    bink2_first(b); /* random payload must not crash */
    bink2_close(b);

    size = build_video(buf, 64, 64, 0, payload, sizeof(payload));
    /* Point the terminator past EOF. */
    w32(buf + 48, 0x10000);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    CHECK(bink2_next(b) == BINK2_ERROR || bink2_next(b) == BINK2_LAST);
    bink2_close(b);
    return 0;
}

static int test_audio_overrun(void)
{
    unsigned char buf[4096];
    size_t size;
    bink2 b;

    size = build_video(buf, 64, 64, 1, NULL, 0);
    /* The packet is empty; claim a huge audio chunk. */
    {
        unsigned long start = 64;
        w32(buf + start, 1000);
        size = (size_t)(start + 4);
        w32(buf + 4, (unsigned long)size - 8);
        w32(buf + 56, start | 1);
        w32(buf + 60, start + 4);
    }
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    CHECK(bink2_first(b) == BINK2_ERROR);
    bink2_close(b);
    return 0;
}

static int test_fuzz(void)
{
    unsigned char buf[1024];
    unsigned seed = 12345;
    int i;

    for (i = 0; i < 2000; i++) {
        size_t size;
        bink2 b;
        int j;

        for (j = 0; j < (int)sizeof(buf); j++) {
            seed = seed * 1103515245u + 12345u;
            buf[j] = (unsigned char)(seed >> 16);
        }
        memcpy(buf, "KB2g", 4);
        size = 44 + (size_t)(seed % 900);
        if (size > sizeof(buf))
            size = sizeof(buf);
        b = bink2_open_memory(buf, (unsigned long)size);
        if (b != NULL) {
            bink2_enable_video(b, 1);
            bink2_enable_audio(b, 0, 1);
            bink2_first(b);
            bink2_next(b);
            bink2_close(b);
        }
    }
    return 0;
}

int main(void)
{
    CHECK(test_header_rejects() == 0);
    CHECK(test_truncated_and_offsets() == 0);
    CHECK(test_audio_overrun() == 0);
    CHECK(test_fuzz() == 0);
    return 0;
}
