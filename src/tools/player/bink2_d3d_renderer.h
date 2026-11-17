#ifndef BINK2_D3D_RENDERER_H
#define BINK2_D3D_RENDERER_H

/* Renderer interface shared by the D3D9 and D3D11 Windows players. Each
 * implementation defines Bink2D3DRenderer itself, so exactly one renderer
 * is linked into a player. */

#include "bink2.h"

typedef struct Bink2D3DRenderer Bink2D3DRenderer;

typedef struct {
    int interp;   /* 0 presentation, 1 interpolation */
    int have_next;
    float t;
    unsigned long blocks_w;
    unsigned long blocks_h;
    int slot_cur; /* 0 or 1: the pair's current frame */
} Bink2D3DDraw;

/* hwnd is an HWND passed as void * so this header stays free of windows.h. */
Bink2D3DRenderer *bink2_d3d9_renderer_create(void *hwnd, unsigned long w,
                                           unsigned long h);
Bink2D3DRenderer *bink2_d3d11_renderer_create(void *hwnd, unsigned long w,
                                            unsigned long h);

/* Upload the current frame's planes into slot (0 or 1). */
int bink2_d3d_renderer_upload(Bink2D3DRenderer *r, bink2 object, int slot);
/* Upload the current frame's motion field (+128 per byte) and report its
 * size in blocks, which the draw call needs to address it. Both are set to
 * 0 when there is no field. */
int bink2_d3d_renderer_upload_motion(Bink2D3DRenderer *r, bink2 object,
                                    unsigned long *blocks_w,
                                    unsigned long *blocks_h);
int bink2_d3d_renderer_draw(Bink2D3DRenderer *r, const Bink2D3DDraw *draw);
void bink2_d3d_renderer_destroy(Bink2D3DRenderer *r);

#endif
