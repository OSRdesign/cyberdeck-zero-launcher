/*
 * Virtual framebuffer shared between the launcher and external (stock CardputerZero) apps.
 *
 * External apps draw to a Linux framebuffer sized for the Cardputer's 320x170 screen. On the
 * Raspberry Pi port an LD_PRELOAD shim (libapplaunch_vfb.so) redirects the app's framebuffer
 * open/ioctl/mmap calls to a shared-memory file with this layout, and the launcher scales that
 * picture up into the compat window.
 *
 * File layout: one 4096-byte header page followed by the pixel data (XRGB8888, little endian).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APPLAUNCH_VFB_MAGIC 0x31424656u /* 'VFB1' */
#define APPLAUNCH_VFB_HEADER_BYTES 4096u
#define APPLAUNCH_VFB_DEFAULT_PATH "/dev/shm/applaunch-vfb"
#define APPLAUNCH_VFB_DEFAULT_WIDTH 320u
#define APPLAUNCH_VFB_DEFAULT_HEIGHT 170u

typedef struct {
    uint32_t magic;
    uint32_t width;         /* visible size */
    uint32_t height;
    uint32_t bpp;           /* 32 */
    uint32_t stride;        /* bytes per row */
    uint32_t yres_virtual;  /* rows backing the buffer (>= height; apps may page-flip) */
    volatile uint32_t yoffset; /* first visible row (updated by FBIOPAN_DISPLAY) */
    volatile uint32_t frame;   /* incremented by the shim on every pan */
    volatile uint32_t attached; /* 1 once an app has opened the virtual framebuffer */
} applaunch_vfb_header_t;

#ifdef __cplusplus
}
#endif
