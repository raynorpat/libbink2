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

static uint64_t picture_hash(bink2 b)
{
    unsigned long w = 0, h = 0, flags = 0;
    uint64_t hash = 0;
    int p;

    bink2_info_video(b, &w, &h, &flags);
    for (p = 0; p < 3; p++) {
        int pw = p == 0 ? (int)w : (int)((w + 1) / 2);
        int ph = p == 0 ? (int)h : (int)((h + 1) / 2);

        hash = hash * 31 + fnv1a_rows(bink2_get_plane(b, p), pw, ph,
                                      bink2_get_stride(b, p));
    }
    return hash;
}

/* A failed inter frame leaves the previous picture visible, and decoding
 * resumes correctly from a keyframe and from a rewind afterwards. */
static int test_error_then_recover(int threads)
{
    unsigned char *buf;
    size_t size;
    unsigned long frame1, key2 = 0, i;
    uint64_t good[32];
    bink2 b;

    buf = load_fixture(&size);
    CHECK(buf != NULL);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    bink2_internal_set_threads(b, threads);
    CHECK(bink2_first(b) != BINK2_ERROR);
    good[0] = picture_hash(b);
    for (i = 1; i < 32; i++) {
        CHECK(bink2_next(b) != BINK2_ERROR);
        good[i] = picture_hash(b);
        if (key2 == 0 && (le32(buf + 48 + 4 * i) & 1))
            key2 = i;
    }
    bink2_close(b);
    CHECK(key2 > 1);

    frame1 = le32(buf + 52) & ~1UL;
    w32(buf + frame1 + 4, 0x40000000UL);
    b = bink2_open_memory(buf, (unsigned long)size);
    CHECK(b != NULL);
    bink2_enable_video(b, 1);
    bink2_internal_set_threads(b, threads);
    CHECK(bink2_first(b) != BINK2_ERROR);
    CHECK(picture_hash(b) == good[0]);
    CHECK(bink2_next(b) == BINK2_ERROR);
    CHECK(picture_hash(b) == good[0]);
    CHECK(bink2_seek_keyframe(b, key2) != BINK2_ERROR);
    CHECK(picture_hash(b) == good[key2]);
    CHECK(bink2_next(b) != BINK2_ERROR);
    CHECK(picture_hash(b) == good[key2 + 1]);
    CHECK(bink2_first(b) != BINK2_ERROR);
    CHECK(picture_hash(b) == good[0]);
    bink2_close(b);
    free(buf);
    return 0;
}

typedef struct {
    int status[2];
    uint64_t hash[2];
} DecodeResult;

/* Decodes frames 0 and 1 of buf with the given thread count. */
static int decode_two(const unsigned char *buf, size_t size, int threads,
                      DecodeResult *r)
{
    bink2 b = bink2_open_memory(buf, (unsigned long)size);

    if (b == NULL)
        return -1;
    bink2_enable_video(b, 1);
    bink2_internal_set_threads(b, threads);
    r->status[0] = bink2_first(b);
    r->hash[0] = picture_hash(b);
    r->status[1] = r->status[0] == BINK2_ERROR ? BINK2_ERROR : bink2_next(b);
    r->hash[1] = picture_hash(b);
    bink2_close(b);
    return 0;
}

/* Serial and threaded decoding agree on the result and on the picture left
 * behind. A failed first frame has no previous picture to restore, so its
 * contents are only compared on success. */
static int same_in_both_modes(const unsigned char *buf, size_t size)
{
    DecodeResult serial, threaded;

    CHECK(decode_two(buf, size, 1, &serial) == 0);
    CHECK(decode_two(buf, size, 8, &threaded) == 0);
    CHECK(serial.status[0] == threaded.status[0]);
    if (serial.status[0] != BINK2_ERROR) {
        CHECK(serial.hash[0] == threaded.hash[0]);
        CHECK(serial.status[1] == threaded.status[1]);
        CHECK(serial.hash[1] == threaded.hash[1]);
    }
    return 0;
}

/* Slice 2 starts inside slice 1, so slice 1 runs past it. */
static int test_slice_overlap(void)
{
    unsigned char *buf;
    size_t size;
    unsigned long frame0;
    DecodeResult r;

    buf = load_fixture(&size);
    CHECK(buf != NULL);
    frame0 = le32(buf + 48) & ~1UL;
    w32(buf + frame0 + 8, le32(buf + frame0 + 4) + 4);
    CHECK(decode_two(buf, size, 1, &r) == 0);
    CHECK(r.status[0] == BINK2_ERROR);
    CHECK(decode_two(buf, size, 8, &r) == 0);
    CHECK(r.status[0] == BINK2_ERROR);
    free(buf);
    return 0;
}

/* Slice 3's offset is below slice 2's. */
static int test_slice_offset_backwards(void)
{
    unsigned char *buf;
    size_t size;
    unsigned long frame0;
    DecodeResult r;

    buf = load_fixture(&size);
    CHECK(buf != NULL);
    frame0 = le32(buf + 48) & ~1UL;
    w32(buf + frame0 + 12, le32(buf + frame0 + 8) - 4);
    CHECK(decode_two(buf, size, 1, &r) == 0);
    CHECK(r.status[0] == BINK2_ERROR);
    CHECK(decode_two(buf, size, 8, &r) == 0);
    CHECK(r.status[0] == BINK2_ERROR);
    free(buf);
    return 0;
}

