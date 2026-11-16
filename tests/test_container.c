#include "bink2.h"
#include "test_common.h"

static void w32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void w24(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
}

static size_t build(unsigned char *buf, const char *sig, unsigned tracks,
                    unsigned frames)
{
    unsigned long index_end;
    unsigned long off;
    unsigned t;
    unsigned n;

    memset(buf, 0, 4096);
    memcpy(buf, sig, 4);
    w32(buf + 8, frames);
    w32(buf + 12, 128);
    w32(buf + 16, frames);
    w32(buf + 20, 1280);
    w32(buf + 24, 736);
    w32(buf + 28, 30000);
    w32(buf + 32, 1001);
    w32(buf + 36, 0x10);
    w32(buf + 40, tracks);

    off = 44;
    if (sig[3] >= 'i') {
        w32(buf + off, 0x87e70800UL);
        off += 4;
    }
    for (t = 0; t < tracks; t++)
        w32(buf + off + 4 * t, 69120);
    off += 4 * tracks;
    for (t = 0; t < tracks; t++) {
        w24(buf + off, t == 0 ? 48000 : 44100);
        buf[off + 3] = (unsigned char)(t == 0 ? 0x70 : 0x50);
        off += 4;
    }
    for (t = 0; t < tracks; t++) {
        w32(buf + off, t + 1);
        off += 4;
    }

    index_end = off + 4UL * (frames + 1);
    for (n = 0; n <= frames; n++) {
        unsigned long value = index_end + 16UL * n;
        if (n < frames && (n % 2) == 0)
            value |= 1;
        w32(buf + off + 4UL * n, value);
    }
    w32(buf + 4, index_end + 16UL * frames + 16UL - 8UL);
    return (size_t)(index_end + 16UL * frames + 16UL);
}

static int test_metadata(void)
{
    unsigned char buf[4096];
    unsigned char mask = 0;
    unsigned char channels[7];
    unsigned char depths[7];
    unsigned long rates[7];
    unsigned long frames = 0;
    unsigned long w = 0, h = 0, flags = 0;
    double usf = 0.0;
    size_t size;
    bink2 b;

    size = build(buf, "KB2g", 0, 2);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    CHECK(bink2_info_all(b, NULL, &frames, &usf) == 0);
    CHECK(frames == 2);
    CHECK(usf > 33366.0 && usf < 33367.0);
    CHECK(bink2_info_video(b, &w, &h, &flags) == 0);
    CHECK(w == 1280 && h == 736 && flags == 0x10);
    CHECK(bink2_info_audio(b, &mask, channels, depths, rates) == 0);
    CHECK(mask == 0);
    bink2_close(b);

    size = build(buf, "KB2f", 6, 2);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    CHECK(bink2_info_audio(b, &mask, channels, depths, rates) == 0);
    CHECK(mask == 0x3F);
    CHECK(rates[0] == 48000 && rates[1] == 44100);
    CHECK(channels[0] == 2 && channels[1] == 1);
    CHECK(depths[0] == 16 && depths[5] == 16);
    bink2_close(b);

    size = build(buf, "KB2j", 1, 2);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    CHECK(bink2_info_audio(b, &mask, channels, depths, rates) == 0);
    CHECK(mask == 0x01 && rates[0] == 48000 && channels[0] == 2);
    bink2_close(b);

    size = build(buf, "KB2i", 1, 2);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    CHECK(bink2_info_audio(b, &mask, channels, depths, rates) == 0);
    CHECK(mask == 0x01 && rates[0] == 48000);
    bink2_close(b);

    return 0;
}

static int test_rejects(void)
{
    unsigned char buf[4096];
    size_t size;
    bink2 b;

    size = build(buf, "BIKf", 0, 1);
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);
    size = build(buf, "BIKi", 0, 1);
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);
    size = build(buf, "KB2a", 0, 1);
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);
    size = build(buf, "KB2o", 0, 1);
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);
    size = build(buf, "XXXX", 0, 1);
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);

    size = build(buf, "KB2g", 0, 1);
    CHECK(bink2_open_memory(buf, 43) == NULL);
    CHECK(bink2_open_memory(NULL, (unsigned long)size) == NULL);
    CHECK(bink2_open_memory(buf, 0) == NULL);

    size = build(buf, "KB2g", 0, 1);
    w32(buf + 32, 0);
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);

    size = build(buf, "KB2g", 0, 1);
    w32(buf + 20, 0);
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);

    size = build(buf, "KB2g", 8, 1);
    CHECK(bink2_open_memory(buf, (unsigned long)size) == NULL);

    return 0;
}

static int test_disk_open(void)
{
    unsigned char buf[4096];
    unsigned long frames = 0;
    size_t size;
    FILE *f;
    bink2 b;

    size = build(buf, "KB2g", 0, 2);
    f = fopen("test_container.bk2", "wb");
    CHECK(f != NULL);
    CHECK(fwrite(buf, 1, size, f) == size);
    fclose(f);

    b = bink2_open_file("test_container.bk2", BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_info_all(b, NULL, &frames, NULL) == 0);
    CHECK(frames == 2);
    bink2_close(b);

    f = fopen("test_container.bk2", "rb");
    CHECK(f != NULL);
    b = bink2_open_filepointer(f, BINK2_MODE_DISK);
    CHECK(b != NULL);
    bink2_close(b);
    fclose(f);

    remove("test_container.bk2");
    return 0;
}

int main(void)
{
    CHECK(test_metadata() == 0);
    CHECK(test_rejects() == 0);
    CHECK(test_disk_open() == 0);
    return 0;
}
