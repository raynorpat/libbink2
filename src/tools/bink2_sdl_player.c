#include "bink2.h"
#include "bink2_mix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if BINK2_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#define MIX_FRAMES 16384

static int poll_events(int *paused, int *seek_delta)
{
    SDL_Event event;
    int quit = 0;

    while (SDL_PollEvent(&event)) {
#if BINK2_USE_SDL3
        if (event.type == SDL_EVENT_QUIT) {
            quit = 1;
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            SDL_Keycode key = event.key.key;

            if (key == SDLK_ESCAPE || key == SDLK_Q)
                quit = 1;
            else if (key == SDLK_SPACE)
                *paused = !*paused;
            else if (key == SDLK_LEFT)
                *seek_delta = -1;
            else if (key == SDLK_RIGHT)
                *seek_delta = 1;
        }
#else
        if (event.type == SDL_QUIT) {
            quit = 1;
        } else if (event.type == SDL_KEYDOWN) {
            SDL_Keycode key = event.key.keysym.sym;

            if (key == SDLK_ESCAPE || key == SDLK_q)
                quit = 1;
            else if (key == SDLK_SPACE)
                *paused = !*paused;
            else if (key == SDLK_LEFT)
                *seek_delta = -1;
            else if (key == SDLK_RIGHT)
                *seek_delta = 1;
        }
#endif
    }
    return quit;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    bink2 b;
    unsigned long width = 0, height = 0, frames = 0;
    unsigned long rate = 48000;
    double usf = 0.0;
    unsigned char mask = 0;
    unsigned char channels[7] = { 0 };
    unsigned char depths[7] = { 0 };
    unsigned long rates[7] = { 0 };
    int tracks = 0;
    int test_frames = 0;
    int decoded = 0;
    int paused = 0;
    int result = 0;
    int status;

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
#if BINK2_USE_SDL3
    SDL_AudioStream *stream = NULL;
#else
    SDL_AudioDeviceID device = 0;
#endif
    short mix[MIX_FRAMES * 2];

    if (argc != 2) {
        fprintf(stderr, "usage: bink2_sdl_player video.bk2\n");
        return 2;
    }
    path = argv[1];
    {
        const char *env = getenv("BINK2_PLAYER_TEST_FRAMES");

        if (env != NULL)
            test_frames = atoi(env);
    }

    b = bink2_open_file(path, BINK2_MODE_DISK);
    if (b == NULL) {
        fprintf(stderr, "bink2_sdl_player: cannot open %s\n", path);
        return 1;
    }
    if (bink2_info_all(b, NULL, &frames, &usf) != 0 ||
        bink2_info_video(b, &width, &height, NULL) != 0) {
        fprintf(stderr, "bink2_sdl_player: cannot read file info\n");
        bink2_close(b);
        return 1;
    }
    bink2_info_audio(b, &mask, channels, depths, rates);
    for (tracks = 0; tracks < 7; tracks++) {
        if (!(mask & (1u << tracks)))
            break;
        bink2_enable_audio(b, (unsigned char)tracks, 1);
    }
    if (tracks > 0 && rates[0] != 0)
        rate = rates[0];

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "bink2_sdl_player: SDL init failed: %s\n",
                SDL_GetError());
        bink2_close(b);
        return 77;
    }
#if BINK2_USE_SDL3
    if (!SDL_CreateWindowAndRenderer(path, (int)width, (int)height, 0,
                                     &window, &renderer)) {
        fprintf(stderr, "bink2_sdl_player: window failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        bink2_close(b);
        return 77;
    }
#else
    window = SDL_CreateWindow(path, SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, (int)width,
                              (int)height, 0);
    if (window == NULL) {
        fprintf(stderr, "bink2_sdl_player: window failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        bink2_close(b);
        return 77;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL)
        renderer = SDL_CreateRenderer(window, -1, 0);
#endif
    if (renderer == NULL) {
        fprintf(stderr, "bink2_sdl_player: renderer failed: %s\n",
                SDL_GetError());
        if (window != NULL)
            SDL_DestroyWindow(window);
        SDL_Quit();
        bink2_close(b);
        return 77;
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                                SDL_TEXTUREACCESS_STREAMING, (int)width,
                                (int)height);
    if (texture == NULL) {
        fprintf(stderr, "bink2_sdl_player: texture failed: %s\n",
                SDL_GetError());
        result = 1;
        goto done;
    }

    if (tracks > 0) {
#if BINK2_USE_SDL3
        SDL_AudioSpec spec;

        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = (int)rate;
        stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (stream != NULL)
            SDL_ResumeAudioStreamDevice(stream);
#else
        SDL_AudioSpec want;

        SDL_zero(want);
        want.freq = (int)rate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (device != 0)
            SDL_PauseAudioDevice(device, 0);
#endif
    }

    bink2_enable_video(b, 1);
    status = bink2_first(b);
    while (status == BINK2_MORE || status == BINK2_LAST) {
        const unsigned char *rgb = bink2_get_video(b);
        int seek_delta = 0;

        if (rgb == NULL) {
            fprintf(stderr, "bink2_sdl_player: frame unavailable\n");
            result = 1;
            break;
        }
        SDL_UpdateTexture(texture, NULL, rgb, (int)width * 3);
        SDL_RenderClear(renderer);
#if BINK2_USE_SDL3
        SDL_RenderTexture(renderer, texture, NULL, NULL);
#else
        SDL_RenderCopy(renderer, texture, NULL, NULL);
#endif
        SDL_RenderPresent(renderer);

        if (poll_events(&paused, &seek_delta))
            break;
        if (seek_delta != 0) {
            unsigned long current = 0;

            bink2_info_all(b, &current, NULL, NULL);
            if (seek_delta < 0 && current > 30)
                current -= 30;
            else if (seek_delta < 0)
                current = 0;
            else
                current += 30;
            status = bink2_seek_keyframe(b, current);
            continue;
        }
        if (paused) {
            SDL_Delay(16);
            continue;
        }

        if (tracks > 0) {
            unsigned long mixed = bink2_mix_frame(b, mix, MIX_FRAMES,
                                                  (unsigned char)tracks);

            if (mixed > 0) {
#if BINK2_USE_SDL3
                if (stream != NULL)
                    SDL_PutAudioStreamData(stream, mix,
                                           (int)(mixed * 2 * sizeof(short)));
#else
                if (device != 0)
                    SDL_QueueAudio(device, mix,
                                   (Uint32)(mixed * 2 * sizeof(short)));
#endif
            }
        }

        decoded++;
        if (test_frames > 0 && decoded >= test_frames)
            break;
        if (status == BINK2_LAST)
            break;
        if (usf > 0.0)
            SDL_Delay((Uint32)(usf / 1000.0));
        status = bink2_next(b);
        if (status == BINK2_ERROR) {
            fprintf(stderr, "bink2_sdl_player: decode error\n");
            result = 1;
            break;
        }
    }

done:
#if BINK2_USE_SDL3
    if (stream != NULL)
        SDL_DestroyAudioStream(stream);
#else
    if (device != 0)
        SDL_CloseAudioDevice(device);
#endif
    if (texture != NULL)
        SDL_DestroyTexture(texture);
    if (renderer != NULL)
        SDL_DestroyRenderer(renderer);
    if (window != NULL)
        SDL_DestroyWindow(window);
    SDL_Quit();
    bink2_close(b);
    return result;
}