/* Single-byte corruptions in the slice data of frames 0 and 1. */
static int test_threaded_fuzz(void)
{
    unsigned char *orig, *buf;
    size_t size;
    unsigned long frame[3];
    unsigned long seed = 1;
    int i, f;

    orig = load_fixture(&size);
    CHECK(orig != NULL);
    buf = (unsigned char *)malloc(size);
    CHECK(buf != NULL);
    for (f = 0; f < 3; f++)
        frame[f] = le32(orig + 48 + 4 * f) & ~1UL;
    for (f = 0; f < 2; f++) {
        unsigned long start = frame[f] + 16;
        unsigned long len = frame[f + 1] - start;

        for (i = 0; i < 150; i++) {
            unsigned long pos;

            seed = seed * 1103515245UL + 12345UL;
            pos = start + (seed >> 8) % len;
            memcpy(buf, orig, size);
            buf[pos] ^= (unsigned char)(1u << ((seed >> 4) & 7));
            if (same_in_both_modes(buf, size) != 0) {
                fprintf(stderr, "frame %d byte %lu\n", f, pos);
                free(buf);
                free(orig);
                return 1;
            }
        }
    }
    free(buf);
    free(orig);
    return 0;
}

/* Hashes of frames 0..n-1 decoded with bink2_next, BINK2_ERROR frames
 * included (the picture left visible after the failure). */
static int decode_run(const unsigned char *buf, size_t size, int threads,
                      int n, int status[], uint64_t hash[])
{
    bink2 b = bink2_open_memory(buf, (unsigned long)size);
    int i;

    if (b == NULL)
        return -1;
    bink2_enable_video(b, 1);
    bink2_internal_set_threads(b, threads);
    for (i = 0; i < n; i++) {
        status[i] = i == 0 ? bink2_first(b) : bink2_next(b);
        hash[i] = picture_hash(b);
    }
    bink2_close(b);
    return 0;
}

/* Damage inside a slice of inter frame 1 (not its offsets) fails that
 * frame; frame 0 stays visible, decoding carries on with plain next calls,
 * and from the next keyframe on the pictures are the undamaged ones. */
static int test_mid_slice_recover(void)
{
    unsigned char *orig, *buf;
    size_t size;
    unsigned long frame1, frame2, key2 = 0, pos;
    int good_status[32], status[2][32];
    uint64_t good[32], hash[2][32];
    int i, t, found = 0;

    orig = load_fixture(&size);
    CHECK(orig != NULL);
    buf = (unsigned char *)malloc(size);
    CHECK(buf != NULL);
    for (i = 2; i < 32 && key2 == 0; i++)
        if (le32(orig + 48 + 4 * i) & 1)
            key2 = (unsigned long)i;
    CHECK(key2 > 2 && key2 + 2 < 32);
    CHECK(decode_run(orig, size, 1, 32, good_status, good) == 0);
    frame1 = le32(orig + 52) & ~1UL;
    frame2 = le32(orig + 56) & ~1UL;
    /* The first byte past frame 1's offset table whose damage fails it. */
    for (pos = frame1 + 16; pos < frame2 && !found; pos++) {
        memcpy(buf, orig, size);
        buf[pos] ^= 0xFF;
        CHECK(decode_run(buf, size, 1, 2, status[0], hash[0]) == 0);
        found = status[0][1] == BINK2_ERROR;
    }
    CHECK(found);
    for (t = 0; t < 2; t++) {
        CHECK(decode_run(buf, size, t == 0 ? 1 : 8, (int)key2 + 2,
                         status[t], hash[t]) == 0);
        CHECK(status[t][1] == BINK2_ERROR);
        CHECK(hash[t][1] == good[0]);
        CHECK(status[t][key2] != BINK2_ERROR);
        CHECK(hash[t][key2] == good[key2]);
        CHECK(hash[t][key2 + 1] == good[key2 + 1]);
    }
    for (i = 0; i < (int)key2 + 2; i++) {
        CHECK(status[0][i] == status[1][i]);
        CHECK(hash[0][i] == hash[1][i]);
    }
    free(buf);
    free(orig);
    return 0;
}

int main(void)
{
    CHECK(test_header_rejects() == 0);
    CHECK(test_slice_memory() == 0);
    CHECK(test_truncated_video() == 0);
    CHECK(test_slice_offset() == 0);
    CHECK(test_error_then_recover(1) == 0);
    CHECK(test_error_then_recover(8) == 0);
    CHECK(test_slice_overlap() == 0);
    CHECK(test_slice_offset_backwards() == 0);
    CHECK(test_threaded_fuzz() == 0);
    CHECK(test_mid_slice_recover() == 0);
    CHECK(test_seek_clamp() == 0);
    CHECK(test_audio_overrun() == 0);
    CHECK(test_audio_truncated_prefix() == 0);
    CHECK(test_audio_reported_size_overrun() == 0);
    CHECK(test_audio_zero_rate_descriptor() == 0);
    CHECK(test_fuzz() == 0);
    return 0;
}
