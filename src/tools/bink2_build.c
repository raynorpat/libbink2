/**
    bink2_build - encode a Bink 2 file from still images
    Copyright (C) 2026 libbink2 contributors

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
*/

#include "bink2.h"
#include "bink2_encoder.h"
#include "bink2_enc_image.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define BINK2_BUILD_VERSION "0.1.0"

typedef struct {
    char **paths;
    unsigned count;
    unsigned width;
    unsigned height;
} build_input;

static void usage(FILE *out, const char *program)
{
    fprintf(out,
        "usage: %s <input> [-o output.bk2] [options]\n"
        "\n"
        "  <input>                 directory of .ppm/.bmp/.png/.jpg/.jpeg frames, or one image\n"
        "  -o, --output FILE       output .bk2 (default: <input>.bk2)\n"
        "      --fps N[/D]         frame rate (default 30)\n"
        "      --keyframe-interval N  frames between keyframes (default 150; 0 = all intra)\n"
        "      --motion-range N    motion search radius 0..64 (default 8)\n"
        "      --slices N          slices per frame: 2, 3, 4 or 8 (default 2)\n"
        "      --quality N         0..100 (default 80)\n"
        "      --bitrate N         target bits per second (default 0 = quality mode)\n"
        "      --four-mv 0|1       four motion vectors per macroblock (default 1)\n"
        "      --recon FILE        write the encoder reconstruction as raw YUV 4:2:0\n"
        "      --verify            decode the result and check it is readable\n"
        "      --verbose           print progress\n"
        "      --quiet             suppress progress messages\n"
        "  -h, --help              show this help\n"
        "      --version           show the version\n",
        program);
}

static int usage_error(const char *program, const char *message)
{
    fprintf(stderr, "bink2_build: %s\n", message);
    usage(stderr, program);
    return BINK2_ENC_ERR_USAGE;
}

static int parse_uint(const char *text, unsigned *out)
{
    char *end;
    unsigned long value = strtoul(text, &end, 10);

    if (end == text || *end != '\0' || value > 0xffffffffUL)
        return -1;
    *out = (unsigned)value;
    return 0;
}

static int parse_fps(const char *text, unsigned long *num, unsigned long *den)
{
    char *end;
    unsigned long n;
    unsigned long d = 1;

    n = strtoul(text, &end, 10);
    if (end == text || n == 0 || n > 1000000UL)
        return -1;
    if (*end == '/') {
        const char *start = end + 1;

        d = strtoul(start, &end, 10);
        if (end == start || d == 0 || d > 1000000UL)
            return -1;
    }
    if (*end != '\0')
        return -1;
    *num = n;
    *den = d;
    return 0;
}

static void derive_output(const char *input, char *out, size_t outlen)
{
    const char *base = input;
    const char *dot;
    size_t length;
    size_t i;

    for (i = 0; input[i] != '\0'; i++) {
        if (input[i] == '/' || input[i] == '\\')
            base = input + i + 1;
    }
    if (*base == '\0')
        base = "out";
    dot = strrchr(base, '.');
    length = dot != NULL && dot != base ? (size_t)(dot - base) : strlen(base);
    if (length > outlen - 5)
        length = outlen - 5;
    memcpy(out, base, length);
    memcpy(out + length, ".bk2", 5);
}

static int ends_with_ci(const char *name, const char *ext)
{
    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);
    size_t i;

    if (name_len < ext_len)
        return 0;
    for (i = 0; i < ext_len; i++) {
        unsigned char a = (unsigned char)name[name_len - ext_len + i];
        unsigned char b = (unsigned char)ext[i];

        if (tolower(a) != tolower(b))
            return 0;
    }
    return 1;
}

static int has_image_extension(const char *name)
{
    return ends_with_ci(name, ".ppm") || ends_with_ci(name, ".bmp") ||
           ends_with_ci(name, ".png") || ends_with_ci(name, ".jpg") ||
           ends_with_ci(name, ".jpeg");
}

static char *duplicate_string(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);

    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

static char *join_path(const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int need_sep = dir_len > 0 && dir[dir_len - 1] != '/' &&
                   dir[dir_len - 1] != '\\';
    char *out = (char *)malloc(dir_len + name_len + 2);

    if (out == NULL)
        return NULL;
    memcpy(out, dir, dir_len);
    if (need_sep)
        out[dir_len++] = '/';
    memcpy(out + dir_len, name, name_len + 1);
    return out;
}

/* natural_compare: frame2 before frame10. */
static int natural_compare(const char *a, const char *b)
{
    size_t i = 0;
    size_t j = 0;

    while (a[i] != '\0' && b[j] != '\0') {
        if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
            size_t a_start;
            size_t b_start;
            int cmp;

            while (a[i] == '0')
                i++;
            while (b[j] == '0')
                j++;
            a_start = i;
            b_start = j;
            while (isdigit((unsigned char)a[i]))
                i++;
            while (isdigit((unsigned char)b[j]))
                j++;
            if (i - a_start != j - b_start)
                return i - a_start < j - b_start ? -1 : 1;
            cmp = memcmp(a + a_start, b + b_start, i - a_start);
            if (cmp != 0)
                return cmp < 0 ? -1 : 1;
        } else {
            int ca = tolower((unsigned char)a[i]);
            int cb = tolower((unsigned char)b[j]);

            if (ca != cb)
                return ca < cb ? -1 : 1;
            i++;
            j++;
        }
    }
    if (a[i] != '\0')
        return 1;
    if (b[j] != '\0')
        return -1;
    return 0;
}

