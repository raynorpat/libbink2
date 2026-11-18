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

#if defined(_WIN32)
    _mkdir("build_e2e_frames");
#else
    mkdir("build_e2e_frames", 0777);
#endif
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
#if defined(_WIN32)
    _rmdir("build_e2e_frames");
#else
    rmdir("build_e2e_frames");
#endif
    remove("build_e2e.wav");
    remove("build_e2e.bk2");
    return 0;
}
