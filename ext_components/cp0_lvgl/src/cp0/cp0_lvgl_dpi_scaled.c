/*
 * Raspberry Pi Zero 2W / Waveshare 2.8" DPI LCD (landscape 640x480) backend.
 *
 * Enabled at runtime with APPLAUNCH_DISPLAY=dpi-scaled. See cp0_display.h for
 * the two-display model (native + 320x170 compat window).
 *
 * Optional tuning (environment):
 *   LV_LINUX_FBDEV_DEVICE       framebuffer node          (default /dev/fb0)
 *   APPLAUNCH_ROTATE            0|90|180|270              (default 0 / 90 if portrait)
 *   APPLAUNCH_TOUCH_DEVICE      evdev node                (default: first "Goodix")
 *   APPLAUNCH_TOUCH_SWAP_XY     1 to swap raw touch axes
 *   APPLAUNCH_TOUCH_INVERT_X    1 to mirror touch X (after the optional swap)
 *   APPLAUNCH_TOUCH_INVERT_Y    1 to mirror touch Y (after the optional swap)
 */

#include "lvgl/lvgl.h"
#include "cp0_display.h"
#include "cp0_lvgl.h"
#include "input_keys.h"
#include "keyboard_input.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define UI_W 320
#define UI_H 170

typedef struct {
    uint8_t *fb;
    size_t fb_size;
    int fd;
    int pw, ph;     /* physical framebuffer size */
    int stride;     /* bytes per physical row */
    int bpp;        /* bytes per pixel: 2 or 4 */
    int rot;        /* 0, 90, 180, 270 */
    int lw, lh;     /* landscape (post-rotation) size */
    int scale;      /* compat integer upscale */
    int ox, oy;     /* compat window origin in the landscape view */
    int ww, wh;     /* compat window size in the landscape view */
    lv_display_t *native;
    lv_display_t *compat;
    volatile cp0_display_mode_t mode;
    volatile bool blackout;
    volatile bool external;
    bool ready;
} dpi_ctx_t;

static dpi_ctx_t g;
static bool fill_window_black_pending;
static struct { volatile bool active; int x, y, w, h; } overlay; /* compat coordinates */
static void fill_window_black(void);

static int env_int(const char *name, int def)
{
    const char *v = getenv(name);
    return (v && v[0]) ? atoi(v) : def;
}

/* landscape (X,Y) -> physical (px,py) */
static inline void land_to_phys(int X, int Y, int *px, int *py)
{
    switch (g.rot) {
    case 90:  *px = g.pw - 1 - Y; *py = X; break;
    case 180: *px = g.pw - 1 - X; *py = g.ph - 1 - Y; break;
    case 270: *px = Y; *py = g.lw - 1 - X; break;
    default:  *px = X; *py = Y; break;
    }
}

/* physical (px,py) -> landscape (X,Y) */
static inline void phys_to_land(int px, int py, int *X, int *Y)
{
    switch (g.rot) {
    case 90:  *X = py; *Y = g.pw - 1 - px; break;
    case 180: *X = g.pw - 1 - px; *Y = g.ph - 1 - py; break;
    case 270: *X = g.lw - 1 - py; *Y = px; break;
    default:  *X = px; *Y = py; break;
    }
}

static inline void put_pixel(int X, int Y, const uint8_t *src)
{
    int px, py;
    land_to_phys(X, Y, &px, &py);
    memcpy(g.fb + (size_t)py * g.stride + (size_t)px * g.bpp, src, g.bpp);
}

static inline bool in_window(int X, int Y)
{
    return X >= g.ox && X < g.ox + g.ww && Y >= g.oy && Y < g.oy + g.wh;
}

/* ------------------------------------------------------------- flushing */

