#ifndef LIBBINK2_ENC_IMAGE_H
#define LIBBINK2_ENC_IMAGE_H

/** Load a PPM, BMP, PNG, or JPEG image as packed RGB24. */
int bink2_enc_image_load(const char *path, unsigned char **rgb,
                         unsigned *width, unsigned *height);

#endif
