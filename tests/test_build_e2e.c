#include "bink2.h"

#include "test_common.h"

#include <stdio.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define FRAMES 3
#define WIDTH 64
#define HEIGHT 48

static int write_ppm(const char *path, int frame)
{
    FILE *file = fopen(path, "wb");
    int x, y;

    if (file == NULL)
        return -1;
    if (fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT) < 0) {
        fclose(file);
        return -1;
    }
    for (y = 0; y < HEIGHT; y++)
        for (x = 0; x < WIDTH; x++) {
            unsigned char px[3] = {
                (unsigned char)((x + frame * 3) & 0xFF),
                (unsigned char)((y + frame * 2) & 0xFF),
                (unsigned char)((x + y + frame) & 0xFF)
            };

            if (fwrite(px, 1, 3, file) != 3) {
                fclose(file);
                return -1;
            }
        }
    return fclose(file) == 0 ? 0 : -1;
}

static int write_wav(const char *path)
{
    FILE *file = fopen(path, "wb");
    unsigned char header[44];
    unsigned long data_size = 9600UL * 2UL * 2UL;
    unsigned long riff = 36UL + data_size;
    int i;

    if (file == NULL)
        return -1;
    memcpy(header, "RIFF", 4);
    header[4] = (unsigned char)(riff & 0xFF);
    header[5] = (unsigned char)((riff >> 8) & 0xFF);
    header[6] = (unsigned char)((riff >> 16) & 0xFF);
    header[7] = (unsigned char)((riff >> 24) & 0xFF);
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16;
    header[17] = 0;
    header[18] = 0;
    header[19] = 0;
    header[20] = 1;
    header[21] = 0;
    header[22] = 2;
    header[23] = 0;
    header[24] = 0x80;
    header[25] = 0xBB;
    header[26] = 0;
    header[27] = 0;
    header[28] = 0x00;
    header[29] = 0xEE;
    header[30] = 0x02;
    header[31] = 0;
    header[32] = 4;
    header[33] = 0;
    header[34] = 16;
    header[35] = 0;
    memcpy(header + 36, "data", 4);
    header[40] = (unsigned char)(data_size & 0xFF);
    header[41] = (unsigned char)((data_size >> 8) & 0xFF);
    header[42] = (unsigned char)((data_size >> 16) & 0xFF);
    header[43] = (unsigned char)((data_size >> 24) & 0xFF);
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return -1;
    }
    for (i = 0; i < 9600; i++) {
        short sample = (short)((i % 24) * 600 - 7000);
        short pair[2] = { sample, sample };

        if (fwrite(pair, 1, sizeof(pair), file) != sizeof(pair)) {
            fclose(file);
            return -1;
        }
    }
    return fclose(file) == 0 ? 0 : -1;
}

static int file_contains(const char *path, const char *needle)
{
    char text[1024];
    size_t got;
    FILE *file = fopen(path, "rb");

    if (file == NULL)
        return 0;
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    return strstr(text, needle) != NULL;
}

static void make_dir(const char *path)
{
#if defined(_WIN32)
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

static void remove_dir(const char *path)
{
#if defined(_WIN32)
    _rmdir(path);
#else
    rmdir(path);
#endif
}

/* A frame whose size differs from the first must be named in the error,
 * and a bad first image must say why it was rejected. */
static int test_input_errors(void)
{
    char command[1024];
    FILE *file;

    make_dir("build_e2e_bad");
    CHECK(write_ppm("build_e2e_bad/frame0.ppm", 0) == 0);
    file = fopen("build_e2e_bad/frame1.ppm", "wb");
    CHECK(file != NULL);
    CHECK(fprintf(file, "P6\n8 8\n255\n") > 0);
    {
        static const unsigned char black[8 * 8 * 3] = { 0 };

        CHECK(fwrite(black, 1, sizeof(black), file) == sizeof(black));
    }
    CHECK(fclose(file) == 0);
    snprintf(command, sizeof(command),
             "\"%s\" build_e2e_bad -o build_e2e_bad.bk2 2> build_e2e_err.txt",
             BINK2_BUILD_EXE);
    CHECK(system(command) != 0);
    CHECK(file_contains("build_e2e_err.txt", "frame 1"));
    CHECK(file_contains("build_e2e_err.txt", "frame1.ppm"));
    CHECK(file_contains("build_e2e_err.txt", "8x8"));

    file = fopen("build_e2e_bad.ppm", "wb");
    CHECK(file != NULL);
    CHECK(fprintf(file, "P6\n4 4\n65535\n") > 0);
    CHECK(fclose(file) == 0);
    snprintf(command, sizeof(command),
             "\"%s\" build_e2e_bad.ppm -o build_e2e_bad.bk2 "
             "2> build_e2e_err.txt", BINK2_BUILD_EXE);
    CHECK(system(command) != 0);
    CHECK(file_contains("build_e2e_err.txt", "maxval"));

    remove("build_e2e_bad/frame0.ppm");
    remove("build_e2e_bad/frame1.ppm");
    remove_dir("build_e2e_bad");
    remove("build_e2e_bad.ppm");
    remove("build_e2e_bad.bk2");
    remove("build_e2e_err.txt");
    return 0;
}

int main(void)
{
    char path[128];
    char command[1024];
    int frame;
    bink2 b;
    unsigned long frames = 0;
    unsigned long f;
    unsigned char mask = 0;
    unsigned char channels[7] = { 0 };
    unsigned char depths[7] = { 0 };
    unsigned long rates[7] = { 0 };
    unsigned long audio_bytes = 0;

    CHECK(test_input_errors() == 0);
    make_dir("build_e2e_frames");
    for (frame = 0; frame < FRAMES; frame++) {
        snprintf(path, sizeof(path), "build_e2e_frames/frame%d.ppm", frame);
        CHECK(write_ppm(path, frame) == 0);
    }
    CHECK(write_wav("build_e2e.wav") == 0);
    snprintf(command, sizeof(command),
             "\"%s\" build_e2e_frames -o build_e2e.bk2 --fps 15 --slices 4 "
             "--quality 80 --audio build_e2e.wav --audio-quality 60 --verify",
             BINK2_BUILD_EXE);
    CHECK(system(command) == 0);

    b = bink2_open_file("build_e2e.bk2", BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_info_all(b, NULL, &frames, NULL) == 0);
    CHECK(frames == FRAMES);
    CHECK(bink2_info_audio(b, &mask, channels, depths, rates) == 0);
    CHECK((mask & 1) != 0 && channels[0] == 2 && depths[0] == 16 &&
          rates[0] == 48000);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_enable_audio(b, 0, 1) == 0);
    for (f = 0; f < FRAMES; f++) {
        CHECK((f == 0 ? bink2_first(b) : bink2_next(b)) != BINK2_ERROR);
        audio_bytes += bink2_get_audio_size(b, 0);
    }
    CHECK(audio_bytes == 9600UL * 2UL * 2UL);
    bink2_close(b);

    for (frame = 0; frame < FRAMES; frame++) {
        snprintf(path, sizeof(path), "build_e2e_frames/frame%d.ppm", frame);
        remove(path);
    }
    remove_dir("build_e2e_frames");
    remove("build_e2e.wav");
    remove("build_e2e.bk2");
    return 0;
}