/* Compat display: integer upscale into the window, only while it is visible. */
static void flush_compat(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (g.mode == CP0_DISPLAY_MODE_COMPAT && !g.blackout && g.external && overlay.active) {
        /* the app's picture owns the window; only the overlay rectangle is drawn from LVGL */
        const int aw = lv_area_get_width(area);
        const int x1 = area->x1 > overlay.x ? area->x1 : overlay.x;
        const int y1 = area->y1 > overlay.y ? area->y1 : overlay.y;
        const int x2 = area->x2 < overlay.x + overlay.w - 1 ? area->x2 : overlay.x + overlay.w - 1;
        const int y2 = area->y2 < overlay.y + overlay.h - 1 ? area->y2 : overlay.y + overlay.h - 1;
        for (int y = y1; y <= y2; y++)
            for (int x = x1; x <= x2; x++) {
                const uint8_t *src = px_map + ((size_t)(y - area->y1) * aw + (size_t)(x - area->x1)) * g.bpp;
                const int bx = g.ox + x * g.scale, by = g.oy + y * g.scale;
                for (int dy = 0; dy < g.scale; dy++)
                    for (int dx = 0; dx < g.scale; dx++) put_pixel(bx + dx, by + dy, src);
            }
    } else if (g.mode == CP0_DISPLAY_MODE_COMPAT && !g.blackout && !g.external) {
        const int w = lv_area_get_width(area);
        const int h = lv_area_get_height(area);
        for (int y = 0; y < h; y++) {
            const uint8_t *src = px_map + (size_t)y * w * g.bpp;
            for (int x = 0; x < w; x++, src += g.bpp) {
                const int bx = g.ox + (area->x1 + x) * g.scale;
                const int by = g.oy + (area->y1 + y) * g.scale;
                for (int dy = 0; dy < g.scale; dy++)
                    for (int dx = 0; dx < g.scale; dx++)
                        put_pixel(bx + dx, by + dy, src);
            }
        }
    }
    lv_display_flush_ready(disp);
}

/* Native display: 1:1 copy, leaving the compat window alone while it is shown. */
static void flush_native(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int w = lv_area_get_width(area);
    const int h = lv_area_get_height(area);
    const bool compat = g.mode == CP0_DISPLAY_MODE_COMPAT && !g.blackout;

    for (int y = 0; y < h; y++) {
        const int Y = area->y1 + y;
        const uint8_t *src = px_map + (size_t)y * w * g.bpp;
        int x0 = 0, x1 = w; /* span [x0,x1) within this row */
        if (compat && Y >= g.oy && Y < g.oy + g.wh) {
            /* skip the part of the row that lies inside the window */
            const int wx0 = g.ox - area->x1, wx1 = g.ox + g.ww - area->x1;
            if (wx0 <= 0 && wx1 >= w) continue;           /* fully covered */
            if (wx0 <= 0) x0 = wx1 > w ? w : wx1;         /* left part covered */
            else if (wx1 >= w) x1 = wx0 < 0 ? 0 : wx0;    /* right part covered */
            /* a window strictly inside the row is not a layout we use */
        }
        if (g.rot == 0) {
            if (x1 > x0)
                memcpy(g.fb + (size_t)Y * g.stride + (size_t)(area->x1 + x0) * g.bpp,
                       src + (size_t)x0 * g.bpp, (size_t)(x1 - x0) * g.bpp);
        } else {
            for (int x = x0; x < x1; x++)
                put_pixel(area->x1 + x, Y, src + (size_t)x * g.bpp);
        }
    }
    lv_display_flush_ready(disp);
}

static uint32_t tick_get_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* ---------------------------------------------------------------- touch */

typedef struct {
    int fd;
    int raw_x, raw_y;
    int min_x, max_x, min_y, max_y;
    bool pressed;
    bool swap, inv_x, inv_y;
    int X, Y;           /* last mapped position in the landscape view */
} touch_ctx_t;

static touch_ctx_t tc;
static cp0_display_touch_activity_cb_t activity_cb;
static volatile int swallow_mode; /* 0 none, 1 until cleared, 2 until the finger is lifted */
static bool was_pressed;

