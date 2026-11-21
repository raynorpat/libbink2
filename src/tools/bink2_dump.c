#include "bink2.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash_plane(const unsigned char *plane, int width, int height,
                           int stride)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    int x;
    int y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            hash ^= plane[x];
            hash *= UINT64_C(1099511628211);
        }
        plane += stride;
    }
    return hash;
}

static int write_ppm(const char *outdir, unsigned long frame,
                     const unsigned char *rgb, unsigned long width,
                     unsigned long height)
{
    char path[1024];
    FILE *file;
    size_t size = (size_t)width * (size_t)height * 3;

    snprintf(path, sizeof(path), "%s/frame_%05lu.ppm", outdir, frame);
    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "bink2_dump: cannot create %s\n", path);
        return -1;
    }
    if (fprintf(file, "P6\n%lu %lu\n255\n", width, height) < 0 ||
        fwrite(rgb, 1, size, file) != size) {
        fprintf(stderr, "bink2_dump: cannot write %s\n", path);
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "bink2_dump: cannot close %s\n", path);
        return -1;
    }
    return 0;
}

static int write_plane(FILE *file, const unsigned char *plane, int width,
                       int height, int stride)
{
    int y;

    for (y = 0; y < height; y++) {
        if (fwrite(plane, 1, (size_t)width, file) != (size_t)width)
            return -1;
        plane += stride;
    }
    return 0;
}

#define DUMP_USAGE "usage: bink2_dump [--frames N] [--no-filters] " \
                   "[--yuv out.yuv] file.bk2 [outdir]\n"

int main(int argc, char **argv)
{
    const char *yuv_path = NULL;
    FILE *yuv = NULL;
    const char *path = NULL;
    const char *outdir = ".";
    unsigned long limit = 0;
    unsigned long frame_count = 0;
    unsigned long width = 0;
    unsigned long height = 0;
    unsigned long decoded = 0;
    int filters = 1;
    double usf = 0.0;
    int i;
    int status;
    bink2 b;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            limit = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--yuv") == 0 && i + 1 < argc) {
            yuv_path = argv[++i];
        } else if (strcmp(argv[i], "--no-filters") == 0) {
            filters = 0;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fputs(DUMP_USAGE, stderr);
            return 2;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            outdir = argv[i];
        }
    }
    if (path == NULL) {
        fputs(DUMP_USAGE, stderr);
        return 2;
    }

    b = bink2_open_file(path, BINK2_MODE_DISK);
    if (b == NULL) {
        fprintf(stderr, "bink2_dump: cannot open %s\n", path);
        return 1;
    }

    if (bink2_info_all(b, NULL, &frame_count, &usf) != 0 ||
        bink2_info_video(b, &width, &height, NULL) != 0) {
        fprintf(stderr, "bink2_dump: cannot read file info\n");
        bink2_close(b);
        return 1;
    }
    printf("%s: %lux%lu, %lu frames, %.3f fps\n", path, width, height,
           frame_count, usf > 0.0 ? 1000000.0 / usf : 0.0);

    if (yuv_path != NULL) {
        yuv = fopen(yuv_path, "wb");
        if (yuv == NULL) {
            fprintf(stderr, "bink2_dump: cannot create %s\n", yuv_path);
            bink2_close(b);
            return 1;
        }
    }

    bink2_enable_filters(b, (unsigned char)filters);
    if (bink2_enable_video(b, 1) != 0) {
        fprintf(stderr, "bink2_dump: cannot enable video\n");
        bink2_close(b);
        return 1;
    }

    status = bink2_first(b);
    while (status == BINK2_MORE || status == BINK2_LAST) {
        const unsigned char *y;
        const unsigned char *u;
        const unsigned char *v;
        const unsigned char *rgb;
        unsigned long frame = 0;

        if (bink2_info_all(b, &frame, NULL, NULL) != 0) {
            fprintf(stderr, "bink2_dump: cannot read frame number\n");
            bink2_close(b);
            return 1;
        }
        y = bink2_get_plane(b, BINK2_PLANE_Y);
        u = bink2_get_plane(b, BINK2_PLANE_U);
        v = bink2_get_plane(b, BINK2_PLANE_V);
        rgb = bink2_get_video(b);
        if (y == NULL || u == NULL || v == NULL || rgb == NULL) {
            fprintf(stderr, "bink2_dump: frame %lu is unavailable\n", frame);
            bink2_close(b);
            return 1;
        }

        printf("frame %lu y=%" PRIu64 " u=%" PRIu64 " v=%" PRIu64 "\n",
               frame,
               hash_plane(y, (int)width, (int)height,
                          bink2_get_stride(b, BINK2_PLANE_Y)),
               hash_plane(u, (int)((width + 1) >> 1),
                          (int)((height + 1) >> 1),
                          bink2_get_stride(b, BINK2_PLANE_U)),
               hash_plane(v, (int)((width + 1) >> 1),
                          (int)((height + 1) >> 1),
                          bink2_get_stride(b, BINK2_PLANE_V)));

        if (yuv != NULL) {
            int cw = (int)((width + 1) >> 1);
            int ch = (int)((height + 1) >> 1);

            if (write_plane(yuv, y, (int)width, (int)height,
                            bink2_get_stride(b, BINK2_PLANE_Y)) != 0 ||
                write_plane(yuv, u, cw, ch,
                            bink2_get_stride(b, BINK2_PLANE_U)) != 0 ||
                write_plane(yuv, v, cw, ch,
                            bink2_get_stride(b, BINK2_PLANE_V)) != 0) {
                fprintf(stderr, "bink2_dump: cannot write %s\n", yuv_path);
                fclose(yuv);
                bink2_close(b);
                return 1;
            }
        } else if (write_ppm(outdir, frame, rgb, width, height) != 0) {
            bink2_close(b);
            return 1;
        }
        decoded++;
        if (limit != 0 && decoded >= limit)
            break;
        if (status == BINK2_LAST)
            break;
        status = bink2_next(b);
        if (status == BINK2_ERROR) {
            fprintf(stderr, "bink2_dump: decode error at frame %lu\n",
                    frame + 1);
            bink2_close(b);
            return 1;
        }
    }

    if (yuv != NULL && fclose(yuv) != 0) {
        fprintf(stderr, "bink2_dump: cannot close %s\n", yuv_path);
        bink2_close(b);
        return 1;
    }
    bink2_close(b);
    return 0;
}
