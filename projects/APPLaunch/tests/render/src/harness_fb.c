/* SPDX-License-Identifier: MIT
 *
 * Render harness: fake framebuffer and touch device for the real display manager.
 *
 * cp0_lvgl_dpi_scaled.c is compiled unchanged. The Makefile renames its undefined references
 * open/ioctl/mmap to harness_fb_open/harness_fb_ioctl/harness_fb_mmap (objcopy --redefine-sym),
 * so that one object, and only it, talks to this file:
 *   - HARNESS_FB_PATH is a framebuffer of the configured geometry, kept in memory;
 *   - HARNESS_TOUCH_PATH is the read end of a non-blocking pipe carrying struct input_event,
 *     written by harness_touch_report();
 *   - every other path / descriptor goes to the real libc call.
 */
#define _GNU_SOURCE
#include "harness.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

static struct {
    int pw, ph, bpp, stride;
    int mm_w, mm_h; /* physical size the driver reports (var.width / var.height), 0 = unknown */
    uint8_t *mem;
    size_t size;
    int fb_fd;
    int touch_rd, touch_wr;
} hf = {.fb_fd = -1, .touch_rd = -1, .touch_wr = -1};

void harness_fb_configure(int pw, int ph, int bpp)
{
    hf.pw = pw;
    hf.ph = ph;
    hf.bpp = bpp;
    hf.stride = pw * (bpp / 8);
    hf.size = (size_t)hf.stride * (size_t)ph;
    free(hf.mem);
    hf.mem = calloc(1, hf.size);
    if (!hf.mem) {
        fprintf(stderr, "[harness] out of memory for a %dx%dx%d framebuffer\n", pw, ph, bpp);
        exit(2);
    }
}

void harness_fb_set_mm(int mm_w, int mm_h)
{
    hf.mm_w = mm_w > 0 ? mm_w : 0;
    hf.mm_h = mm_h > 0 ? mm_h : 0;
}

uint8_t *harness_fb_memory(size_t *size, int *stride)
{
    if (size) *size = hf.size;
    if (stride) *stride = hf.stride;
    return hf.mem;
}

int harness_fb_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (path && strcmp(path, HARNESS_FB_PATH) == 0) {
        if (hf.fb_fd < 0) hf.fb_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
        return hf.fb_fd;
    }
    if (path && strcmp(path, HARNESS_TOUCH_PATH) == 0) {
        if (hf.touch_rd < 0) {
            int p[2];
            if (pipe2(p, O_NONBLOCK | O_CLOEXEC) != 0) return -1;
            hf.touch_rd = p[0];
            hf.touch_wr = p[1];
        }
        return hf.touch_rd;
    }
    return open(path, flags, mode);
}

static void fill_var(struct fb_var_screeninfo *v)
{
    memset(v, 0, sizeof(*v));
    v->xres = v->xres_virtual = (uint32_t)hf.pw;
    v->yres = v->yres_virtual = (uint32_t)hf.ph;
    v->bits_per_pixel = (uint32_t)hf.bpp;
    v->width = (uint32_t)hf.mm_w;
    v->height = (uint32_t)hf.mm_h;
    if (hf.bpp == 16) {
        v->red.offset = 11, v->red.length = 5;
        v->green.offset = 5, v->green.length = 6;
        v->blue.offset = 0, v->blue.length = 5;
    } else {
        v->red.offset = 16, v->red.length = 8;
        v->green.offset = 8, v->green.length = 8;
        v->blue.offset = 0, v->blue.length = 8;
    }
}

static int touch_ioctl(unsigned long request, void *arg)
{
    if (_IOC_TYPE(request) != 'E') return -1;
    const unsigned nr = _IOC_NR(request);
    if (nr == 0x06) { /* EVIOCGNAME(len) */
        const size_t len = _IOC_SIZE(request);
        if (!arg || len == 0) return -1;
        snprintf((char *)arg, len, "%s", "render-harness touch");
        return (int)strlen((char *)arg);
    }
    if (nr >= 0x40 && nr < 0x40 + ABS_CNT) { /* EVIOCGABS(axis) */
        struct input_absinfo *ai = (struct input_absinfo *)arg;
        const unsigned axis = nr - 0x40;
        if (!ai) return -1;
        memset(ai, 0, sizeof(*ai));
        if (axis == ABS_MT_POSITION_X || axis == ABS_X) {
            ai->maximum = hf.pw - 1;
            return 0;
        }
        if (axis == ABS_MT_POSITION_Y || axis == ABS_Y) {
            ai->maximum = hf.ph - 1;
            return 0;
        }
        return -1;
    }
    return -1;
}

int harness_fb_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (fd >= 0 && fd == hf.fb_fd) {
        if (request == FBIOGET_VSCREENINFO) {
            fill_var((struct fb_var_screeninfo *)arg);
            return 0;
        }
        if (request == FBIOGET_FSCREENINFO) {
            struct fb_fix_screeninfo *f = (struct fb_fix_screeninfo *)arg;
            memset(f, 0, sizeof(*f));
            snprintf(f->id, sizeof(f->id), "harness");
            f->line_length = (uint32_t)hf.stride;
            f->smem_len = (uint32_t)hf.size;
            return 0;
        }
        return -1;
    }
    if (fd >= 0 && fd == hf.touch_rd) return touch_ioctl(request, arg);
    return ioctl(fd, request, arg);
}

void *harness_fb_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset)
{
    if (fd >= 0 && fd == hf.fb_fd) {
        if (!hf.mem || length > hf.size || offset != 0) return MAP_FAILED;
        return hf.mem;
    }
    return mmap(addr, length, prot, flags, fd, (off_t)offset);
}

static void emit(int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = (unsigned short)type;
    ev.code = (unsigned short)code;
    ev.value = value;
    if (hf.touch_wr >= 0 && write(hf.touch_wr, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
        fprintf(stderr, "[harness] touch pipe full\n");
}

void harness_touch_report(int pressed, int raw_x, int raw_y)
{
    if (hf.touch_wr < 0) {
        fprintf(stderr, "[harness] no touch device opened by the display manager\n");
        return;
    }
    if (pressed) {
        emit(EV_ABS, ABS_MT_TRACKING_ID, 1);
        emit(EV_ABS, ABS_MT_POSITION_X, raw_x);
        emit(EV_ABS, ABS_MT_POSITION_Y, raw_y);
        emit(EV_KEY, BTN_TOUCH, 1);
    } else {
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        emit(EV_KEY, BTN_TOUCH, 0);
    }
    emit(EV_SYN, SYN_REPORT, 0);
}
