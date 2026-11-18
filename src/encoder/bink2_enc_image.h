#ifndef LIBBINK2_ENC_IMAGE_H
#define LIBBINK2_ENC_IMAGE_H

#include <stddef.h>

/**
 * Load a PPM, BMP, PNG, or JPEG image as packed RGB24. On failure, when
 * `err` is non-NULL, it receives a NUL-terminated reason.
 */
int bink2_enc_image_load(const char *path, unsigned char **rgb,
                         unsigned *width, unsigned *height, char *err,
                         size_t errlen);

#endif