static int name_compare(const void *left, const void *right)
{
    const char *a = *(const char *const *)left;
    const char *b = *(const char *const *)right;
    int cmp = natural_compare(a, b);

    if (cmp != 0)
        return cmp;
    return strcmp(a, b);
}

static int path_is_dir(const char *path)
{
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path);

    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;

    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

static int path_exists(const char *path)
{
#if defined(_WIN32)
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat info;

    return stat(path, &info) == 0;
#endif
}

static void free_names(char **names, unsigned count)
{
    unsigned i;

    if (names == NULL)
        return;
    for (i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

static int push_name(char ***names, unsigned *count, unsigned *capacity,
                     const char *name)
{
    if (*count == *capacity) {
        unsigned next = *capacity == 0 ? 16u : *capacity * 2u;
        char **grown = (char **)realloc(*names, (size_t)next * sizeof(char *));

        if (grown == NULL)
            return -1;
        *names = grown;
        *capacity = next;
    }
    (*names)[*count] = duplicate_string(name);
    if ((*names)[*count] == NULL)
        return -1;
    (*count)++;
    return 0;
}

static int collect_names(const char *dir, char ***out_names,
                         unsigned *out_count)
{
    char **names = NULL;
    unsigned count = 0;
    unsigned capacity = 0;

#if defined(_WIN32)
    {
        char pattern[4096];
        WIN32_FIND_DATAA entry;
        HANDLE handle;

        snprintf(pattern, sizeof(pattern), "%s\\*", dir);
        handle = FindFirstFileA(pattern, &entry);
        if (handle == INVALID_HANDLE_VALUE)
            return -1;
        do {
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (!has_image_extension(entry.cFileName))
                continue;
            if (push_name(&names, &count, &capacity, entry.cFileName) != 0) {
                FindClose(handle);
                free_names(names, count);
                return -1;
            }
        } while (FindNextFileA(handle, &entry));
        FindClose(handle);
    }
#else
    {
        DIR *handle = opendir(dir);
        struct dirent *entry;

        if (handle == NULL)
            return -1;
        while ((entry = readdir(handle)) != NULL) {
            struct stat info;
            char *full;

            if (entry->d_name[0] == '.')
                continue;
            if (!has_image_extension(entry->d_name))
                continue;
            full = join_path(dir, entry->d_name);
            if (full == NULL) {
                closedir(handle);
                free_names(names, count);
                return -1;
            }
            if (stat(full, &info) != 0 || !S_ISREG(info.st_mode)) {
                free(full);
                continue;
            }
            free(full);
            if (push_name(&names, &count, &capacity, entry->d_name) != 0) {
                closedir(handle);
                free_names(names, count);
                return -1;
            }
        }
        closedir(handle);
    }
#endif
    if (count == 0) {
        free(names);
        return -1;
    }
    qsort(names, count, sizeof(char *), name_compare);
    *out_names = names;
    *out_count = count;
    return 0;
}

static void input_close(build_input *input)
{
    unsigned i;

    if (input == NULL)
        return;
    if (input->paths != NULL) {
        for (i = 0; i < input->count; i++)
            free(input->paths[i]);
        free(input->paths);
    }
    memset(input, 0, sizeof(*input));
}

static int input_read_frame(void *user, unsigned long index, unsigned char *rgb)
{
    build_input *input = (build_input *)user;
    unsigned char *loaded = NULL;
    unsigned width = 0;
    unsigned height = 0;

    if (input == NULL || rgb == NULL || index >= input->count)
        return -1;
    if (bink2_enc_image_load(input->paths[index], &loaded, &width,
                             &height) != 0)
        return -1;
    if (width != input->width || height != input->height) {
        free(loaded);
        return -1;
    }
    memcpy(rgb, loaded, (size_t)width * height * 3);
    free(loaded);
    return 0;
}

static int input_open(const char *path, int directory, build_input *input)
{
    char **names = NULL;
    unsigned count = 0;
    unsigned i;
    unsigned char *probe = NULL;
    unsigned width = 0;
    unsigned height = 0;

    memset(input, 0, sizeof(*input));
    if (directory) {
        if (collect_names(path, &names, &count) != 0)
            return -1;
    } else {
        if (!has_image_extension(path))
            return -1;
        names = (char **)malloc(sizeof(char *));
        if (names == NULL)
            return -1;
        names[0] = duplicate_string(path);
        if (names[0] == NULL) {
            free(names);
            return -1;
        }
        count = 1;
    }
    input->paths = (char **)calloc(count, sizeof(char *));
    if (input->paths == NULL) {
        free_names(names, count);
        return -1;
    }
    for (i = 0; i < count; i++) {
        input->paths[i] = directory ? join_path(path, names[i])
                                    : duplicate_string(names[i]);
        if (input->paths[i] == NULL) {
            input->count = i;
            input_close(input);
            free_names(names, count);
            return -1;
        }
        input->count = i + 1;
    }
    free_names(names, count);
    if (bink2_enc_image_load(input->paths[0], &probe, &width, &height) != 0) {
        input_close(input);
        return -1;
    }
    free(probe);
    input->width = width;
    input->height = height;
    return 0;
}

int main(int argc, char **argv)
{
    bink2_enc_options options;
    bink2_enc_source source;
    bink2_enc_status status;
    build_input input;
    const char *image_input = NULL;
    const char *output = NULL;
    const char *recon = NULL;
    char output_buffer[4096];
    char err[512];
    unsigned long fps_num = 30;
    unsigned long fps_den = 1;
    int verify = 0;
    int verbose = 0;
    int i;

    bink2_enc_options_default(&options);
    options.motion_range = 8;
    memset(&input, 0, sizeof(input));
    memset(&source, 0, sizeof(source));
    err[0] = '\0';

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout, argv[0]);
            return BINK2_ENC_OK;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("bink2_build %s\n", BINK2_BUILD_VERSION);
            return BINK2_ENC_OK;
        }
        if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            if (++i >= argc)
                return usage_error(argv[0], "--output needs a value");
            output = argv[i];
        } else if (strcmp(arg, "--fps") == 0) {
            if (++i >= argc || parse_fps(argv[i], &fps_num, &fps_den) != 0)
                return usage_error(argv[0], "--fps expects N or N/D");
        } else if (strcmp(arg, "--keyframe-interval") == 0) {
            if (++i >= argc ||
                parse_uint(argv[i], &options.keyframe_interval) != 0)
                return usage_error(argv[0],
                                   "--keyframe-interval expects a number");
        } else if (strcmp(arg, "--motion-range") == 0) {
            if (++i >= argc ||
                parse_uint(argv[i], &options.motion_range) != 0)
                return usage_error(argv[0], "--motion-range expects 0..64");
        } else if (strcmp(arg, "--slices") == 0) {
            if (++i >= argc || parse_uint(argv[i], &options.slices) != 0)
                return usage_error(argv[0], "--slices expects 2, 3, 4 or 8");
        } else if (strcmp(arg, "--quality") == 0) {
            unsigned value;

            if (++i >= argc || parse_uint(argv[i], &value) != 0 || value > 100)
                return usage_error(argv[0], "--quality expects 0..100");
            options.quality = (int)value;
        } else if (strcmp(arg, "--bitrate") == 0) {
            if (++i >= argc ||
                parse_uint(argv[i], &options.target_bitrate) != 0)
                return usage_error(argv[0], "--bitrate expects bits per second");
        } else if (strcmp(arg, "--four-mv") == 0) {
            unsigned value;

            if (++i >= argc || parse_uint(argv[i], &value) != 0 || value > 1)
                return usage_error(argv[0], "--four-mv expects 0 or 1");
            options.four_mv = (int)value;
        } else if (strcmp(arg, "--recon") == 0) {
            if (++i >= argc)
                return usage_error(argv[0], "--recon needs a value");
            recon = argv[i];
        } else if (strcmp(arg, "--verify") == 0) {
            verify = 1;
        } else if (strcmp(arg, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(arg, "--quiet") == 0) {
            verbose = 0;
        } else if (arg[0] == '-') {
            fprintf(stderr, "bink2_build: unknown option %s\n", arg);
            return BINK2_ENC_ERR_USAGE;
        } else if (image_input == NULL) {
            image_input = arg;
        } else {
            return usage_error(argv[0], "only one input is accepted");
        }
    }

    if (image_input == NULL)
        return usage_error(argv[0], "missing input");
    if (!path_exists(image_input)) {
        fprintf(stderr, "bink2_build: cannot open input\n");
        return BINK2_ENC_ERR_INPUT;
    }
    if (output == NULL) {
        derive_output(image_input, output_buffer, sizeof(output_buffer));
        output = output_buffer;
    }

    if (input_open(image_input, path_is_dir(image_input), &input) != 0) {
        fprintf(stderr, "bink2_build: cannot read image input\n");
        input_close(&input);
        return BINK2_ENC_ERR_INPUT;
    }

    source.user = &input;
    source.width = input.width;
    source.height = input.height;
    source.frame_count = input.count;
    source.fps_num = fps_num;
    source.fps_den = fps_den;
    source.read_frame = input_read_frame;
    options.recon_path = recon;
    options.verify = verify;

    if (verbose)
        fprintf(stderr, "bink2_build: encoding %s\n", image_input);
    status = bink2_enc_encode(&source, &options, output, err, sizeof(err));
    if (status != BINK2_ENC_OK) {
        if (err[0] != '\0')
            fprintf(stderr, "bink2_build: %s\n", err);
        input_close(&input);
        return status;
    }
    if (verbose)
        fprintf(stderr, "bink2_build: wrote %s\n", output);
    input_close(&input);
    return BINK2_ENC_OK;
}
