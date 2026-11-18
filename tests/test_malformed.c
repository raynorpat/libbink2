#include "bink2.h"
#include "test_common.h"

static void w32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static size_t build_video_ex(unsigned char *buf, const char *sig,
                             unsigned long flags, unsigned long width,
                             unsigned long height, unsigned tracks,
                             const unsigned char *payload, unsigned long size)
{
    unsigned long off = 44;
    unsigned long index_end;
    unsigned long data;
    unsigned t;

    memset(buf, 0, 4096);
    memcpy(buf, sig, 4);
    w32(buf + 8, 1);
    w32(buf + 12, size);
    w32(buf + 16, 1);
    w32(buf + 20, width);
    w32(buf + 24, height);
    w32(buf + 28, 30);
    w32(buf + 32, 1);
    w32(buf + 36, flags);
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

static size_t build_video(unsigned char *buf, unsigned long width,
                          unsigned long height, unsigned tracks,
                          const unsigned char *payload, unsigned long size)
{
    return build_video_ex(buf, "KB2g", 0x10, width, height, tracks, payload,
                          size);
}

static unsigned char *load_fixture(size_t *size)
{
    FILE *f = fopen(BINK2_FIXTURE_DIR "/synthetic_kb2j.bk2", "rb");
    unsigned char *buf;
    long end;

    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)end);
    if (buf == NULL || fread(buf, 1, (size_t)end, f) != (size_t)end) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)end;
    return buf;
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

    size = build_video_ex(buf, "KB2g", 0x100010, 64, 64, 0, payload,
                          sizeof(payload));
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);
    return 0;
}

static int test_slice_memory(void)
{
    unsigned char buf[4096];
    unsigned char payload[16] = { 0 };
    size_t size;
    bink2 b;

    /* 8 slices with an 8-byte header: too short, and the old code wrote
     * eight heights into a four-entry stack array. */
    size = build_video_ex(buf, "KB2h", 0x13, 64, 576, 0, payload, 8);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    CHECK(bink2_first(b) == BINK2_ERROR);
    bink2_close(b);

    /* 3 slices with an 8-byte header: the offset table is missing. */
    size = build_video_ex(buf, "KB2h", 0x11, 64, 576, 0, payload, 8);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    CHECK(bink2_first(b) == BINK2_ERROR);
    bink2_close(b);
    return 0;
}

static int test_truncated_video(void)
{
    unsigned char *buf;
    size_t size;
    unsigned long frame0, frame1;
    bink2 b;

    buf = load_fixture(&size);
    CHECK(buf != NULL);
    frame0 = le32(buf + 48) & ~1UL;
    frame1 = le32(buf + 52) & ~1UL;
    CHECK(frame1 > frame0);
    w32(buf + 52, frame0 + (frame1 - frame0) / 2);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    CHECK(bink2_first(b) == BINK2_ERROR);
    bink2_close(b);
    free(buf);
    return 0;
}

static int test_slice_offset(void)
{
    unsigned char *buf;
    size_t size;
    unsigned long frame0;
    bink2 b;

    buf = load_fixture(&size);
    CHECK(buf != NULL);
    frame0 = le32(buf + 48) & ~1UL;
    w32(buf + frame0 + 4, 0x40000000UL);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    CHECK(bink2_first(b) == BINK2_ERROR);
    bink2_close(b);
    free(buf);

    buf = load_fixture(&size);
    CHECK(buf != NULL);
    frame0 = le32(buf + 48) & ~1UL;
    w32(buf + frame0 + 4, 0xFFFFFFFFUL);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    CHECK(bink2_first(b) == BINK2_ERROR);
    bink2_close(b);
    free(buf);
    return 0;
}

static int test_seek_clamp(void)
{
    unsigned char *src;
    unsigned char out[8192];
    size_t size;
    unsigned long frame0, frame1, frame_size;
    bink2 b;
    unsigned long current = 1;

    src = load_fixture(&size);
    CHECK(src != NULL);
    frame0 = le32(src + 48) & ~1UL;
    frame1 = le32(src + 52) & ~1UL;
    frame_size = frame1 - frame0;
    CHECK(frame_size > 0 && 60 + frame_size < sizeof(out));

    memcpy(out, src, 48);
    w32(out + 4, 60 + frame_size - 8);
    w32(out + 8, 2);
    w32(out + 8, 3);
    w32(out + 4, 64 + frame_size - 8);
    w32(out + 48, 64 | 1);
    w32(out + 52, 64 + frame_size);
    w32(out + 56, 0x100000UL | 1); /* keyframe past EOF */
    w32(out + 60, 0x100010UL);
    memcpy(out + 64, src + frame0, frame_size);
    free(src);

    b = bink2_open_memory(out, 64 + frame_size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    CHECK(bink2_seek_keyframe(b, 2) == BINK2_MORE);
    CHECK(bink2_info_all(b, &current, NULL, NULL) == 0);
    CHECK(current == 0);
    bink2_close(b);
    return 0;
}

static int test_audio_overrun(void)
{
    unsigned char buf[4096];
    size_t size;
    bink2 b;

    size = build_video(buf, 64, 64, 1, NULL, 0);
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

static int test_audio_truncated_prefix(void)
{
    unsigned char buf[4096];
    unsigned char payload[3] = { 0x01, 0x02, 0x03 };
    size_t size;
    bink2 b;

    size = build_video(buf, 64, 64, 1, payload, sizeof(payload));
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_enable_audio(b, 0, 1) == 0);
    CHECK(bink2_first(b) == BINK2_ERROR);
    CHECK(bink2_get_audio_size(b, 0) == 0);
    bink2_close(b);
    return 0;
}

static int test_audio_reported_size_overrun(void)
{
    unsigned char buf[4096];
    unsigned char payload[4];
    size_t size;
    bink2 b;

    w32(payload, 0xFFFFFFFFUL);
    size = build_video(buf, 64, 64, 1, payload, sizeof(payload));
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_enable_audio(b, 0, 1) == 0);
    CHECK(bink2_first(b) == BINK2_ERROR);
    CHECK(bink2_get_audio_size(b, 0) == 0);
    bink2_close(b);
    return 0;
}

static int test_audio_zero_rate_descriptor(void)
{
    unsigned char buf[4096];
    unsigned char payload[8] = { 0 };
    size_t size;

    size = build_video(buf, 64, 64, 1, payload, sizeof(payload));
    buf[48] = 0;
    buf[49] = 0;
    buf[50] = 0;
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);
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
    CHECK(test_slice_memory() == 0);
    CHECK(test_truncated_video() == 0);
    CHECK(test_slice_offset() == 0);
    CHECK(test_seek_clamp() == 0);
    CHECK(test_audio_overrun() == 0);
    CHECK(test_audio_truncated_prefix() == 0);
    CHECK(test_audio_reported_size_overrun() == 0);
    CHECK(test_audio_zero_rate_descriptor() == 0);
    CHECK(test_fuzz() == 0);
    return 0;
}
