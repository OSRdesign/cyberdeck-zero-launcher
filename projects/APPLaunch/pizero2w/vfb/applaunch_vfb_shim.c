/*
 * LD_PRELOAD shim: gives a legacy fullscreen (framebuffer) app a virtual 320x170 framebuffer.
 *
 * Intercepts open() of /dev/fb*, the fb ioctls and mmap() of that descriptor, and serves them
 * from a shared-memory file (see applaunch_vfb.h). The launcher scales that picture into its
 * window. Active only when APPLAUNCH_VFB names an existing file; otherwise every call passes
 * straight through to libc.
 *
 * Build: see build.sh
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "applaunch_vfb.h"

#define MAX_VFDS 16

static int g_vfds[MAX_VFDS];
static int g_vfd_count;
static applaunch_vfb_header_t *g_header;
static off_t g_data_offset; /* where the pixels start in the file (header_data_offset) */

static int (*real_open)(const char *, int, ...);
static int (*real_open64)(const char *, int, ...);
static int (*real_openat)(int, const char *, int, ...);
static int (*real_close)(int);
static void *(*real_mmap)(void *, size_t, int, int, int, off_t);
static void *(*real_mmap64)(void *, size_t, int, int, int, off_t);

static void resolve(void)
{
    if (real_open) return;
    real_open = dlsym(RTLD_NEXT, "open");
    real_open64 = dlsym(RTLD_NEXT, "open64");
    real_openat = dlsym(RTLD_NEXT, "openat");
    real_close = dlsym(RTLD_NEXT, "close");
    real_mmap = dlsym(RTLD_NEXT, "mmap");
    real_mmap64 = dlsym(RTLD_NEXT, "mmap64");
}

static const char *vfb_path(void)
{
    const char *path = getenv("APPLAUNCH_VFB");
    return (path && path[0]) ? path : NULL;
}

static int is_fb_path(const char *path)
{
    return path && (strncmp(path, "/dev/fb", 7) == 0 || strncmp(path, "/dev/graphics/fb", 16) == 0);
}

static int is_virtual(int fd)
{
    for (int i = 0; i < g_vfd_count; i++)
        if (g_vfds[i] == fd) return 1;
    return 0;
}

static void forget(int fd)
{
    for (int i = 0; i < g_vfd_count; i++)
        if (g_vfds[i] == fd) {
            g_vfds[i] = g_vfds[--g_vfd_count];
            return;
        }
}

/* Offset of the pixel data in an initialised file: VFB2 = its header_bytes (64 KiB: page-aligned on 4, 16 and
 * 64 KiB-page kernels), VFB1 (a file an older launcher left) = 4096; -1 for anything else. */
static off_t header_data_offset(const applaunch_vfb_header_t *header)
{
    if (header->magic == APPLAUNCH_VFB_MAGIC_V1) return APPLAUNCH_VFB_HEADER_BYTES_V1;
    if (header->magic != APPLAUNCH_VFB_MAGIC || header->version < APPLAUNCH_VFB_VERSION) return -1;
    const uint32_t bytes = header->header_bytes;
    if (bytes < APPLAUNCH_VFB_HEADER_MAP_BYTES || bytes > (16u << 20) || bytes % 4096u != 0) return -1;
    return (off_t)bytes;
}

