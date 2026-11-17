/*
 * libbink2 - SDL + Vulkan playback example
 *
 * On macOS this runs through MoltenVK, which the Vulkan loader picks up
 * from its ICD file.
 *
 * Keys: Esc/q quit, Space pauses, Left/Right seek one second,
 *       M toggles interpolation.
 *
 * Hooks (environment): BINK2_PLAYER_TEST_FRAMES, BINK2_PLAYER_SHADER,
 * BINK2_PLAYER_T, BINK2_PLAYER_DUMP, BINK2_PLAYER_TEST_SEEK.
 */

#ifdef BINK2_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "tools/player/bink2_renderer.h"
#include "tools/player/bink2_sdl_player_core.h"
#include "tools/player/bink2_vulkan_renderer.h"

static const Bink2RendererBackend vulkan_backend = {
    "vulkan",
    SDL_WINDOW_VULKAN,
    bink2_vulkan_prepare,
    bink2_vulkan_renderer_create,
    bink2_vulkan_renderer_upload,
    bink2_vulkan_renderer_upload_motion,
    bink2_vulkan_renderer_draw,
    bink2_vulkan_renderer_readback,
    bink2_vulkan_renderer_destroy
};

int main(int argc, char **argv)
{
    return bink2_player_run(argc, argv, &vulkan_backend);
}
