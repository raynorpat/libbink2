#include "bink2_encoder.h"
#include "bink2_enc_container.h"
#include "bink2_enc_internal.h"
#include "bink2_enc_motion.h"
#include "bink2_enc_video.h"
#include "bink2_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static unsigned long slice_flags(unsigned slices)
{
    switch (slices) {
    case 3:
        return 0x11;
    case 4:
        return 0x12;
    case 8:
        return 0x13;
    default:
        return 0x10;
    }
}

static int write_u32(FILE *file, unsigned long value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xFF);
    bytes[1] = (unsigned char)((value >> 8) & 0xFF);
    bytes[2] = (unsigned char)((value >> 16) & 0xFF);
    bytes[3] = (unsigned char)((value >> 24) & 0xFF);
    return fwrite(bytes, 1, 4, file) == 4 ? 0 : -1;
}

static int write_recon(FILE *file, const Bink2Frame *frame)
{
    int cw = (frame->width + 1) / 2;
    int ch = (frame->height + 1) / 2;
    int y;

    for (y = 0; y < frame->height; y++)
        if (fwrite(frame->y + (size_t)y * frame->stride[0], 1,
                   (size_t)frame->width, file) != (size_t)frame->width)
            return -1;
    for (y = 0; y < ch; y++)
        if (fwrite(frame->u + (size_t)y * frame->stride[1], 1,
                   (size_t)cw, file) != (size_t)cw)
            return -1;
    for (y = 0; y < ch; y++)
        if (fwrite(frame->v + (size_t)y * frame->stride[2], 1,
                   (size_t)cw, file) != (size_t)cw)
            return -1;
    return 0;
}

static int verify_written(FILE *file, FILE *recon, int width, int height,
                          unsigned long frames)
{
    bink2 b;
    unsigned char *ref;
    int cw = (width + 1) / 2;
    int ch = (height + 1) / 2;
    size_t y_size = (size_t)width * height;
    size_t c_size = (size_t)cw * ch;
    unsigned long f;
    int ok = 1;

    ref = (unsigned char *)malloc(y_size + c_size * 2);
    if (ref == NULL)
        return -1;
    b = bink2_open_filepointer(file, BINK2_MODE_DISK);
    if (b == NULL) {
        free(ref);
        return -1;
    }
    bink2_enable_video(b, 1);
    for (f = 0; ok && f < frames; f++) {
        int status = f == 0 ? bink2_first(b) : bink2_next(b);
        int plane;

        if (status == BINK2_ERROR)
            ok = 0;
        for (plane = 0; ok && plane < 3; plane++) {
            const unsigned char *decoded = bink2_get_plane(b, plane);
            int stride = bink2_get_stride(b, plane);
            int pw = plane == 0 ? width : cw;
            int ph = plane == 0 ? height : ch;
            size_t size = plane == 0 ? y_size : c_size;
            int row;

            if (decoded == NULL ||
                fread(ref, 1, size, recon) != size) {
                ok = 0;
                break;
            }
            for (row = 0; ok && row < ph; row++)
                if (memcmp(decoded + (size_t)row * stride,
                           ref + (size_t)row * pw, (size_t)pw) != 0)
                    ok = 0;
        }
    }
    bink2_close(b);
    free(ref);
    return ok ? 0 : -1;
}

