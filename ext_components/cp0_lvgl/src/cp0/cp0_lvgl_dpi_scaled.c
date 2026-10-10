/*
 * Raspberry Pi Zero 2W / Waveshare 2.8" DPI LCD (landscape 640x480) backend.
 *
 * Enabled at runtime with APPLAUNCH_DISPLAY=dpi-scaled. See cp0_display.h for
 * the two-display model (native + 320x170 compat window).
 *
 * Optional tuning (environment, normally from the board profile /etc/applaunch/board.conf; with
 * none of them set the deck behaviour is unchanged). Read once, when the display is created; the
 * resolved values are logged on one "[display-profile]" line (stderr / journal).
 *   APPLAUNCH_BOARD             label, only logged
 *   APPLAUNCH_FB                framebuffer node          (default LV_LINUX_FBDEV_DEVICE, then /dev/fb0)
 *   APPLAUNCH_ROTATE            0|90|180|270 clockwise pre-rotation of the landscape canvas into
 *                               the buffer                (default 0 / 90 if the buffer is portrait)
 *   APPLAUNCH_LOGICAL           WxH landscape canvas      (default: the buffer size through the
 *                               rotation; a size that does not match it is logged and ignored)
 *   APPLAUNCH_COMPAT_SCALE      1|2 scale of the 320x170 window (default: largest that fits, 2 on
 *                               the deck; a scale that does not fit is lowered)
 *   APPLAUNCH_TOUCH_ORIENT      legacy (default) | buffer: the touch axes follow the framebuffer;
 *                               raw values are normalised by the device's ABS range and turned
 *                               back through APPLAUNCH_ROTATE (SWAP/INVERT below are ignored)
 *   APPLAUNCH_TOUCH_DEV         evdev node, or "auto" = pick by capability (default: auto in
 *                               buffer mode, else the legacy discovery below)
 *   APPLAUNCH_TOUCH_DEVICE      evdev node                (legacy: default first "Goodix")
 *   APPLAUNCH_TOUCH_SWAP_XY     1 to swap raw touch axes            (legacy mode)
 *   APPLAUNCH_TOUCH_INVERT_X    1 to mirror touch X (after the optional swap)
 *   APPLAUNCH_TOUCH_INVERT_Y    1 to mirror touch Y (after the optional swap)
 *   APPLAUNCH_BACKLIGHT         see cp0_backlight_profile.h
 * Depth, channel layout and line length come from the framebuffer ioctls (16 bpp RGB565 layouts
 * and 32 bpp XRGB8888). All drawing goes through cp0_fb_output.c.
 */

#include "lvgl/lvgl.h"
#include "cp0_backlight_profile.h"
#include "cp0_display.h"
#include "cp0_fb_output.h"
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
    cp0_fb_out_t out;     /* rotating output stage over the mapping */
    uint8_t *scratch;     /* work area of the scaled blits */
    size_t scratch_size;
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

/* physical (px,py) -> landscape (X,Y) (legacy touch mapping) */
static inline void phys_to_land(int px, int py, int *X, int *Y)
{
    switch (g.rot) {
    case 90:  *X = py; *Y = g.pw - 1 - px; break;
    case 180: *X = g.pw - 1 - px; *Y = g.ph - 1 - py; break;
    case 270: *X = g.lw - 1 - py; *Y = px; break;
    default:  *X = px; *Y = py; break;
    }
}

static inline bool in_window(int X, int Y)
{
    return X >= g.ox && X < g.ox + g.ww && Y >= g.oy && Y < g.oy + g.wh;
}

/* ------------------------------------------------------------- flushing */

