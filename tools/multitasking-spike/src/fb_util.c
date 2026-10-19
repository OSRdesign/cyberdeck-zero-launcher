/*
 * SPDX-License-Identifier: MIT
 *
 * Framebuffer access. See fb_util.h.
 */
#define _GNU_SOURCE
#include "fb_util.h"

#include "font5x7.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int fb_open(fbdev *fb, const char *path, const char *geom, int writable, char *err, size_t err_n)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;
    const int flags = (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC;
    if (geom) {
        if (sscanf(geom, "%dx%dx%d", &fb->w, &fb->h, &fb->bpp) != 3 || fb->w <= 0 || fb->h <= 0 ||
            (fb->bpp != 16 && fb->bpp != 32)) {
            snprintf(err, err_n, "bad geometry '%s' (WxHx16 or WxHx32)", geom);
            return -1;
        }
        fb->is_file = 1;
        fb->stride = fb->w * fb->bpp / 8;
        fb->fd = open(path, flags | (writable ? O_CREAT : 0), 0644);
        if (fb->fd < 0) {
            snprintf(err, err_n, "open %s: %s", path, strerror(errno));
            return -1;
        }
        fb->map_len = fb_vis_len(fb);
        struct stat st;
        if (fstat(fb->fd, &st) == 0 && (size_t)st.st_size < fb->map_len && writable &&
            ftruncate(fb->fd, (off_t)fb->map_len) != 0) {
            snprintf(err, err_n, "ftruncate %s: %s", path, strerror(errno));
            close(fb->fd);
            return -1;
        }
        if (fb->bpp == 16) {
            fb->r_off = 11, fb->r_len = 5, fb->g_off = 5, fb->g_len = 6, fb->b_off = 0, fb->b_len = 5;
        } else {
            fb->r_off = 16, fb->r_len = 8, fb->g_off = 8, fb->g_len = 8, fb->b_off = 0, fb->b_len = 8;
        }
        snprintf(fb->id, sizeof(fb->id), "file");
    } else {
        fb->fd = open(path, flags);
        if (fb->fd < 0) {
            snprintf(err, err_n, "open %s: %s (group video?)", path, strerror(errno));
            return -1;
        }
        struct fb_var_screeninfo v;
        struct fb_fix_screeninfo f;
        if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &v) != 0 || ioctl(fb->fd, FBIOGET_FSCREENINFO, &f) != 0) {
            snprintf(err, err_n, "%s is not a framebuffer (FBIOGET failed: %s); use --fb-geom for a file", path,
                     strerror(errno));
            close(fb->fd);
            return -1;
        }
        fb->w = (int)v.xres;
        fb->h = (int)v.yres;
        fb->bpp = (int)v.bits_per_pixel;
        fb->stride = (int)f.line_length;
        if (fb->bpp != 16 && fb->bpp != 32) {
            snprintf(err, err_n, "%d bpp is not supported (16 or 32)", fb->bpp);
            close(fb->fd);
            return -1;
        }
        fb->r_off = (int)v.red.offset, fb->r_len = (int)v.red.length;
        fb->g_off = (int)v.green.offset, fb->g_len = (int)v.green.length;
        fb->b_off = (int)v.blue.offset, fb->b_len = (int)v.blue.length;
        fb->t_off = (int)v.transp.offset, fb->t_len = (int)v.transp.length;
        fb->map_len = f.smem_len;
        fb->vis_off = (size_t)v.yoffset * (size_t)fb->stride + (size_t)v.xoffset * (size_t)fb->bpp / 8;
        if (fb->vis_off + fb_vis_len(fb) > fb->map_len) fb->vis_off = 0;
        snprintf(fb->id, sizeof(fb->id), "%.16s", f.id);
    }
    fb->map = mmap(NULL, fb->map_len, writable ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, fb->fd, 0);
    if (fb->map == MAP_FAILED) {
        snprintf(err, err_n, "mmap %s: %s", path, strerror(errno));
        close(fb->fd);
        fb->map = NULL;
        return -1;
    }
    return 0;
}

void fb_close(fbdev *fb)
{
    if (fb->map) munmap(fb->map, fb->map_len);
    if (fb->fd >= 0) close(fb->fd);
    fb->map = NULL;
    fb->fd = -1;
}

static uint32_t chan(int v, int off, int len)
{
    if (len <= 0) return 0;
    return ((uint32_t)(v & 0xff) >> (8 - (len > 8 ? 8 : len))) << off;
}

uint32_t fb_pack(const fbdev *fb, int r, int g, int b)
{
    uint32_t p = chan(r, fb->r_off, fb->r_len) | chan(g, fb->g_off, fb->g_len) | chan(b, fb->b_off, fb->b_len);
    if (fb->t_len > 0) p |= chan(0xff, fb->t_off, fb->t_len);
    return p;
}

void fb_put(const fbdev *fb, uint8_t *buf, int x, int y, uint32_t pix)
{
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    uint8_t *p = buf + (size_t)y * (size_t)fb->stride + (size_t)x * (size_t)(fb->bpp / 8);
    if (fb->bpp == 32) memcpy(p, &pix, 4);
    else {
        const uint16_t v = (uint16_t)pix;
        memcpy(p, &v, 2);
    }
}

void fb_fill(const fbdev *fb, uint8_t *buf, int x, int y, int w, int h, uint32_t pix)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb->w) w = fb->w - x;
    if (y + h > fb->h) h = fb->h - y;
    if (w <= 0 || h <= 0) return;
    const int bp = fb->bpp / 8;
    for (int yy = y; yy < y + h; ++yy) {
        uint8_t *row = buf + (size_t)yy * (size_t)fb->stride + (size_t)x * (size_t)bp;
        if (bp == 4) {
            for (int i = 0; i < w; ++i) memcpy(row + 4 * i, &pix, 4);
        } else {
            const uint16_t v = (uint16_t)pix;
            for (int i = 0; i < w; ++i) memcpy(row + 2 * i, &v, 2);
        }
    }
}

int fb_text(const fbdev *fb, uint8_t *buf, int x, int y, int scale, const char *s, uint32_t pix)
{
    const int x0 = x;
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c < FONT5X7_FIRST || c > FONT5X7_LAST) c = '?';
        const uint8_t *glyph = font5x7[c - FONT5X7_FIRST];
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (glyph[col] & (1u << row)) fb_fill(fb, buf, x + col * scale, y + row * scale, scale, scale, pix);
        x += 6 * scale;
    }
    return x - x0;
}

long fb_diff_pixels(const fbdev *fb, const uint8_t *a, const uint8_t *b)
{
    const int bp = fb->bpp / 8;
    long n = 0;
    for (int y = 0; y < fb->h; ++y) {
        const uint8_t *ra = a + (size_t)y * (size_t)fb->stride, *rb = b + (size_t)y * (size_t)fb->stride;
        if (memcmp(ra, rb, (size_t)fb->w * (size_t)bp) == 0) continue;
        for (int x = 0; x < fb->w; ++x)
            if (memcmp(ra + x * bp, rb + x * bp, (size_t)bp) != 0) ++n;
    }
    return n;
}
