#ifndef BINK2_SDL_PLAYER_CORE_H
#define BINK2_SDL_PLAYER_CORE_H

#include "tools/player/bink2_renderer.h"

/* Run one SDL player with the given renderer backend. Each player's main is
 * a backend table and this call. */
int bink2_player_run(int argc, char **argv,
                    const Bink2RendererBackend *backend);

#endif
