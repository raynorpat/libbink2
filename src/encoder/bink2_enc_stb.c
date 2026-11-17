/**
    libbink2 encoder

    PNG, JPEG, and BMP decoding is stb_image (public domain), vendored at
    third_party/stb/stb_image.h. That header is not installed.
*/

#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_MAX_DIMENSIONS 7680
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define STBI_NEON
#endif

#include "stb_image.h"

uint8_t *bink2_enc_stbi_load(const char *path, int *width, int *height,
                             const char **reason)
{
    int comp = 0;
    unsigned char *pixels;

    if (reason != NULL)
        *reason = NULL;
    if (path == NULL || width == NULL || height == NULL)
        return NULL;
    pixels = stbi_load(path, width, height, &comp, 3);
    if (pixels == NULL && reason != NULL)
        *reason = stbi_failure_reason();
    return (uint8_t *)pixels;
}

void bink2_enc_stbi_free(uint8_t *pixels)
{
    stbi_image_free(pixels);
}
