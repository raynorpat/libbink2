#ifndef BINK2_VULKAN_RENDERER_H
#define BINK2_VULKAN_RENDERER_H

#include "tools/player/bink2_renderer.h"

int bink2_vulkan_prepare(void);
Bink2Renderer *bink2_vulkan_renderer_create(void *window, unsigned long w,
                                          unsigned long h);
int bink2_vulkan_renderer_upload(Bink2Renderer *r, bink2 object, int slot);
int bink2_vulkan_renderer_upload_motion(Bink2Renderer *r, bink2 object,
                                       unsigned long *blocks_w,
                                       unsigned long *blocks_h);
int bink2_vulkan_renderer_draw(Bink2Renderer *r, const Bink2RenderParams *params);
int bink2_vulkan_renderer_readback(Bink2Renderer *r, unsigned char *rgb,
                                  unsigned long w, unsigned long h);
void bink2_vulkan_renderer_destroy(Bink2Renderer *r);

#endif
