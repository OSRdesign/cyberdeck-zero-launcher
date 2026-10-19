/*
 * SPDX-License-Identifier: MIT
 *
 * Framebuffer access for fbhandoff and fbhapp: a real fbdev node (FBIOGET_VSCREENINFO / FSCREENINFO, the visible
 * page from yoffset) or a plain file with a given geometry (host tests). 16 and 32 bpp, channel layout from the
 * driver's bitfields. Drawing goes into any buffer laid out like the visible page.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    uint8_t *map;
    size_t map_len;
    size_t vis_off; /* byte offset of the visible page in the mapping */
    int w, h, bpp, stride;
    int is_file;
    int r_off, r_len, g_off, g_len, b_off, b_len, t_off, t_len;
    char id[32];
} fbdev;

/* geom: "WxHxBPP" for a plain file (NULL for a device). writable: O_RDWR. 0 on success, else -1 with a message. */
int fb_open(fbdev *fb, const char *path, const char *geom, int writable, char *err, size_t err_n);
void fb_close(fbdev *fb);
static inline uint8_t *fb_vis(const fbdev *fb) { return fb->map + fb->vis_off; }
static inline size_t fb_vis_len(const fbdev *fb) { return (size_t)fb->h * (size_t)fb->stride; }

uint32_t fb_pack(const fbdev *fb, int r, int g, int b);
void fb_put(const fbdev *fb, uint8_t *buf, int x, int y, uint32_t pix);
void fb_fill(const fbdev *fb, uint8_t *buf, int x, int y, int w, int h, uint32_t pix);
/* 5x7 font, scale = pixel size of one font dot; returns the width drawn */
int fb_text(const fbdev *fb, uint8_t *buf, int x, int y, int scale, const char *s, uint32_t pix);
/* number of pixels that differ between two buffers laid out like the visible page */
long fb_diff_pixels(const fbdev *fb, const uint8_t *a, const uint8_t *b);
