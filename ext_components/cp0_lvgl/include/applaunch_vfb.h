/*
 * Virtual framebuffer shared between the launcher and external (stock CardputerZero) apps.
 *
 * External apps draw to a Linux framebuffer sized for the Cardputer's 320x170 screen. On the
 * Raspberry Pi port an LD_PRELOAD shim (libapplaunch_vfb.so) redirects the app's framebuffer
 * open/ioctl/mmap calls to a shared-memory file with this layout, and the launcher scales that
 * picture up into the compat window.
 *
 * File layout (VFB2, task 014 P5, report 023 section 2): a 64 KiB header area followed by the
 * pixel data (XRGB8888, little endian). 64 KiB is a multiple of the 4, 16 and 64 KiB page sizes,
 * so the shim's mmap of the pixels at +header_bytes is page-aligned on every kernel (VFB1's
 * 4096-byte header made that mmap fail with EINVAL on 16 KiB-page kernels: HackBerry, Pi 5).
 * The shim takes the data offset from header_bytes and still accepts a VFB1 file (magic 'VFB1',
 * data at +4096) left by an older launcher.
 *
 * Launcher <-> shim internal: apps only see the fbdev ioctls. The launcher and the shim ship
 * together (one bundle, no versioned .so name).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APPLAUNCH_VFB_MAGIC 0x32424656u      /* 'VFB2' */
#define APPLAUNCH_VFB_MAGIC_V1 0x31424656u   /* 'VFB1': 4096-byte header, no fields after 'attached' */
#define APPLAUNCH_VFB_VERSION 2u
#define APPLAUNCH_VFB_HEADER_BYTES 65536u    /* VFB2 data offset */
#define APPLAUNCH_VFB_HEADER_BYTES_V1 4096u  /* VFB1 data offset */
#define APPLAUNCH_VFB_HEADER_MAP_BYTES 4096u /* what the shim maps of the header (the struct fits) */
#define APPLAUNCH_VFB_DEFAULT_PATH "/dev/shm/applaunch-vfb"
#define APPLAUNCH_VFB_DEFAULT_WIDTH 320u
#define APPLAUNCH_VFB_DEFAULT_HEIGHT 170u

#define APPLAUNCH_VFB_CANVAS_NATIVE 0u /* canvas_mode: the app draws 320x170, the launcher scales it */

typedef struct {
    /* VFB1 fields, same offsets */
    uint32_t magic;
    uint32_t width;         /* visible size */
    uint32_t height;
    uint32_t bpp;           /* 32 */
    uint32_t stride;        /* bytes per row */
    uint32_t yres_virtual;  /* rows backing the buffer (>= height; apps may page-flip) */
    volatile uint32_t yoffset; /* first visible row (updated by FBIOPAN_DISPLAY) */
    volatile uint32_t frame;   /* incremented by the shim on every pan */
    volatile uint32_t attached; /* 1 once an app has opened the virtual framebuffer */
    /* VFB2 fields (written by the launcher before magic) */
    uint32_t version;       /* APPLAUNCH_VFB_VERSION */
    uint32_t header_bytes;  /* offset of the pixel data in the file (APPLAUNCH_VFB_HEADER_BYTES) */
    uint32_t host_w;        /* the panel, logical landscape px */
    uint32_t host_h;
    int32_t win_x;          /* the compat window on the panel, logical px */
    int32_t win_y;
    int32_t win_w;
    int32_t win_h;
    uint32_t scale;         /* compat window scale (screen px per canvas px) */
    uint32_t ppmm_x100;     /* panel density, px/mm x100 (the shim still reports mm = -1 on purpose) */
    uint32_t canvas_mode;   /* APPLAUNCH_VFB_CANVAS_NATIVE */
    volatile uint32_t kb_request; /* reserved (launcher-drawn keyboard over compat apps), 0 */
    volatile uint32_t kb_inset;   /* reserved, 0 */
    uint32_t gen;           /* screen.state gen when the file was created */
} applaunch_vfb_header_t;

#ifdef __cplusplus
static_assert(offsetof(applaunch_vfb_header_t, attached) == 32, "VFB1 field offsets");
static_assert(offsetof(applaunch_vfb_header_t, version) == 36, "VFB2 fields follow the VFB1 ones");
static_assert(sizeof(applaunch_vfb_header_t) <= APPLAUNCH_VFB_HEADER_MAP_BYTES, "header map");
#else
_Static_assert(offsetof(applaunch_vfb_header_t, attached) == 32, "VFB1 field offsets");
_Static_assert(offsetof(applaunch_vfb_header_t, version) == 36, "VFB2 fields follow the VFB1 ones");
_Static_assert(sizeof(applaunch_vfb_header_t) <= APPLAUNCH_VFB_HEADER_MAP_BYTES, "header map");
#endif

#ifdef __cplusplus
}
#endif