/* Opens the shared file for an app that asked for a framebuffer; -1 when not available. */
static int open_virtual(int flags)
{
    resolve();
    const char *path = vfb_path();
    if (!path || g_vfd_count >= MAX_VFDS) return -1;
    int fd = real_open(path, (flags & ~(O_CREAT | O_EXCL | O_TRUNC)) | O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    if (!g_header) {
        void *map = real_mmap(NULL, APPLAUNCH_VFB_HEADER_MAP_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        const off_t data_offset = map == MAP_FAILED ? -1 : header_data_offset(map);
        if (data_offset < 0) {
            if (map != MAP_FAILED) munmap(map, APPLAUNCH_VFB_HEADER_MAP_BYTES);
            real_close(fd);
            return -1;
        }
        g_header = map;
        g_data_offset = data_offset;
    }
    g_header->attached = 1;
    g_vfds[g_vfd_count++] = fd;
    return fd;
}

int open(const char *path, int flags, ...)
{
    resolve();
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    if (is_fb_path(path)) {
        int fd = open_virtual(flags);
        if (fd >= 0) return fd;
    }
    return real_open(path, flags, mode);
}

int open64(const char *path, int flags, ...)
{
    resolve();
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    if (is_fb_path(path)) {
        int fd = open_virtual(flags);
        if (fd >= 0) return fd;
    }
    return (real_open64 ? real_open64 : real_open)(path, flags, mode);
}

int openat(int dirfd, const char *path, int flags, ...)
{
    resolve();
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    if (path[0] == '/' && is_fb_path(path)) {
        int fd = open_virtual(flags);
        if (fd >= 0) return fd;
    }
    return real_openat(dirfd, path, flags, mode);
}

int close(int fd)
{
    resolve();
    if (is_virtual(fd)) forget(fd);
    return real_close(fd);
}

static void *do_mmap(void *(*fn)(void *, size_t, int, int, int, off_t), void *addr, size_t length, int prot,
                     int flags, int fd, off_t offset)
{
    if (fd >= 0 && is_virtual(fd)) {
        /* the framebuffer starts after the header (mmap needs a page-aligned offset: VFB2's 64 KiB is) */
        return fn(addr, length, prot, (flags & ~MAP_ANONYMOUS) | MAP_SHARED, fd,
                  offset + g_data_offset);
    }
    return fn(addr, length, prot, flags, fd, offset);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    resolve();
    return do_mmap(real_mmap, addr, length, prot, flags, fd, offset);
}

void *mmap64(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    resolve();
    return do_mmap(real_mmap64 ? real_mmap64 : real_mmap, addr, length, prot, flags, fd, offset);
}

static void fill_var(struct fb_var_screeninfo *var)
{
    memset(var, 0, sizeof(*var));
    var->xres = var->xres_virtual = g_header->width;
    var->yres = g_header->height;
    var->yres_virtual = g_header->yres_virtual;
    var->yoffset = g_header->yoffset;
    var->bits_per_pixel = g_header->bpp;
    var->red.offset = 16;
    var->red.length = 8;
    var->green.offset = 8;
    var->green.length = 8;
    var->blue.offset = 0;
    var->blue.length = 8;
    var->activate = FB_ACTIVATE_NOW;
    /* unknown physical size, on purpose even though VFB2 carries ppmm_x100: a cp0_lvgl app (the Store) takes its
     * density from the fbdev mm (cp0_lvgl_dpi_scaled.c), so real mm would change how it renders (report 023) */
    var->height = var->width = (uint32_t)-1;
    var->vmode = FB_VMODE_NONINTERLACED;
}

int ioctl(int fd, unsigned long request, ...)
{
    static int (*real_ioctl)(int, unsigned long, ...);
    if (!real_ioctl) real_ioctl = dlsym(RTLD_NEXT, "ioctl");

    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    if (!is_virtual(fd) || !g_header) return real_ioctl(fd, request, arg);

    switch (request) {
    case FBIOGET_VSCREENINFO:
        fill_var((struct fb_var_screeninfo *)arg);
        return 0;
    case FBIOPUT_VSCREENINFO: {
        struct fb_var_screeninfo *var = arg;
        /* accept page-flipping layouts that fit the file, otherwise keep ours */
        if (var->yres_virtual >= g_header->height && var->yres_virtual <= g_header->height * 2)
            g_header->yres_virtual = var->yres_virtual;
        if (var->yoffset + g_header->height <= g_header->yres_virtual) g_header->yoffset = var->yoffset;
        fill_var(var);
        return 0;
    }
    case FBIOPAN_DISPLAY: {
        struct fb_var_screeninfo *var = arg;
        if (var->yoffset + g_header->height <= g_header->yres_virtual) g_header->yoffset = var->yoffset;
        g_header->frame++;
        return 0;
    }
    case FBIOGET_FSCREENINFO: {
        struct fb_fix_screeninfo *fix = arg;
        memset(fix, 0, sizeof(*fix));
        snprintf(fix->id, sizeof(fix->id), "applaunch-vfb");
        fix->smem_len = g_header->stride * g_header->yres_virtual;
        fix->type = FB_TYPE_PACKED_PIXELS;
        fix->visual = FB_VISUAL_TRUECOLOR;
        fix->line_length = g_header->stride;
        fix->ypanstep = 1;
        return 0;
    }
    case FBIO_WAITFORVSYNC:
        usleep(8000);
        return 0;
    case FBIOBLANK:
    case FBIOGETCMAP:
    case FBIOPUTCMAP:
        return 0;
    default:
        errno = ENOTTY;
        return -1;
    }
}
