#include "bink2_enc_image.h"
#include "test_common.h"

#include <stdio.h>

static int check_image(const char *path)
{
    unsigned char *rgb = NULL;
    unsigned width = 0, height = 0;

    CHECK(bink2_enc_image_load(path, &rgb, &width, &height) == 0);
    CHECK(width == 4 && height == 4);
    CHECK(rgb != NULL);
    free(rgb);
    return 0;
}

int main(void)
{
    unsigned char *rgb = NULL;
    unsigned width = 0, height = 0;
    FILE *file;
    int x, y;

    CHECK(check_image(BINK2_FIXTURE_DIR "/fixture.png") == 0);
    CHECK(check_image(BINK2_FIXTURE_DIR "/fixture.jpg") == 0);
    CHECK(check_image(BINK2_FIXTURE_DIR "/fixture.bmp") == 0);

    file = fopen("test_enc_image.ppm", "wb");
    CHECK(file != NULL);
    CHECK(fwrite("P6\n4 4\n255\n", 1, 11, file) == 11);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++) {
            unsigned char px[3] = {
                (unsigned char)(x * 60),
                (unsigned char)(y * 60),
                (unsigned char)(x * 20 + y * 30)
            };

            CHECK(fwrite(px, 1, 3, file) == 3);
        }
    fclose(file);
    CHECK(bink2_enc_image_load("test_enc_image.ppm", &rgb, &width, &height) ==
          0);
    CHECK(width == 4 && height == 4 && rgb != NULL);
    free(rgb);
    remove("test_enc_image.ppm");

    CHECK(bink2_enc_image_load("/nonexistent.png", &rgb, &width, &height) !=
          0);
    return 0;
}