static int find_touch_device(char *out, size_t n)
{
    const char *forced = getenv("APPLAUNCH_TOUCH_DEVICE");
    if (forced && forced[0]) {
        snprintf(out, n, "%s", forced);
        return 0;
    }
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    int found = -1;
    while (found < 0 && (e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[300], name[128] = {0};
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 && strstr(name, "Goodix")) {
            snprintf(out, n, "%s", path);
            found = 0;
        }
        close(fd);
    }
    closedir(d);
    return found;
}

/* Drain pending evdev events and refresh the mapped landscape position. */
static void touch_update(void)
{
    struct input_event ev;
    ssize_t r;
    while ((r = read(tc.fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) tc.raw_x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) tc.raw_y = ev.value;
            else if (ev.code == ABS_MT_TRACKING_ID) tc.pressed = ev.value >= 0;
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            tc.pressed = ev.value != 0;
        }
    }

    const int spanx = tc.max_x - tc.min_x, spany = tc.max_y - tc.min_y;
    double nx = spanx > 0 ? (double)(tc.raw_x - tc.min_x) / spanx : 0;
    double ny = spany > 0 ? (double)(tc.raw_y - tc.min_y) / spany : 0;
    if (tc.swap) { double t = nx; nx = ny; ny = t; } /* normalise each axis first */
    if (tc.inv_x) nx = 1.0 - nx;
    if (tc.inv_y) ny = 1.0 - ny;
    int px = (int)(nx * (g.pw - 1)), py = (int)(ny * (g.ph - 1));
    if (px < 0) px = 0;
    if (px >= g.pw) px = g.pw - 1;
    if (py < 0) py = 0;
    if (py >= g.ph) py = g.ph - 1;
    phys_to_land(px, py, &tc.X, &tc.Y);

    /* One notification per new touch (the second pointer device sees no edge). */
    const bool edge = tc.pressed && !was_pressed;
    was_pressed = tc.pressed;
    if (tc.pressed && activity_cb) activity_cb(edge ? 1 : 0);
    if (swallow_mode == 2 && !tc.pressed) swallow_mode = 0;
}

