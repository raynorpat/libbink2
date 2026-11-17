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

int main(void)
{
    char path[128];
    char command[1024];
    int frame;
    bink2 b;
    unsigned long frames = 0;
    unsigned long f;

#if defined(_WIN32)
    _mkdir("build_e2e_frames");
#else
    mkdir("build_e2e_frames", 0777);
#endif
    for (frame = 0; frame < FRAMES; frame++) {
        snprintf(path, sizeof(path), "build_e2e_frames/frame%d.ppm", frame);
        CHECK(write_ppm(path, frame) == 0);
    }
    snprintf(command, sizeof(command),
             "\"%s\" build_e2e_frames -o build_e2e.bk2 --fps 15 --slices 4 "
             "--quality 80 --verify",
             BINK2_BUILD_EXE);
    CHECK(system(command) == 0);

    b = bink2_open_file("build_e2e.bk2", BINK2_MODE_DISK);
    CHECK(b != NULL);
    CHECK(bink2_info_all(b, NULL, &frames, NULL) == 0);
    CHECK(frames == FRAMES);
    CHECK(bink2_enable_video(b, 1) == 0);
    CHECK(bink2_first(b) != BINK2_ERROR);
    for (f = 1; f < FRAMES; f++)
        CHECK(bink2_next(b) != BINK2_ERROR);
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
    remove("build_e2e.bk2");
    return 0;
}
