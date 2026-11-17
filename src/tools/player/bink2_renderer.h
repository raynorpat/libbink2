#ifndef BINK2_RENDERER_H
#define BINK2_RENDERER_H

/* Renderer interface shared by the SDL players. Each backend defines
 * Bink2Renderer itself, so exactly one is linked per player. This header
 * deliberately includes no SDL, GL or Metal headers. */

#include "bink2.h"

typedef struct Bink2Renderer Bink2Renderer;

typedef struct {
    int interp;   /* 0 presentation, 1 interpolation */
    int have_next;
    float t;
    unsigned long blocks_w;
    unsigned long blocks_h;
    int slot_cur; /* 0 or 1: the pair's current frame */
    int capture;  /* render for readback: do not present */
} Bink2RenderParams;

typedef struct {
    const char *name;          /* "opengl", "metal" */
    unsigned int window_flags; /* SDL_WINDOW_OPENGL or SDL_WINDOW_METAL */
    /* Called before the window exists, for context attributes. */
    int (*prepare)(void);
    Bink2Renderer *(*create)(void *window, unsigned long w, unsigned long h);
    int (*upload)(Bink2Renderer *r, bink2 object, int slot);
    int (*upload_motion)(Bink2Renderer *r, bink2 object,
                         unsigned long *blocks_w, unsigned long *blocks_h);
    int (*draw)(Bink2Renderer *r, const Bink2RenderParams *params);
    /* Read the last drawn frame back as packed RGB24 at the plane size,
     * top-down. Returns 0 on success. */
    int (*readback)(Bink2Renderer *r, unsigned char *rgb, unsigned long w,
                    unsigned long h);
    void (*destroy)(Bink2Renderer *r);
} Bink2RendererBackend;

#endif