/* Compat display: integer upscale into the window, only while it is visible. */
static void flush_compat(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int aw = lv_area_get_width(area);
    const int ah = lv_area_get_height(area);
    const int src_stride = aw * g.bpp;
    if (g.mode == CP0_DISPLAY_MODE_COMPAT && !g.blackout && g.external && overlay.active) {
        /* the app's picture owns the window; only the overlay rectangle is drawn from LVGL */
        const int x1 = area->x1 > overlay.x ? area->x1 : overlay.x;
        const int y1 = area->y1 > overlay.y ? area->y1 : overlay.y;
        const int x2 = area->x2 < overlay.x + overlay.w - 1 ? area->x2 : overlay.x + overlay.w - 1;
        const int y2 = area->y2 < overlay.y + overlay.h - 1 ? area->y2 : overlay.y + overlay.h - 1;
        if (x2 >= x1 && y2 >= y1) {
            uint8_t *src = px_map + (size_t)(y1 - area->y1) * src_stride + (size_t)(x1 - area->x1) * g.bpp;
            cp0_fbo_to_buffer_format(&g.out, src, x2 - x1 + 1, y2 - y1 + 1, src_stride);
            cp0_fbo_blit_scaled(&g.out, g.ox + x1 * g.scale, g.oy + y1 * g.scale, x2 - x1 + 1, y2 - y1 + 1, src,
                                src_stride, g.scale, g.scratch, g.scratch_size);
        }
    } else if (g.mode == CP0_DISPLAY_MODE_COMPAT && !g.blackout && !g.external) {
        cp0_fbo_to_buffer_format(&g.out, px_map, aw, ah, src_stride);
        cp0_fbo_blit_scaled(&g.out, g.ox + area->x1 * g.scale, g.oy + area->y1 * g.scale, aw, ah, px_map,
                            src_stride, g.scale, g.scratch, g.scratch_size);
    }
    lv_display_flush_ready(disp);
}

/* Native display: 1:1 copy, leaving the compat window alone while it is shown. */
static void flush_native(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int w = lv_area_get_width(area);
    const int h = lv_area_get_height(area);
    const bool compat = g.mode == CP0_DISPLAY_MODE_COMPAT && !g.blackout;

    cp0_fbo_to_buffer_format(&g.out, px_map, w, h, w * g.bpp);
    if (compat)
        cp0_fbo_blit_except(&g.out, area->x1, area->y1, w, h, px_map, w * g.bpp, g.ox, g.oy, g.ww, g.wh);
    else
        cp0_fbo_blit(&g.out, area->x1, area->y1, w, h, px_map, w * g.bpp);
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
    bool buffer_mode;   /* APPLAUNCH_TOUCH_ORIENT=buffer: axes follow the framebuffer */
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

#define DPI_LONG_BITS (sizeof(unsigned long) * 8)
#define DPI_NLONGS(n) (((n) + DPI_LONG_BITS - 1) / DPI_LONG_BITS)

static bool has_bit(const unsigned long *bits, int bit)
{
    return (bits[bit / DPI_LONG_BITS] >> (bit % DPI_LONG_BITS)) & 1u;
}

static bool contains_nocase(const char *s, const char *needle)
{
    const size_t n = strlen(needle);
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i] && (s[i] | 0x20) == (needle[i] | 0x20)) i++;
        if (i == n) return true;
    }
    return false;
}

/* How much an evdev node looks like a touch screen: -1 = not one. Needs X/Y (multi-touch or
 * single) plus INPUT_PROP_DIRECT or BTN_TOUCH; virtual devices (our own uinput keyboard) and
 * touchpads (INPUT_PROP_POINTER without DIRECT) are skipped. */
static int touch_score(int fd, char *name, size_t name_size)
{
    unsigned long ev[DPI_NLONGS(EV_CNT)] = {0}, abs_bits[DPI_NLONGS(ABS_CNT)] = {0};
    unsigned long key[DPI_NLONGS(KEY_CNT)] = {0}, prop[DPI_NLONGS(INPUT_PROP_CNT)] = {0};
    struct input_id id;
    memset(name, 0, name_size);
    if (ioctl(fd, EVIOCGNAME(name_size - 1), name) < 0) name[0] = '\0';
    if (ioctl(fd, EVIOCGID, &id) == 0 && id.bustype == BUS_VIRTUAL) return -1;
    if (contains_nocase(name, "applaunch")) return -1;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0 || !has_bit(ev, EV_ABS)) return -1;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0) return -1;
    if (has_bit(ev, EV_KEY)) (void)ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key);
    (void)ioctl(fd, EVIOCGPROP(sizeof(prop)), prop);
    const bool mt = has_bit(abs_bits, ABS_MT_POSITION_X) && has_bit(abs_bits, ABS_MT_POSITION_Y);
    const bool st = has_bit(abs_bits, ABS_X) && has_bit(abs_bits, ABS_Y);
    const bool direct = has_bit(prop, INPUT_PROP_DIRECT);
    const bool btn_touch = has_bit(key, BTN_TOUCH);
    if (!mt && !st) return -1;
    if (!direct && !btn_touch) return -1;
    if (!direct && has_bit(prop, INPUT_PROP_POINTER)) return -1;
    return (direct ? 4 : 0) + (mt ? 2 : 0) + (btn_touch ? 1 : 0);
}