int bink2_enc_encode_intra(const bink2_enc_source *source,
                           const bink2_enc_options *options, FILE *file,
                           short *quality)
{
    unsigned long *offsets = NULL;
    unsigned char *keys = NULL;
    unsigned long index_offset = 0;
    unsigned long largest = 0;
    unsigned long end = 0;
    int heights[8] = { 0 };
    int slices;
    Bink2Frame frame;
    Bink2Video state;
    FILE *recon = NULL;
    FILE *verify_recon = NULL;
    unsigned long f;
    unsigned char *previous = NULL;
    int frame_q;
    long target_bytes = 0;
    int verify_failed = 0;
    int input_failed = 0;
    int rc = -1;

    if (source == NULL || options == NULL || file == NULL)
        return BINK2_ENC_ERR_USAGE;
    slices = (int)options->slices;
    frame_q = 8 + (100 - options->quality) / 5;
    if (frame_q < 1)
        frame_q = 1;
    if (frame_q > 36)
        frame_q = 36;
    if (options->target_bitrate > 0) {
        frame_q = 12;
        target_bytes = (long)((options->target_bitrate * source->fps_den) /
                              (source->fps_num * 8));
        if (target_bytes < 1)
            target_bytes = 1;
    }

    if (bink2_container_write_header(file, (unsigned long)source->width,
                                     (unsigned long)source->height,
                                     source->fps_num, source->fps_den,
                                     source->frame_count,
                                     slice_flags((unsigned) slices),
                                     &index_offset) != 0)
        return BINK2_ENC_ERR_ENCODE;

    if (options->recon_path != NULL) {
        recon = fopen(options->recon_path, "wb");
        if (recon == NULL)
            goto done;
    }
    if (options->verify) {
        verify_recon = tmpfile();
        if (verify_recon == NULL)
            goto done;
    }
    offsets = (unsigned long *)calloc(source->frame_count + 1,
                                      sizeof(unsigned long));
    keys = (unsigned char *)calloc(source->frame_count, 1);
    if (offsets == NULL || keys == NULL)
        goto done;
    if (bink2_frame_alloc(&frame, source->width, source->height) != 0)
        goto done;
    previous = (unsigned char *)calloc(1, (size_t)source->width *
                                       (size_t)source->height);
    if (previous == NULL) {
        bink2_frame_free(&frame);
        goto done;
    }
    memset(&state, 0, sizeof(state));
    if (bink2_video_init(&state, (unsigned long)source->width,
                         (unsigned long)source->height,
                         slice_flags((unsigned) slices), 'j') != 0) {
        bink2_frame_free(&frame);
        goto done;
    }
    if (bink2_video_slice_heights('j', slice_flags((unsigned) slices),
                                  (unsigned long)source->height,
                                  heights) != slices) {
        bink2_video_uninit(&state);
        bink2_frame_free(&frame);
        goto done;
    }

    bink2_enc_reset_stats();
    for (f = 0; f < source->frame_count; f++) {
        Bink2BitWriter bw;
        size_t bytes;
        long offset;
        int is_key;
        int scene_cut = 0;

        if (bink2_enc_read_frame(source, f, &frame) != 0) {
            input_failed = 1;
            bink2_video_uninit(&state);
            bink2_frame_free(&frame);
            goto done;
        }
        if (f > 0) {
            unsigned long sum = 0;
            unsigned long i;
            unsigned long count = (unsigned long)source->width *
                                  (unsigned long)source->height;

            for (i = 0; i < count; i++)
                sum += (unsigned long)abs((int)frame.y[i] -
                                          (int)previous[i]);
            if (sum > count * 24)
                scene_cut = 1;
        }
        memcpy(previous, frame.y,
               (size_t)source->width * (size_t)source->height);

        bink2_enc_reset_frame(&state);
        bink2_bw_init(&bw);
        is_key = options->keyframe_interval == 0 || f == 0 ||
                 (options->keyframe_interval > 0 &&
                  f % options->keyframe_interval == 0) || scene_cut;
        if (bink2_enc_encode_frame(&bw, &frame, &state, frame_q, slices,
                                   heights, is_key,
                                   (int)options->motion_range,
                                   options->four_mv) != 0) {
            bink2_bw_free(&bw);
            bink2_video_uninit(&state);
            bink2_frame_free(&frame);
            goto done;
        }
        bytes = bink2_bw_bytes(&bw);
        offset = ftell(file);
        if (offset < 0 || (unsigned long)offset > 0xFFFFFFFFUL - bytes ||
            fwrite(bink2_bw_data(&bw), 1, bytes, file) != bytes) {
            bink2_bw_free(&bw);
            bink2_video_uninit(&state);
            bink2_frame_free(&frame);
            goto done;
        }
        offsets[f] = (unsigned long)offset;
        keys[f] = (unsigned char)(is_key ? 1 : 0);
        if (bytes > largest)
            largest = (unsigned long)bytes;
        if (target_bytes > 0) {
            if ((long)bytes > target_bytes * 3 / 2)
                frame_q += 3;
            else if ((long)bytes > target_bytes * 5 / 4)
                frame_q += 2;
            else if ((long)bytes > target_bytes)
                frame_q += 1;
            else if ((long)bytes < target_bytes * 2 / 3)
                frame_q -= 3;
            else if ((long)bytes < target_bytes * 4 / 5)
                frame_q -= 2;
            else if ((long)bytes < target_bytes)
                frame_q -= 1;
            if (frame_q < 1)
                frame_q = 1;
            if (frame_q > 36)
                frame_q = 36;
        }
        if (recon != NULL && write_recon(recon, &frame) != 0) {
            bink2_bw_free(&bw);
            bink2_video_uninit(&state);
            bink2_frame_free(&frame);
            goto done;
        }
        if (verify_recon != NULL && write_recon(verify_recon, &frame) != 0) {
            bink2_bw_free(&bw);
            bink2_video_uninit(&state);
            bink2_frame_free(&frame);
            goto done;
        }
        if (options->verbose)
            fprintf(stderr, "bink2 encoder: frame %lu/%lu\n", f + 1,
                    source->frame_count);
        bink2_bw_free(&bw);
    }
    end = (unsigned long)ftell(file);
    offsets[source->frame_count] = end;
    if (bink2_container_patch_index(file, index_offset, offsets, keys,
                                    source->frame_count) != 0) {
        bink2_video_uninit(&state);
        bink2_frame_free(&frame);
        goto done;
    }
    if (fseek(file, 4, SEEK_SET) != 0 || write_u32(file, end - 8) != 0 ||
        fseek(file, 12, SEEK_SET) != 0 || write_u32(file, largest) != 0 ||
        fseek(file, 0, SEEK_END) != 0) {
        bink2_video_uninit(&state);
        bink2_frame_free(&frame);
        goto done;
    }
    if (verify_recon != NULL) {
        fflush(file);
        fflush(verify_recon);
        if (fseek(verify_recon, 0, SEEK_SET) != 0 ||
            verify_written(file, verify_recon, source->width, source->height,
                           source->frame_count) != 0)
            verify_failed = 1;
        fseek(file, 0, SEEK_END);
    }
    if (quality != NULL)
        *quality = (short)frame_q;
    bink2_video_uninit(&state);
    bink2_frame_free(&frame);
    rc = 0;

done:
    if (recon != NULL)
        fclose(recon);
    if (verify_recon != NULL)
        fclose(verify_recon);
    free(previous);
    free(offsets);
    free(keys);
    if (verify_failed)
        return BINK2_ENC_ERR_VERIFY;
    if (input_failed)
        return BINK2_ENC_ERR_INPUT;
    if (rc != 0)
        return BINK2_ENC_ERR_ENCODE;
    return BINK2_ENC_OK;
}

bink2_enc_status bink2_enc_encode(const bink2_enc_source *source,
                                  const bink2_enc_options *options,
                                  const char *output_path, char *err,
                                  size_t errlen)
{
    FILE *file;
    short q = 0;
    int rc;

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

    file = fopen(output_path, "wb+");
    if (file == NULL) {
        set_error(err, errlen, "cannot create the output file");
        return BINK2_ENC_ERR_ENCODE;
    }
    rc = bink2_enc_encode_intra(source, options, file, &q);
    if (fclose(file) != 0 && rc == BINK2_ENC_OK)
        rc = BINK2_ENC_ERR_ENCODE;
    if (rc == BINK2_ENC_ERR_VERIFY) {
        set_error(err, errlen, "encoder verification failed");
        return BINK2_ENC_ERR_VERIFY;
    }
    if (rc == BINK2_ENC_ERR_INPUT) {
        set_error(err, errlen, "failed to read a source frame");
        return BINK2_ENC_ERR_INPUT;
    }
    if (rc != BINK2_ENC_OK) {
        set_error(err, errlen, "encoder failed");
        return BINK2_ENC_ERR_ENCODE;
    }
    return BINK2_ENC_OK;
}
