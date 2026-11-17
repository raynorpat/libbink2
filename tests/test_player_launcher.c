#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#endif

static void set_env(const char *name, const char *value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

int main(int argc, char **argv)
{
    const char *media = getenv("BINK2_TEST_MEDIA");
    char path[4096];
    char command[8192];
    int status;
    int code;

    if (argc != 2) {
        fprintf(stderr, "usage: %s player\n", argv[0]);
        return 2;
    }
    if (media == NULL || media[0] == '\0')
        return 77;
    snprintf(path, sizeof(path), "%s/ge_video_86l.bk2", media);
    set_env("SDL_VIDEODRIVER", "dummy");
    set_env("SDL_AUDIODRIVER", "dummy");
    set_env("BINK2_PLAYER_TEST_FRAMES", "3");
    set_env("BINK2_D3D_TEST_FRAMES", "3");
    snprintf(command, sizeof(command), "\"%s\" \"%s\"", argv[1], path);
    status = system(command);
#if defined(_WIN32)
    code = status;
#else
    code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
    return code;
}