/* Best-scoring /dev/input/eventN (lowest N on a tie). */
static int find_touch_by_capability(char *out, size_t n, char *name, size_t name_size)
{
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    int best_score = -1, best_num = -1;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char *end = NULL;
        const long num = strtol(e->d_name + 5, &end, 10);
        if (end == e->d_name + 5 || *end != '\0' || num < 0 || num > 1023) continue;
        char path[64], dev_name[128] = {0};
        snprintf(path, sizeof(path), "/dev/input/event%ld", num);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        const int score = touch_score(fd, dev_name, sizeof(dev_name));
        close(fd);
        if (score > best_score || (score == best_score && score >= 0 && num < best_num)) {
            best_score = score;
            best_num = (int)num;
            snprintf(out, n, "%s", path);
            snprintf(name, name_size, "%s", dev_name);
        }
    }
    closedir(d);
    return best_score >= 0 ? 0 : -1;
}

/* Which evdev node to read, and how it was chosen (for the profile log line). */
static int resolve_touch_device(char *out, size_t n, char *name, size_t name_size, const char **how)
{
    name[0] = '\0';
    const char *dev = getenv("APPLAUNCH_TOUCH_DEV");
    const bool want_auto = (dev && strcmp(dev, "auto") == 0) || tc.buffer_mode;
    if (dev && dev[0] && strcmp(dev, "auto") != 0) {
        snprintf(out, n, "%s", dev);
        *how = "APPLAUNCH_TOUCH_DEV";
        return 0;
    }
    if (!want_auto) {
        *how = "legacy discovery";
        return find_touch_device(out, n); /* today's: APPLAUNCH_TOUCH_DEVICE, else first "Goodix" */
    }
    const char *forced = getenv("APPLAUNCH_TOUCH_DEVICE");
    if (forced && forced[0]) {
        snprintf(out, n, "%s", forced);
        *how = "APPLAUNCH_TOUCH_DEVICE";
        return 0;
    }
    *how = "capability";
    return find_touch_by_capability(out, n, name, name_size);
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

    if (tc.buffer_mode) {
        cp0_fbo_touch_to_logical(&g.out, tc.raw_x, tc.raw_y, tc.min_x, tc.max_x, tc.min_y, tc.max_y, &tc.X,
                                 &tc.Y);
    } else {
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
    }

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

static char touch_desc[768] = "none";

static void init_touch(void)
{
    char path[300], name[128];
    const char *how = "";
    if (resolve_touch_device(path, sizeof(path), name, sizeof(name), &how) != 0) {
        fprintf(stderr, "[dpi] no touch device found (set APPLAUNCH_TOUCH_DEVICE)\n");
        snprintf(touch_desc, sizeof(touch_desc), "none (%s)", how);
        return;
    }
    tc.fd = open(path, O_RDONLY | O_NONBLOCK);
    if (tc.fd < 0) {
        fprintf(stderr, "[dpi] open %s: %s\n", path, strerror(errno));
        snprintf(touch_desc, sizeof(touch_desc), "%s via %s: open failed", path, how);
        return;
    }
    if (tc.buffer_mode && !name[0]) {
        memset(name, 0, sizeof(name));
        if (ioctl(tc.fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) name[0] = '\0';
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
    if (tc.buffer_mode)
        snprintf(touch_desc, sizeof(touch_desc), "%s \"%s\" via %s, buffer orientation, raw x[%d..%d] y[%d..%d]",
                 path, name, how, tc.min_x, tc.max_x, tc.min_y, tc.max_y);
    else
        snprintf(touch_desc, sizeof(touch_desc), "%s via %s, legacy swap=%d invert_x=%d invert_y=%d", path, how,
                 tc.swap, tc.inv_x, tc.inv_y);
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
    /* rotated and/or 16 bpp: convert, scale and rotate through the output stage, keeping the
     * overlay (ribbon) rectangle out of it like the fast path */
    const bool ov = overlay.active;
    cp0_fbo_blit_xrgb_scaled(&g.out, g.ox, g.oy, width, height, (const uint8_t *)xrgb8888, stride_bytes, s,
                             ov ? overlay.x : 0, ov ? overlay.y : 0, ov ? overlay.w : 0, ov ? overlay.h : 0,
                             g.scratch, g.scratch_size);
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
    cp0_fbo_fill_black(&g.out, g.ox, g.oy, g.ww, g.wh);
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

static uint8_t fb_channel(uint32_t v)
{
    return (uint8_t)(v > 255u ? 255u : v);
}

lv_display_t *cp0_dpi_scaled_create(void)
{
    const char *dev = getenv("APPLAUNCH_FB");
    if (!dev || !dev[0]) dev = getenv("LV_LINUX_FBDEV_DEVICE");
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
    if (g.fb == MAP_FAILED && fi.smem_len > 0 && fi.smem_len < g.fb_size) {
        /* a virtual size beyond the driver's memory: map the memory that exists */
        g.fb_size = fi.smem_len;
        g.fb = mmap(NULL, g.fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, g.fd, 0);
    }
    if (g.fb == MAP_FAILED) {
        fprintf(stderr, "[dpi] mmap: %s\n", strerror(errno));
        return NULL;
    }

    /* Default: portrait panel shown rotated to landscape. */
    g.rot = env_int("APPLAUNCH_ROTATE", g.pw < g.ph ? 90 : 0);
    if (g.rot != 0 && g.rot != 90 && g.rot != 180 && g.rot != 270) g.rot = 0;
    {
        const char *rot_env = getenv("APPLAUNCH_ROTATE");
        int parsed;
        if (rot_env && rot_env[0] && cp0_fbo_parse_rotation(rot_env, &parsed) != 0)
            fprintf(stderr, "[display-profile] APPLAUNCH_ROTATE=%s is not 0|90|180|270: using %d\n", rot_env, g.rot);
    }

    /* Output stage: checks the buffer against the rotated logical canvas (never writes outside the
     * mapping) and takes the 16 bpp channel layout from the driver. */
    int req_w = 0, req_h = 0;
    const char *logical = getenv("APPLAUNCH_LOGICAL");
    if (logical && logical[0] && cp0_fbo_parse_size(logical, &req_w, &req_h) != 0) {
        fprintf(stderr, "[display-profile] APPLAUNCH_LOGICAL=%s is not WxH: using the framebuffer size\n", logical);
        req_w = req_h = 0;
    }
    const cp0_fb_layout_t layout = {{fb_channel(vi.red.offset), fb_channel(vi.red.length)},
                                    {fb_channel(vi.green.offset), fb_channel(vi.green.length)},
                                    {fb_channel(vi.blue.offset), fb_channel(vi.blue.length)}};
    const int fix = cp0_fbo_setup(&g.out, g.fb, g.fb_size, g.stride, g.bpp, g.pw, g.ph, g.rot, req_w, req_h, &layout);
    if (fix < 0) {
        fprintf(stderr, "[display-profile] %s: unusable geometry %dx%d, line length %d, %zu bytes mapped\n", dev,
                g.pw, g.ph, g.stride, g.fb_size);
        return NULL;
    }
    if (fix & (CP0_FBO_FIX_WIDTH | CP0_FBO_FIX_HEIGHT))
        fprintf(stderr, "[display-profile] %s: %dx%d does not fit line length %d / %zu bytes: using %dx%d\n", dev,
                g.pw, g.ph, g.stride, g.fb_size, g.out.pw, g.out.ph);
    if (fix & CP0_FBO_FIX_LOGICAL)
        fprintf(stderr,
                "[display-profile] APPLAUNCH_LOGICAL=%dx%d does not match %s %dx%d at rotate %d: using %dx%d\n",
                req_w, req_h, dev, g.out.pw, g.out.ph, g.rot, g.out.lw, g.out.lh);
    if (fix & CP0_FBO_FIX_LAYOUT)
        fprintf(stderr, "[display-profile] %s: channel layout r%u/%u g%u/%u b%u/%u unusable: RGB565 assumed\n", dev,
                vi.red.offset, vi.red.length, vi.green.offset, vi.green.length, vi.blue.offset, vi.blue.length);
    if (g.bpp == 4 && (vi.red.offset != 16 || vi.green.offset != 8 || vi.blue.offset != 0))
        fprintf(stderr, "[display-profile] %s reports offsets r%u g%u b%u: drawing XRGB8888 as before\n", dev,
                vi.red.offset, vi.green.offset, vi.blue.offset);
    g.pw = g.out.pw;
    g.ph = g.out.ph;
    g.lw = g.out.lw;
    g.lh = g.out.lh;

    const int sx = g.lw / UI_W, sy = g.lh / UI_H;
    g.scale = sx < sy ? sx : sy;
    if (g.scale < 1) g.scale = 1;
    {
        const char *cs = getenv("APPLAUNCH_COMPAT_SCALE");
        const int want = !cs ? 0 : strcmp(cs, "1") == 0 ? 1 : strcmp(cs, "2") == 0 ? 2 : -1;
        if (cs && cs[0] && want < 0)
            fprintf(stderr, "[display-profile] APPLAUNCH_COMPAT_SCALE=%s is not 1|2: using %d\n", cs, g.scale);
        else if (want > g.scale)
            fprintf(stderr, "[display-profile] APPLAUNCH_COMPAT_SCALE=%d does not fit %dx%d: using %d\n", want, g.lw,
                    g.lh, g.scale);
        else if (want > 0)
            g.scale = want;
    }
    g.ww = UI_W * g.scale;
    g.wh = UI_H * g.scale;
    /* centred horizontally; at the top on the deck, centred above the toolbar on small panels */
    cp0_fbo_compat_origin(g.lw, g.lh, g.ww, g.wh, &g.ox, &g.oy);

    /* 16 source rows per pass of the scaled blits */
    g.scratch_size = (size_t)UI_W * g.scale * g.scale * 16 * g.bpp;
    g.scratch = malloc(g.scratch_size);
    if (!g.scratch) return NULL;

    {
        const char *orient = getenv("APPLAUNCH_TOUCH_ORIENT");
        tc.buffer_mode = orient && strcmp(orient, "buffer") == 0;
        if (orient && orient[0] && !tc.buffer_mode && strcmp(orient, "legacy") != 0)
            fprintf(stderr, "[display-profile] APPLAUNCH_TOUCH_ORIENT=%s is not buffer|legacy: legacy\n", orient);
    }

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

    const char *board = getenv("APPLAUNCH_BOARD");
    char backlight[300] = "default";
    if (cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_GPIO_ONOFF)
        snprintf(backlight, sizeof(backlight), "gpio:%s (on/off)", cp0_backlight_profile_dir());
    fprintf(stderr,
            "[display-profile] board=%s fb=%s %dx%d %dbpp line=%d rgb=%u/%u,%u/%u,%u/%u%s rotate=%d logical=%dx%d "
            "compat_scale=%d window=%dx%d@(%d,%d) touch=%s backlight=%s\n",
            board && board[0] ? board : "-", dev, g.pw, g.ph, g.bpp * 8, g.stride, vi.red.offset, vi.red.length,
            vi.green.offset, vi.green.length, vi.blue.offset, vi.blue.length,
            g.bpp == 2 && !g.out.rgb565_plain ? " (converted)" : "", g.rot, g.lw, g.lh, g.scale, g.ww, g.wh, g.ox,
            g.oy, touch_desc, backlight);
    return g.compat;
}