/* Pointer for the native display (everything except the compat window while it is shown). */
static void touch_read_native(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    touch_update();
    data->point.x = tc.X;
    data->point.y = tc.Y;
    const bool hidden = (g.mode == CP0_DISPLAY_MODE_COMPAT && in_window(tc.X, tc.Y)) || swallow_mode != 0;
    data->state = (tc.pressed && !hidden) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ---- list touch mode: gestures -> key events ---- */

static cp0_display_touch_mode_t touch_mode = CP0_DISPLAY_TOUCH_POINTER;
static int list_center_y = 96;
static int list_row_h = 21;

typedef struct {
    bool down;
    int x0, y0;         /* press position (compat coordinates) */
    int last_y;
    int acc;            /* vertical travel not yet turned into a row step */
    bool stepped;       /* at least one row step was emitted: this is a drag, not a tap */
    int max_travel;
    uint32_t t0;
} list_gesture_t;

static list_gesture_t lg;
static int list_drag_inverted;

void cp0_display_set_list_drag_inverted(int inverted) { list_drag_inverted = inverted != 0; }

static cp0_display_key_sink_t key_sink;
static uint32_t swipe_tap_key = KEY_ENTER;

void cp0_display_set_swipe_tap_key(unsigned short code) { swipe_tap_key = code ? code : KEY_ENTER; }

void cp0_display_set_key_sink(cp0_display_key_sink_t sink) { key_sink = sink; }

static void tap_key(uint32_t code)
{
    if (key_sink) {
        key_sink((unsigned short)code, 1);
        key_sink((unsigned short)code, 0);
        return;
    }
    cp0_keyboard_inject(code, KBD_KEY_PRESSED, 0);
    cp0_keyboard_inject(code, KBD_KEY_RELEASED, 0);
}

/* ---- swipe touch mode: direction -> arrow key, tap -> Enter ---- */

typedef struct {
    bool down;
    int x0, y0, x, y;
    uint32_t t0;
} swipe_gesture_t;

static swipe_gesture_t sw;

static void swipe_gesture(void)
{
    const bool pressed = tc.pressed && g.mode == CP0_DISPLAY_MODE_COMPAT && in_window(tc.X, tc.Y);
    const int lx = (tc.X - g.ox) / g.scale;
    const int ly = (tc.Y - g.oy) / g.scale;

    if (pressed && !sw.down) {
        sw = (swipe_gesture_t){.down = true, .x0 = lx, .y0 = ly, .x = lx, .y = ly, .t0 = lv_tick_get()};
        return;
    }
    if (pressed && sw.down) {
        sw.x = lx;
        sw.y = ly;
        return;
    }
    if (!pressed && sw.down) {
        const int dx = sw.x - sw.x0, dy = sw.y - sw.y0;
        const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        const int travel = ax > ay ? ax : ay;
        if (travel < 10 && lv_tick_elaps(sw.t0) < 600) tap_key(swipe_tap_key);
        else if (travel >= 24 && ax > ay) tap_key(dx > 0 ? KEY_RIGHT : KEY_LEFT);
        else if (travel >= 24) tap_key(dy > 0 ? KEY_DOWN : KEY_UP);
        sw.down = false;
    }
}

static void list_gesture(void)
{
    const bool pressed = tc.pressed && g.mode == CP0_DISPLAY_MODE_COMPAT && in_window(tc.X, tc.Y);
    const int lx = (tc.X - g.ox) / g.scale;
    const int ly = (tc.Y - g.oy) / g.scale;

    if (pressed && !lg.down) {
        lg = (list_gesture_t){.down = true, .x0 = lx, .y0 = ly, .last_y = ly, .t0 = lv_tick_get()};
        return;
    }
    if (pressed && lg.down) {
        lg.acc += ly - lg.last_y;
        lg.last_y = ly;
        const int dx = lx - lg.x0, dy = ly - lg.y0;
        const int travel = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (travel > lg.max_travel) lg.max_travel = travel;
        /* the list follows the finger: dragging up brings later rows up, i.e. selects "down" */
        while (lg.acc <= -list_row_h) {
            tap_key(list_drag_inverted ? KEY_UP : KEY_DOWN);
            lg.acc += list_row_h;
            lg.stepped = true;
        }
        while (lg.acc >= list_row_h) {
            tap_key(list_drag_inverted ? KEY_DOWN : KEY_UP);
            lg.acc -= list_row_h;
            lg.stepped = true;
        }
        return;
    }
    if (!pressed && lg.down) {
        const int dx = lx - lg.x0;
        const uint32_t dt = lv_tick_elaps(lg.t0);
        if (!lg.stepped && lg.max_travel < 8 && dt < 700) {
            /* tap: highlighted row opens, any other row becomes the highlighted one */
            int rows = (lg.y0 - list_center_y + (lg.y0 >= list_center_y ? list_row_h / 2 : -list_row_h / 2)) / list_row_h;
            if (rows == 0) {
                tap_key(KEY_ENTER);
            } else {
                for (int i = 0; i < (rows < 0 ? -rows : rows); i++) tap_key(rows < 0 ? KEY_UP : KEY_DOWN);
            }
        } else if (!lg.stepped && lg.x0 < 24 && dx > 40) {
            tap_key(KEY_ESC); /* swipe in from the left edge: back */
        }
        lg.down = false;
    }
}

void cp0_display_set_touch_mode(cp0_display_touch_mode_t mode, int center_y, int row_h)
{
    touch_mode = mode;
    if (center_y > 0) list_center_y = center_y;
    if (row_h > 4) list_row_h = row_h;
    lg.down = false;
    sw.down = false;
}

/* Pointer for the compat display: touches inside the window, in 320x170 coordinates. */
static void touch_read_compat(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    touch_update();
    static int last_x, last_y;
    if (swallow_mode != 0) {
        lg.down = false;
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (touch_mode == CP0_DISPLAY_TOUCH_SWIPE) {
        swipe_gesture();
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED; /* LVGL never sees the touch: keys only */
        return;
    }
    if (touch_mode == CP0_DISPLAY_TOUCH_LIST) {
        list_gesture();
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED; /* LVGL never sees the touch: keys only */
        return;
    }
    const bool inside = g.mode == CP0_DISPLAY_MODE_COMPAT && in_window(tc.X, tc.Y);
    if (inside) {
        last_x = (tc.X - g.ox) / g.scale;
        last_y = (tc.Y - g.oy) / g.scale;
    }
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = (tc.pressed && inside) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void init_touch(void)
{
    char path[300];
    if (find_touch_device(path, sizeof(path)) != 0) {
        fprintf(stderr, "[dpi] no touch device found (set APPLAUNCH_TOUCH_DEVICE)\n");
        return;
    }
    tc.fd = open(path, O_RDONLY | O_NONBLOCK);
    if (tc.fd < 0) {
        fprintf(stderr, "[dpi] open %s: %s\n", path, strerror(errno));
        return;
    }
    struct input_absinfo ai;
    if (ioctl(tc.fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) < 0 && ioctl(tc.fd, EVIOCGABS(ABS_X), &ai) < 0)
        ai.minimum = 0, ai.maximum = g.pw - 1;
    tc.min_x = ai.minimum; tc.max_x = ai.maximum;
    if (ioctl(tc.fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) < 0 && ioctl(tc.fd, EVIOCGABS(ABS_Y), &ai) < 0)
        ai.minimum = 0, ai.maximum = g.ph - 1;
    tc.min_y = ai.minimum; tc.max_y = ai.maximum;
    tc.swap = env_int("APPLAUNCH_TOUCH_SWAP_XY", 0);
    tc.inv_x = env_int("APPLAUNCH_TOUCH_INVERT_X", 0);
    tc.inv_y = env_int("APPLAUNCH_TOUCH_INVERT_Y", 0);

    lv_indev_t *native = lv_indev_create();
    lv_indev_set_type(native, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(native, touch_read_native);
    lv_indev_set_display(native, g.native);

    lv_indev_t *compat = lv_indev_create();
    lv_indev_set_type(compat, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(compat, touch_read_compat);
    lv_indev_set_display(compat, g.compat);

    printf("[dpi] touch %s raw x[%d..%d] y[%d..%d]\n", path, tc.min_x, tc.max_x, tc.min_y, tc.max_y);
}

/* -------------------------------------------------------------- public API */

int cp0_display_available(void) { return g.ready; }
lv_display_t *cp0_display_native(void) { return g.native; }
lv_display_t *cp0_display_compat(void) { return g.compat; }
cp0_display_mode_t cp0_display_get_mode(void) { return g.mode; }

void cp0_display_set_touch_activity_cb(cp0_display_touch_activity_cb_t cb) { activity_cb = cb; }

void cp0_display_set_overlay_rect(int x, int y, int w, int h)
{
    overlay.x = x;
    overlay.y = y;
    overlay.w = w;
    overlay.h = h;
    overlay.active = w > 0 && h > 0;
}

void cp0_display_set_external(int on)
{
    if (!g.ready || (bool)on == g.external) return;
    g.external = on != 0;
    if (g.external) {
        fill_window_black_pending = true;
    } else if (g.mode == CP0_DISPLAY_MODE_COMPAT) {
        lv_obj_invalidate(lv_display_get_screen_active(g.compat)); /* repaint what the app covered */
    }
}

/* Scale one XRGB8888 frame into the compat window (integer upscale, same placement as the
 * compat display). */
void cp0_display_external_blit(const void *xrgb8888, int width, int height, int stride_bytes)
{
    if (!g.ready || !g.external || !xrgb8888 || width <= 0 || height <= 0) return;
    if (width > UI_W) width = UI_W;
    if (height > UI_H) height = UI_H;
    if (fill_window_black_pending) {
        fill_window_black();
        fill_window_black_pending = false;
    }
    const int s = g.scale;
    if (g.rot == 0 && g.bpp == 4) {
        /* fast path: scale a row horizontally once, then copy it down `scale` times */
        uint32_t *row = malloc((size_t)width * s * 4);
        if (!row) return;
        for (int y = 0; y < height; y++) {
            const uint32_t *src = (const uint32_t *)((const uint8_t *)xrgb8888 + (size_t)y * stride_bytes);
            for (int x = 0; x < width; x++)
                for (int dx = 0; dx < s; dx++) row[x * s + dx] = src[x];
            const bool in_overlay_rows = overlay.active && y >= overlay.y && y < overlay.y + overlay.h;
            for (int dy = 0; dy < s; dy++) {
                uint8_t *dst = g.fb + (size_t)(g.oy + y * s + dy) * g.stride + (size_t)g.ox * 4;
                if (!in_overlay_rows) {
                    memcpy(dst, row, (size_t)width * s * 4);
                    continue;
                }
                /* leave [overlay.x, overlay.x + overlay.w) alone (it is the ribbon) */
                const int left = overlay.x * s;
                const int right = (overlay.x + overlay.w) * s;
                const int total = width * s;
                if (left > 0) memcpy(dst, row, (size_t)(left < total ? left : total) * 4);
                if (right < total) memcpy(dst + (size_t)right * 4, row + right, (size_t)(total - right) * 4);
            }
        }
        free(row);
        return;
    }
    for (int y = 0; y < height; y++) {
        const uint8_t *src = (const uint8_t *)xrgb8888 + (size_t)y * stride_bytes;
        for (int x = 0; x < width; x++, src += 4) {
            uint8_t px[4];
            if (g.bpp == 4) {
                memcpy(px, src, 4);
            } else {
                const uint16_t v = (uint16_t)(((src[2] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[0] >> 3));
                memcpy(px, &v, 2);
            }
            for (int dy = 0; dy < s; dy++)
                for (int dx = 0; dx < s; dx++) put_pixel(g.ox + x * s + dx, g.oy + y * s + dy, px);
        }
    }
}
void cp0_display_set_touch_swallow(int mode) { swallow_mode = mode; }

void cp0_display_set_blackout(int on)
{
    if (!g.ready || (bool)on == g.blackout) return;
    g.blackout = on != 0;
    if (!g.blackout) {
        /* repaint everything that was hidden behind the blackout */
        if (g.mode == CP0_DISPLAY_MODE_COMPAT) {
            lv_obj_invalidate(lv_display_get_screen_active(g.compat));
            lv_obj_invalidate(lv_display_get_layer_top(g.compat));
        }
        lv_obj_invalidate(lv_display_get_screen_active(g.native));
    }
}

void cp0_display_compat_window(int *x, int *y, int *w, int *h)
{
    if (x) *x = g.ox;
    if (y) *y = g.oy;
    if (w) *w = g.ww;
    if (h) *h = g.wh;
}

static void fill_window_black(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0};
    for (int Y = g.oy; Y < g.oy + g.wh; Y++)
        for (int X = g.ox; X < g.ox + g.ww; X++)
            put_pixel(X, Y, black);
}

void cp0_display_set_mode(cp0_display_mode_t mode)
{
    if (!g.ready || mode == g.mode) return;
    g.mode = mode;
    if (mode == CP0_DISPLAY_MODE_COMPAT) {
        fill_window_black();
        lv_display_set_default(g.compat);
        lv_obj_invalidate(lv_display_get_screen_active(g.compat));
        /* the native chrome (toolbar) is redrawn by its owner when it loads */
    } else {
        lv_display_set_default(g.native);
        lv_obj_invalidate(lv_display_get_screen_active(g.native));
    }
}

lv_display_t *cp0_dpi_scaled_create(void)
{
    const char *dev = getenv("LV_LINUX_FBDEV_DEVICE");
    if (!dev || !dev[0]) dev = "/dev/fb0";

    g.fd = open(dev, O_RDWR);
    if (g.fd < 0) {
        fprintf(stderr, "[dpi] open %s: %s\n", dev, strerror(errno));
        return NULL;
    }
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(g.fd, FBIOGET_VSCREENINFO, &vi) < 0 || ioctl(g.fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        fprintf(stderr, "[dpi] fb ioctl failed: %s\n", strerror(errno));
        return NULL;
    }
    if (vi.bits_per_pixel != 16 && vi.bits_per_pixel != 32) {
        fprintf(stderr, "[dpi] unsupported fb depth %u bpp\n", vi.bits_per_pixel);
        return NULL;
    }
    g.pw = vi.xres;
    g.ph = vi.yres;
    g.bpp = vi.bits_per_pixel / 8;
    g.stride = fi.line_length;
    g.fb_size = (size_t)fi.line_length * vi.yres_virtual;
    g.fb = mmap(NULL, g.fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, g.fd, 0);
    if (g.fb == MAP_FAILED) {
        fprintf(stderr, "[dpi] mmap: %s\n", strerror(errno));
        return NULL;
    }

    /* Default: portrait panel shown rotated to landscape. */
    g.rot = env_int("APPLAUNCH_ROTATE", g.pw < g.ph ? 90 : 0);
    if (g.rot != 0 && g.rot != 90 && g.rot != 180 && g.rot != 270) g.rot = 0;
    const bool swap_dims = g.rot == 90 || g.rot == 270;
    g.lw = swap_dims ? g.ph : g.pw;
    g.lh = swap_dims ? g.pw : g.ph;

    const int sx = g.lw / UI_W, sy = g.lh / UI_H;
    g.scale = sx < sy ? sx : sy;
    if (g.scale < 1) g.scale = 1;
    g.ww = UI_W * g.scale;
    g.wh = UI_H * g.scale;
    g.ox = (g.lw - g.ww) / 2; /* centred horizontally, top aligned */
    g.oy = 0;

    memset(g.fb, 0, g.fb_size);
    lv_tick_set_cb(tick_get_cb); /* fbdev/drm drivers normally do this */

    const lv_color_format_t cf = g.bpp == 4 ? LV_COLOR_FORMAT_XRGB8888 : LV_COLOR_FORMAT_RGB565;

    /* compat: 320x170, drawn 2x into the window */
    g.compat = lv_display_create(UI_W, UI_H);
    lv_display_set_color_format(g.compat, cf);
    {
        const size_t sz = (size_t)UI_W * UI_H * g.bpp;
        uint8_t *buf = malloc(sz);
        if (!buf) return NULL;
        lv_display_set_buffers(g.compat, buf, NULL, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(g.compat, flush_compat);
    }

    /* native: full landscape panel */
    g.native = lv_display_create(g.lw, g.lh);
    lv_display_set_color_format(g.native, cf);
    {
        const size_t lines = 96;
        const size_t sz = (size_t)g.lw * lines * g.bpp;
        uint8_t *buf = malloc(sz);
        if (!buf) return NULL;
        lv_display_set_buffers(g.native, buf, NULL, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(g.native, flush_native);
    }

    /* Legacy code builds its screens at startup: make compat the default until
     * a native screen is shown (native_ui switches the mode explicitly). */
    g.mode = CP0_DISPLAY_MODE_COMPAT;
    lv_display_set_default(g.compat);
    g.ready = true;

    printf("[dpi] %s %dx%d %dbpp rot=%d native=%dx%d compat window %dx%d@(%d,%d) scale=%d\n", dev, g.pw,
           g.ph, g.bpp * 8, g.rot, g.lw, g.lh, g.ww, g.wh, g.ox, g.oy, g.scale);

    init_touch();
    return g.compat;
}
