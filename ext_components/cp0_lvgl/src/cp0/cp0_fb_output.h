/*
 * Output stage of the dpi-scaled display manager: everything that writes pixels into the
 * mapped framebuffer goes through here, so the native display, the scaled compat window, the
 * external (vfb) picture, the overlay rectangle and the black fills all honour the same
 * clockwise pre-rotation, pixel depth and bounds.
 *
 * Coordinates:
 *   logical  - the landscape canvas the UI draws on (lw x lh).
 *   buffer   - the framebuffer as mapped (pw x ph). With rotation 90/270 its size is the logical
 *              size swapped (logical 480x320 -> buffer 320x480).
 * rot is the clockwise angle the logical canvas is turned by on its way into the buffer:
 *   90 : logical (X,Y) -> buffer (pw-1-Y, X)
 *   180: logical (X,Y) -> buffer (pw-1-X, ph-1-Y)
 *   270: logical (X,Y) -> buffer (Y, ph-1-X)
 *
 * Pixel formats: the source of cp0_fbo_blit* is already in the framebuffer depth (LVGL renders
 * RGB565 for a 16 bpp buffer and XRGB8888 for a 32 bpp one). 32 bpp is always written as
 * XRGB8888 (as before); a 16 bpp buffer uses the channel offsets/lengths read from the driver.
 *
 * Pure C, no LVGL: unit-tested on the PC by tests/test_fb_output.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t off, len;
} cp0_fb_channel_t;

typedef struct {
    cp0_fb_channel_t r, g, b;
} cp0_fb_layout_t;

typedef struct {
    uint8_t *base;   /* mapped framebuffer */
    size_t size;     /* bytes mapped */
    int stride;      /* bytes per buffer row (line_length) */
    int bpp;         /* bytes per pixel: 2 or 4 */
    int pw, ph;      /* buffer size in pixels */
    int rot;         /* clockwise pre-rotation: 0, 90, 180, 270 */
    int lw, lh;      /* logical canvas */
    cp0_fb_layout_t layout; /* 16 bpp channel layout (32 bpp: XRGB8888) */
    int rgb565_plain;       /* 16 bpp layout equals LVGL's RGB565: plain copy, no conversion */
} cp0_fb_out_t;

/* Bits returned by cp0_fbo_setup() for what it had to correct. */
#define CP0_FBO_FIX_ROTATE 0x01   /* rot was not 0/90/180/270: 0 used */
#define CP0_FBO_FIX_WIDTH 0x02    /* pw did not fit in stride: clamped */
#define CP0_FBO_FIX_HEIGHT 0x04   /* ph rows did not fit in size: clamped */
#define CP0_FBO_FIX_LOGICAL 0x08  /* requested logical size != rotated buffer size: buffer size used */
#define CP0_FBO_FIX_LAYOUT 0x10   /* 16 bpp layout unusable: standard RGB565 assumed */

/* Fill *o. lw/lh <= 0 means "derive from the buffer". The logical size always ends up equal to
 * the buffer size seen through the rotation, so no write can leave the mapping. layout may be
 * NULL (standard RGB565). Returns the CP0_FBO_FIX_* bits, or -1 when the buffer is unusable. */
int cp0_fbo_setup(cp0_fb_out_t *o, uint8_t *base, size_t size, int stride, int bpp, int pw, int ph,
                  int rot, int lw, int lh, const cp0_fb_layout_t *layout);

/* "480x320" -> 0 and *w, *h; -1 when malformed or not positive. */
int cp0_fbo_parse_size(const char *s, int *w, int *h);
/* "0" | "90" | "180" | "270" -> 0 and *rot; -1 otherwise. */
int cp0_fbo_parse_rotation(const char *s, int *rot);

/* Where the compat window sits is decided by the layout service (cp0_ui_metrics.h), not here. */

void cp0_fbo_logical_to_buffer(const cp0_fb_out_t *o, int X, int Y, int *px, int *py);
void cp0_fbo_buffer_to_logical(const cp0_fb_out_t *o, int px, int py, int *X, int *Y);

/* Touch whose device axes follow the buffer (APPLAUNCH_TOUCH_ORIENT=buffer): raw values in
 * [min..max] per axis are spread over the buffer pixels (centre of each device step), clamped,
 * then turned back into logical coordinates through the inverse of rot. */
void cp0_fbo_touch_to_logical(const cp0_fb_out_t *o, int raw_x, int raw_y, int min_x, int max_x,
                              int min_y, int max_y, int *X, int *Y);

/* 8-bit channels -> a 16 bpp pixel in the buffer's layout. */
static inline uint32_t cp0_fbo_pack(const cp0_fb_layout_t *l, uint32_t r8, uint32_t g8, uint32_t b8)
{
    return ((r8 >> (8 - l->r.len)) << l->r.off) | ((g8 >> (8 - l->g.len)) << l->g.off) |
           ((b8 >> (8 - l->b.len)) << l->b.off);
}

/* Copy a w x h block (buffer depth, src_stride bytes per row; 0 repeats the first row) whose
 * top-left is logical (X, Y). Clipped to the logical canvas. */
void cp0_fbo_blit(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *src, int src_stride);

/* Same, leaving the logical rectangle (wx, wy, ww, wh) untouched (ww or wh <= 0: no hole). */
void cp0_fbo_blit_except(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *src,
                         int src_stride, int wx, int wy, int ww, int wh);

/* Integer upscale: source pixel (x, y) covers logical [X + x*scale, +scale) x [Y + y*scale, +scale).
 * scratch is a work area (any size >= scale*scale*bpp bytes; bigger = fewer passes). */
void cp0_fbo_blit_scaled(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *src,
                         int src_stride, int scale, uint8_t *scratch, size_t scratch_size);

/* XRGB8888 source (an external app's picture) converted to the buffer depth, upscaled like
 * cp0_fbo_blit_scaled, skipping the source rectangle (ex, ey, ew, eh) (ew or eh <= 0: none). */
void cp0_fbo_blit_xrgb_scaled(const cp0_fb_out_t *o, int X, int Y, int w, int h, const uint8_t *xrgb,
                              int src_stride, int scale, int ex, int ey, int ew, int eh,
                              uint8_t *scratch, size_t scratch_size);

/* Black logical rectangle (clipped). */
void cp0_fbo_fill_black(const cp0_fb_out_t *o, int X, int Y, int w, int h);

/* In place: LVGL RGB565 pixels -> the buffer's 16 bpp layout. No-op when rgb565_plain or 32 bpp. */
void cp0_fbo_to_buffer_format(const cp0_fb_out_t *o, uint8_t *px, int w, int h, int stride);

#ifdef __cplusplus
}
#endif
