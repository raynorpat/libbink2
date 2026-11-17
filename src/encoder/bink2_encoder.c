#include "bink2_encoder.h"

#include <stdio.h>

#define BINK2_ENC_MAX_FRAMES 1000000UL
#define BINK2_ENC_MAX_WIDTH 7680
#define BINK2_ENC_MAX_HEIGHT 4800
#define BINK2_ENC_MAX_MOTION 64

static void set_error(char *err, size_t errlen, const char *message)
{
    if (err == NULL || errlen == 0)
        return;
    snprintf(err, errlen, "%s", message);
}

static int valid_source(const bink2_enc_source *source)
{
    if (source == NULL)
        return 0;
    if (source->width < 1 || source->width > BINK2_ENC_MAX_WIDTH)
        return 0;
    if (source->height < 1 || source->height > BINK2_ENC_MAX_HEIGHT)
        return 0;
    if (source->frame_count < 1 || source->frame_count > BINK2_ENC_MAX_FRAMES)
        return 0;
    if (source->fps_num == 0 || source->fps_den == 0)
        return 0;
    if (source->read_frame == NULL && source->read_frame_yuv == NULL)
        return 0;
    return 1;
}

static int valid_options(const bink2_enc_options *options)
{
    if (options == NULL)
        return 0;
    if (options->slices != 2 && options->slices != 3 &&
        options->slices != 4 && options->slices != 8)
        return 0;
    if (options->motion_range > BINK2_ENC_MAX_MOTION)
        return 0;
    if (options->quality < 0 || options->quality > 100)
        return 0;
    return 1;
}

void bink2_enc_options_default(bink2_enc_options *options)
{
    if (options == NULL)
        return;
    options->keyframe_interval = 150;
    options->motion_range = 8;
    options->slices = 2;
    options->four_mv = 1;
    options->quality = 80;
    options->target_bitrate = 0;
    options->verify = 0;
    options->recon_path = NULL;
    options->verbose = 0;
}

bink2_enc_status bink2_enc_encode(const bink2_enc_source *source,
                                  const bink2_enc_options *options,
                                  const char *output_path, char *err,
                                  size_t errlen)
{
    if (err != NULL && errlen != 0)
        err[0] = '\0';

    if (source == NULL) {
        set_error(err, errlen, "source is NULL");
        return BINK2_ENC_ERR_USAGE;
    }
    if (output_path == NULL) {
        set_error(err, errlen, "output path is NULL");
        return BINK2_ENC_ERR_USAGE;
    }
    if (!valid_source(source)) {
        set_error(err, errlen, "invalid source dimensions, frame count, "
                               "frame rate, or callbacks");
        return BINK2_ENC_ERR_USAGE;
    }
    if (!valid_options(options)) {
        set_error(err, errlen, "invalid options: slices must be 2, 3, 4 or 8, "
                               "motion_range 0..64, quality 0..100");
        return BINK2_ENC_ERR_USAGE;
    }

    set_error(err, errlen, "encoder not implemented");
    return BINK2_ENC_ERR_ENCODE;
}
