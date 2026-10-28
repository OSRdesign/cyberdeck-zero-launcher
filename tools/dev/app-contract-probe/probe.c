/*
 * SPDX-License-Identifier: MIT
 *
 * app-contract-probe: a small test app for the launcher's app contract v1 (docs/APP-CONTRACT.md, task 014 P5-3).
 *
 *   app-contract-probe --report [--no-fb] [--tag NAME]
 *       prints every contract fact it read as key=value lines (environment, screen.state, input.state, the
 *       framebuffer it would draw to, and the derived mode / class / density / canvas / window / keyboard rule),
 *       exits 0. Used by the render harness (tests/render/run.sh contract) and by hand on a board.
 *   app-contract-probe [--tag NAME] [--fb DEV] [--timeout S]
 *       draws a test pattern on the framebuffer (compat: the virtual 320x170 framebuffer through the launcher's
 *       LD_PRELOAD shim; fullscreen: the panel's framebuffer with its rotation and depth, through cp0_fb_output):
 *       the canvas border, a 1 mm grid (10 mm lines brighter), a 9 mm orange square and the report summary. The
 *       state files are re-read once a second (the picture follows a new gen). Esc or Q exits; so does the timeout
 *       (default 300 s, 0 = none). The report is also written to /tmp/app-contract-probe-<tag>.txt.
 *   app-contract-probe --ppm FILE [--ppm-bpp 16|32] [--tag NAME]
 *       draws the same pattern into memory and writes it as a PPM picture (host check): the canvas as the panel's
 *       memory would hold it (fullscreen: turned by the panel's rotation; 16 bpp = standard RGB565).
 *
 * Uses only the contract SDK (cp0_app_contract.{h,c}, cp0_ui_metrics.{h,c}) and the framebuffer helper
 * (cp0_fb_output.{h,c}). No LVGL. Build: build.sh.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "cp0_app_contract.h"
#include "cp0_fb_output.h"
#include "cp0_ui_metrics.h"
#include "font5x7.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PROBE_REPORT_VERSION 1
#define MAX_KEYBOARDS 16
#define REPORT_MAX 8192

/* ------------------------------------------------------------------ what the probe found */

typedef struct {
    int open_ok;
    int err;
    char dev[CP0_APP_PATH_MAX];
    int xres, yres, xres_virtual, yres_virtual, bpp, stride;
    unsigned smem;
} fb_info_t;

typedef struct {
    const char *tag;
    /* raw */
    int env_ok;
    cp0_app_env_t env;
    char screen_path[CP0_APP_PATH_MAX + 32];
    int screen_ok;
    cp0_app_screen_t screen;
    char input_path[CP0_APP_PATH_MAX + 32];
    int input_ok;
    cp0_app_input_t input;
    fb_info_t fb;
    /* derived */
    const char *source;  /* contract | state | fbdev | none */
    cp0_app_mode_t mode;
    int screen_w, screen_h, rot, ppmm;
    cp0_ui_class_t cls;
    int canvas_w, canvas_h, canvas_scale, canvas_ppmm;
    int win_x, win_y, win_w, win_h;
    cp0_app_screen_t tokens;
    const char *tokens_from; /* file | compute | none */
    int square_px, square_screen_px;
    int osk; /* 1 / 0, -1 = unknown (no input.state) */
} probe_t;

static void fb_probe(const char *dev, fb_info_t *fb)
{
    memset(fb, 0, sizeof(*fb));
    snprintf(fb->dev, sizeof(fb->dev), "%s", dev ? dev : "");
    if (!dev || !dev[0]) {
        fb->err = ENOENT;
        return;
    }
    const int fd = open(dev, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fb->err = errno;
        return;
    }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0) {
        fb->err = errno;
        close(fd);
        return;
    }
    close(fd);
    fb->open_ok = 1;
    fb->xres = (int)var.xres, fb->yres = (int)var.yres;
    fb->xres_virtual = (int)var.xres_virtual, fb->yres_virtual = (int)var.yres_virtual;
    fb->bpp = (int)var.bits_per_pixel, fb->stride = (int)fix.line_length, fb->smem = fix.smem_len;
}

/* The device the probe draws to: compat = the app's fbdev (the shim turns it into the virtual framebuffer);
 * fullscreen / no contract = the panel's framebuffer as the contract names it, else what the launcher used. */
static const char *fb_device(const probe_t *p, const char *forced)
{
    if (forced && forced[0]) return forced;
    const char *app_fb = getenv("APPLAUNCH_FB");
    if (p->mode == CP0_APP_MODE_COMPAT) return app_fb && app_fb[0] ? app_fb : "/dev/fb0";
    if (p->env.screen_fb[0]) return p->env.screen_fb;
    if (p->screen_ok && p->screen.fb[0]) return p->screen.fb;
    return app_fb && app_fb[0] ? app_fb : "/dev/fb0";
}

static int round_div(int a, int b)
{
    return b > 0 ? (a + b / 2) / b : 0;
}

/* Everything the contract says, then the degrade rules (APP-CONTRACT.md "Failure modes"). */
static void probe_read(probe_t *p, const char *forced_fb, int use_fb)
{
    p->env_ok = cp0_app_env_read(&p->env) == 0;
    if (cp0_app_state_path(CP0_APP_SCREEN_FILE, p->screen_path, sizeof(p->screen_path)) != 0) p->screen_path[0] = 0;
    if (cp0_app_state_path(CP0_APP_INPUT_FILE, p->input_path, sizeof(p->input_path)) != 0) p->input_path[0] = 0;
    p->screen_ok = cp0_app_screen_read(p->screen_path[0] ? p->screen_path : NULL, &p->screen) == 0;
    p->input_ok = cp0_app_input_read(p->input_path[0] ? p->input_path : NULL, &p->input) == 0;

    p->mode = p->env_ok ? p->env.mode : CP0_APP_MODE_NONE;
    if (use_fb) fb_probe(fb_device(p, forced_fb), &p->fb);
    else memset(&p->fb, 0, sizeof(p->fb));

    /* the panel: environment, else screen.state, else the framebuffer itself (unrotated), else nothing */
    if (p->env.screen_w > 0 && p->env.screen_h > 0) {
        p->screen_w = p->env.screen_w, p->screen_h = p->env.screen_h, p->rot = p->env.screen_rot;
    } else if (p->screen_ok) {
        p->screen_w = p->screen.w, p->screen_h = p->screen.h, p->rot = p->screen.rot;
    } else if (p->fb.open_ok) {
        p->screen_w = p->fb.xres, p->screen_h = p->fb.yres, p->rot = 0;
    } else {
        p->screen_w = p->screen_h = p->rot = 0;
    }
    p->ppmm = p->env.screen_ppmm_x100 > 0               ? p->env.screen_ppmm_x100
              : p->screen_ok && p->screen.ppmm_x100 > 0 ? p->screen.ppmm_x100
                                                        : CP0_UI_DEFAULT_PPMM_X100;
    p->cls = p->env.screen_class != CP0_UI_CLASS__COUNT ? p->env.screen_class
             : p->screen_ok                             ? p->screen.cls
             : p->screen_w > 0                          ? cp0_ui_classify(p->screen_w, p->screen_h)
                                                        : CP0_UI_CLASS__COUNT;
    p->source = p->env_ok ? "contract" : p->screen_ok ? "state" : p->fb.open_ok ? "fbdev" : "none";

    /* the canvas: the contract's; without it the app owns what it found (the whole screen at scale 1) */
    if (p->env_ok) {
        p->canvas_w = p->env.canvas_w, p->canvas_h = p->env.canvas_h, p->canvas_scale = p->env.canvas_scale;
        p->win_x = p->env.win_x, p->win_y = p->env.win_y, p->win_w = p->env.win_w, p->win_h = p->env.win_h;
    } else {
        p->canvas_w = p->screen_w, p->canvas_h = p->screen_h, p->canvas_scale = 1;
        p->win_x = p->win_y = 0, p->win_w = p->screen_w, p->win_h = p->screen_h;
    }
    p->canvas_ppmm = round_div(p->ppmm, p->canvas_scale > 0 ? p->canvas_scale : 1);
    p->square_px = cp0_ui_mm_px(p->canvas_ppmm, 90, 1); /* the probe's own 9 mm square, independent of the design tokens */
    p->square_screen_px = p->square_px * (p->canvas_scale > 0 ? p->canvas_scale : 1);

    /* the tokens: the launcher's file, else the same computation (cp0_ui_metrics.c) */
    if (p->screen_ok) {
        p->tokens = p->screen;
        p->tokens_from = "file";
    } else if (p->screen_w > 0 &&
               cp0_app_screen_compute(p->screen_w, p->screen_h, p->rot, p->ppmm, &p->tokens) == 0) {
        p->tokens_from = "compute";
    } else {
        memset(&p->tokens, 0, sizeof(p->tokens));
        p->tokens_from = "none";
    }

    p->osk = p->input_ok ? cp0_app_osk_allowed(&p->input, cp0_app_monotonic_s()) : -1;
}

/* ------------------------------------------------------------------ the report */

typedef struct {
    char *buf;
    size_t len, size;
} text_t;

static void put(text_t *t, const char *fmt, ...)
{
    if (t->len >= t->size) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(t->buf + t->len, t->size - t->len, fmt, ap);
    va_end(ap);
    if (n > 0) t->len += (size_t)n < t->size - t->len ? (size_t)n : t->size - t->len - 1;
}

static const char *class_name(cp0_ui_class_t c)
{
    return c < CP0_UI_CLASS__COUNT ? cp0_ui_class_name(c) : "";
}

static const char *env_or(const char *name)
{
    const char *v = getenv(name);
    return v ? v : "";
}

/* "compat 320x170 0,0,640,340 scale 2 standard 1130" */
static void summary(const probe_t *p, char *buf, size_t size)
{
    snprintf(buf, size, "%s %dx%d %d,%d,%d,%d scale %d %s %d", p->mode != CP0_APP_MODE_NONE ? cp0_app_mode_name(p->mode) : "none",
             p->canvas_w, p->canvas_h, p->win_x, p->win_y, p->win_w, p->win_h, p->canvas_scale, class_name(p->cls),
             p->ppmm);
}

static void report(const probe_t *p, text_t *t)
{
    const cp0_app_env_t *e = &p->env;
    put(t, "probe.version=%d\n", PROBE_REPORT_VERSION);
    put(t, "probe.tag=%s\n", p->tag);
    /* 1. the environment as read (raw strings as well, so a malformed value shows) */
    put(t, "env.ok=%d\n", p->env_ok);
    put(t, "env.contract=%d\n", e->contract);
    put(t, "env.mode=%s\n", env_or(CP0_APP_ENV_MODE));
    put(t, "env.window=%s\n", env_or(CP0_APP_ENV_WINDOW));
    put(t, "env.canvas=%s\n", env_or(CP0_APP_ENV_CANVAS));
    put(t, "env.canvas_scale=%s\n", env_or(CP0_APP_ENV_CANVAS_SCALE));
    put(t, "env.state_dir=%s\n", e->state_dir);
    put(t, "env.pda=%s\n", env_or("APPLAUNCH_PDA")); /* report 044: 1 = this app draws its own writing zone's band */
    put(t, "env.screen_w=%s\n", env_or(CP0_APP_ENV_SCREEN_W));
    put(t, "env.screen_h=%s\n", env_or(CP0_APP_ENV_SCREEN_H));
    put(t, "env.screen_rotate=%s\n", env_or(CP0_APP_ENV_SCREEN_ROTATE));
    put(t, "env.screen_ppmm=%s\n", env_or(CP0_APP_ENV_SCREEN_PPMM));
    put(t, "env.screen_class=%s\n", env_or(CP0_APP_ENV_SCREEN_CLASS));
    put(t, "env.screen_fb=%s\n", e->screen_fb);
    put(t, "env.screen_touch=%s\n", e->screen_touch);
    /* 2. screen.state */
    const cp0_app_screen_t *s = &p->screen;
    put(t, "screen.path=%s\n", p->screen_path);
    put(t, "screen.ok=%d\n", p->screen_ok);
    if (p->screen_ok) {
        put(t, "screen.size=%dx%d\n", s->w, s->h);
        put(t, "screen.rot=%d\n", s->rot);
        put(t, "screen.ppmm=%d\n", s->ppmm_x100);
        put(t, "screen.density=%s\n", cp0_ui_density_src_name(s->density_src));
        put(t, "screen.class=%s\n", class_name(s->cls));
        put(t, "screen.compat=%d,%d,%d,%d\n", s->compat_x, s->compat_y, s->compat_w, s->compat_h);
        put(t, "screen.scale=%d\n", s->compat_scale);
        put(t, "screen.toolbar=%d\n", s->toolbar_h);
        put(t, "screen.fb=%s\n", s->fb);
        put(t, "screen.bpp=%d\n", s->bpp);
        put(t, "screen.touch=%s\n", s->touch);
        put(t, "screen.kb_inset=%d\n", s->kb_inset);
        put(t, "screen.gen=%u\n", s->gen);
    }
    /* 3. input.state */
    const cp0_app_input_t *in = &p->input;
    put(t, "input.path=%s\n", p->input_path);
    put(t, "input.ok=%d\n", p->input_ok);
    if (p->input_ok) {
        put(t, "input.keyboards=%u\n", in->keyboards);
        put(t, "input.bt_keyboards=%u\n", in->bt_keyboards);
        put(t, "input.last_seen=%lld\n", (long long)in->last_seen);
        put(t, "input.kb_gone=%lld\n", (long long)in->kb_gone);
        put(t, "input.bt_gone=%lld\n", (long long)in->bt_gone);
        put(t, "input.vkb=%s\n", cp0_app_vkb_name(in->vkb));
        put(t, "input.hand=%d\n", in->hand);
        put(t, "input.gen=%u\n", in->gen);
    }
    /* 4. the framebuffer (fbdev ioctls only: what any app sees) */
    put(t, "fb.dev=%s\n", p->fb.dev);
    put(t, "fb.ok=%d\n", p->fb.open_ok);
    if (p->fb.open_ok) {
        put(t, "fb.size=%dx%d\n", p->fb.xres, p->fb.yres);
        put(t, "fb.virtual=%dx%d\n", p->fb.xres_virtual, p->fb.yres_virtual);
        put(t, "fb.bpp=%d\n", p->fb.bpp);
        put(t, "fb.stride=%d\n", p->fb.stride);
    } else if (p->fb.dev[0]) {
        put(t, "fb.error=%s\n", strerror(p->fb.err));
    }
    /* 5. derived: what the app goes by */
    const cp0_app_screen_t *k = &p->tokens;
    put(t, "derived.source=%s\n", p->source);
    put(t, "derived.mode=%s\n", p->mode != CP0_APP_MODE_NONE ? cp0_app_mode_name(p->mode) : "none");
    put(t, "derived.screen=%dx%d\n", p->screen_w, p->screen_h);
    put(t, "derived.rot=%d\n", p->rot);
    put(t, "derived.ppmm=%d\n", p->ppmm);
    put(t, "derived.class=%s\n", class_name(p->cls));
    put(t, "derived.canvas=%dx%d\n", p->canvas_w, p->canvas_h);
    put(t, "derived.canvas_scale=%d\n", p->canvas_scale);
    put(t, "derived.canvas_ppmm=%d\n", p->canvas_ppmm);
    put(t, "derived.window=%d,%d,%d,%d\n", p->win_x, p->win_y, p->win_w, p->win_h);
    put(t, "derived.square_9mm_px=%d\n", p->square_px);
    put(t, "derived.square_9mm_screen_px=%d\n", p->square_screen_px);
    put(t, "derived.tokens_from=%s\n", p->tokens_from);
    put(t, "derived.tokens=space %d,%d,%d radius %d,%d,%d header %d row %d target %d dialog_max_w %d text %d,%d,%d,%d\n",
        k->space_s, k->space_m, k->space_l, k->radius_s, k->radius_m, k->radius_l, k->header_h, k->row_h, k->target,
        k->dialog_max_w, k->text_title, k->text_heading, k->text_body, k->text_caption);
    put(t, "derived.vkb=%s\n", cp0_app_vkb_name(p->input_ok ? in->vkb : CP0_APP_VKB_AUTO));
    put(t, "derived.osk_allowed=%s\n", p->osk < 0 ? "unknown" : p->osk ? "1" : "0");
    char line[160];
    summary(p, line, sizeof(line));
    put(t, "summary=%s\n", line);
}

/* ------------------------------------------------------------------ drawing (through cp0_fb_output) */

typedef struct {
    const cp0_fb_out_t *o;
    uint8_t *row; /* one row of a solid colour, buffer depth */
    int row_px;
} painter_t;

static void fill(painter_t *pt, int x, int y, int w, int h, uint32_t rgb)
{
    const cp0_fb_out_t *o = pt->o;
    if (x < 0) w += x, x = 0;
    if (y < 0) h += y, y = 0;
    if (x + w > o->lw) w = o->lw - x;
    if (y + h > o->lh) h = o->lh - y;
    if (w <= 0 || h <= 0) return;
    const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    if (o->bpp == 4) {
        uint32_t *px = (uint32_t *)pt->row;
        for (int i = 0; i < w; i++) px[i] = rgb & 0xFFFFFF;
    } else {
        uint16_t *px = (uint16_t *)pt->row;
        const uint16_t v = (uint16_t)cp0_fbo_pack(&o->layout, r, g, b);
        for (int i = 0; i < w; i++) px[i] = v;
    }
    cp0_fbo_blit(o, x, y, w, h, pt->row, 0); /* stride 0: the same row h times */
}

static int text(painter_t *pt, int x, int y, int scale, uint32_t rgb, const char *s)
{
    for (; *s; s++, x += 6 * scale) {
        const unsigned char c = (unsigned char)*s;
        if (c < FONT5X7_FIRST || c > FONT5X7_LAST) continue;
        const uint8_t *glyph = font5x7[c - FONT5X7_FIRST];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (glyph[col] & (1u << row)) fill(pt, x + col * scale, y + row * scale, scale, scale, rgb);
    }
    return x;
}

static void draw(const cp0_fb_out_t *o, const probe_t *p)
{
    painter_t pt = {o, NULL, o->lw > o->lh ? o->lw : o->lh};
    pt.row = calloc((size_t)pt.row_px, 4);
    if (!pt.row) return;
    const int W = o->lw, H = o->lh;
    const int ppmm = p->canvas_ppmm > 0 ? p->canvas_ppmm : CP0_UI_DEFAULT_PPMM_X100;

    fill(&pt, 0, 0, W, H, 0x101418);
    /* 1 mm grid, every 10th line brighter: line k at round(k mm * density) */
    for (int k = 0;; k++) {
        const int at = round_div(k * ppmm, 100);
        if (at >= W && at >= H) break;
        const uint32_t c = k % 10 == 0 ? 0x5A6470 : 0x2A3038;
        if (at < W) fill(&pt, at, 0, 1, H, c);
        if (at < H) fill(&pt, 0, at, W, 1, c);
    }
    /* the canvas border: 1 px white, then 1 px red inside */
    fill(&pt, 0, 0, W, 1, 0xFFFFFF), fill(&pt, 0, H - 1, W, 1, 0xFFFFFF);
    fill(&pt, 0, 0, 1, H, 0xFFFFFF), fill(&pt, W - 1, 0, 1, H, 0xFFFFFF);
    fill(&pt, 1, 1, W - 2, 1, 0xE03030), fill(&pt, 1, H - 2, W - 2, 1, 0xE03030);
    fill(&pt, 1, 1, 1, H - 2, 0xE03030), fill(&pt, W - 2, 1, 1, H - 2, 0xE03030);

    /* the 9 mm square, bottom right, its top-left corner on a 1 mm grid line (so it covers 9 grid cells); a 1 px
     * dark frame shows its exact edge */
    const int sq = p->square_px;
    const int margin = round_div(2 * ppmm, 100);
    int sx = round_div((W - margin - sq) * 100 / ppmm * ppmm, 100);
    int sy = round_div((H - margin - sq) * 100 / ppmm * ppmm, 100);
    if (sx < 2) sx = 2;
    if (sy < 2) sy = 2;
    fill(&pt, sx, sy, sq, sq, 0xFF8000);
    fill(&pt, sx, sy, sq, 1, 0x000000), fill(&pt, sx, sy + sq - 1, sq, 1, 0x000000);
    fill(&pt, sx, sy, 1, sq, 0x000000), fill(&pt, sx + sq - 1, sy, 1, sq, 0x000000);

    /* the report, about 1.4 mm high glyphs */
    int scale = ppmm * 14 / 10 / 700;
    if (scale < 1) scale = 1;
    const int line_h = 9 * scale;
    char lines[8][128];
    int n = 0;
    snprintf(lines[n++], sizeof(lines[0]), "APP CONTRACT PROBE %.40s", p->tag);
    summary(p, lines[n++], sizeof(lines[0]));
    snprintf(lines[n++], sizeof(lines[0]), "screen %dx%d rot %d ppmm %d %s", p->screen_w, p->screen_h, p->rot, p->ppmm,
             p->source);
    snprintf(lines[n++], sizeof(lines[0]), "draw %dx%d %d bpp rot %d %.40s", W, H, o->bpp * 8, o->rot, p->fb.dev);
    if (p->input_ok)
        snprintf(lines[n++], sizeof(lines[0]), "gen %u kb %u bt %u vkb %s osk %d", p->screen.gen, p->input.keyboards,
                 p->input.bt_keyboards, cp0_app_vkb_name(p->input.vkb), p->osk);
    else
        snprintf(lines[n++], sizeof(lines[0]), "gen %u no input.state", p->screen.gen);
    snprintf(lines[n++], sizeof(lines[0]), "square %d px = 9 mm, grid 1 mm", p->square_px);
    snprintf(lines[n++], sizeof(lines[0]), "Esc or Q: exit");
    const int tx = margin + 2;
    int ty = margin + 2;
    for (int i = 0; i < n && ty + line_h <= H - 2; i++, ty += line_h) {
        /* the full width above the square, up to its left edge beside it */
        const int right = ty + line_h > sy - 1 ? sx - 2 : W - 3;
        int limit = (right - tx) / (6 * scale);
        if (limit < 0) limit = 0;
        char clipped[128];
        snprintf(clipped, sizeof(clipped), "%s", lines[i]);
        if (limit < (int)sizeof(clipped)) clipped[limit] = 0;
        if (!clipped[0]) continue;
        fill(&pt, tx - 1, ty - 1, (int)strlen(clipped) * 6 * scale + 1, 8 * scale + 1, 0x000000);
        text(&pt, tx, ty, scale, i == 1 ? 0x40FF80 : 0xFFFFFF, clipped);
    }
    free(pt.row);
}

/* ------------------------------------------------------------------ framebuffer and keys */

typedef struct {
    int fd;
    uint8_t *map;
    size_t map_size;
    cp0_fb_out_t out;
} fb_t;

static int fb_open(const char *dev, const probe_t *p, fb_t *fb)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = open(dev, O_RDWR | O_CLOEXEC);
    if (fb->fd < 0) {
        fprintf(stderr, "app-contract-probe: open %s: %s\n", dev, strerror(errno));
        return -1;
    }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) != 0) {
        fprintf(stderr, "app-contract-probe: %s: not a framebuffer: %s\n", dev, strerror(errno));
        close(fb->fd);
        return -1;
    }
    if (var.bits_per_pixel != 16 && var.bits_per_pixel != 32) {
        fprintf(stderr, "app-contract-probe: %s: %u bpp is not supported (16 or 32)\n", dev, var.bits_per_pixel);
        close(fb->fd);
        return -1;
    }
    /* show page 0 (a launcher may have left a later page panned in) */
    if (var.xoffset != 0 || var.yoffset != 0) {
        var.xoffset = var.yoffset = 0;
        (void)ioctl(fb->fd, FBIOPAN_DISPLAY, &var);
    }
    fb->map_size = fix.smem_len ? fix.smem_len : (size_t)fix.line_length * var.yres;
    fb->map = mmap(NULL, fb->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->map == MAP_FAILED) {
        fprintf(stderr, "app-contract-probe: mmap %s: %s\n", dev, strerror(errno));
        close(fb->fd);
        return -1;
    }
    const cp0_fb_layout_t layout = {{(uint8_t)var.red.offset, (uint8_t)var.red.length},
                                    {(uint8_t)var.green.offset, (uint8_t)var.green.length},
                                    {(uint8_t)var.blue.offset, (uint8_t)var.blue.length}};
    /* compat: the virtual framebuffer is upright; fullscreen: the panel's clockwise pre-rotation */
    const int rot = p->mode == CP0_APP_MODE_COMPAT ? 0 : p->rot;
    const size_t page = (size_t)fix.line_length * var.yres;
    const int fixes = cp0_fbo_setup(&fb->out, fb->map, page < fb->map_size ? page : fb->map_size,
                                    (int)fix.line_length, (int)var.bits_per_pixel / 8, (int)var.xres, (int)var.yres,
                                    rot, p->canvas_w, p->canvas_h, var.bits_per_pixel == 16 ? &layout : NULL);
    if (fixes < 0) {
        fprintf(stderr, "app-contract-probe: %s: unusable buffer\n", dev);
        munmap(fb->map, fb->map_size);
        close(fb->fd);
        return -1;
    }
    if (fixes & CP0_FBO_FIX_LOGICAL)
        fprintf(stderr, "app-contract-probe: canvas %dx%d differs from the framebuffer (%dx%d logical): drawing %dx%d\n",
                p->canvas_w, p->canvas_h, fb->out.lw, fb->out.lh, fb->out.lw, fb->out.lh);
    return 0;
}

static void fb_close(fb_t *fb)
{
    if (fb->map && fb->map != MAP_FAILED) munmap(fb->map, fb->map_size);
    if (fb->fd > 0) close(fb->fd);
}

static int has_esc_key(int fd)
{
    unsigned long bits[KEY_MAX / (8 * sizeof(unsigned long)) + 1];
    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return 0;
    const size_t word = KEY_ESC / (8 * sizeof(unsigned long)), bit = KEY_ESC % (8 * sizeof(unsigned long));
    return (bits[word] >> bit) & 1u;
}

/* compat: the launcher's virtual keyboard (LV_LINUX_KEYBOARD_DEVICE) carries every key; otherwise (fullscreen, by
 * hand) every input device with an Esc key. */
static int open_keyboards(const probe_t *p, int *fds, int max)
{
    int n = 0;
    const char *vkbd = getenv("LV_LINUX_KEYBOARD_DEVICE");
    if (vkbd && vkbd[0]) {
        const int fd = open(vkbd, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) fds[n++] = fd;
        if (n && p->mode == CP0_APP_MODE_COMPAT) return n;
    }
    DIR *dir = opendir("/dev/input");
    if (!dir) return n;
    struct dirent *de;
    while (n < max && (de = readdir(dir))) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (has_esc_key(fd)) fds[n++] = fd;
        else close(fd);
    }
    closedir(dir);
    return n;
}

/* 1 when an Esc or Q press arrived */
static int exit_key(int fd)
{
    struct input_event ev[16];
    for (;;) {
        const ssize_t got = read(fd, ev, sizeof(ev));
        if (got <= 0) return 0;
        for (size_t i = 0; i < (size_t)got / sizeof(ev[0]); i++)
            if (ev[i].type == EV_KEY && ev[i].value == 1 && (ev[i].code == KEY_ESC || ev[i].code == KEY_Q)) return 1;
    }
}

static void write_report_file(const probe_t *p, const char *body)
{
    char path[256];
    snprintf(path, sizeof(path), "/tmp/app-contract-probe-%s.txt", p->tag);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(body, f);
    fclose(f);
}

static int write_ppm(const char *path, const cp0_fb_out_t *o)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "app-contract-probe: %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", o->pw, o->ph);
    for (int y = 0; y < o->ph; y++)
        for (int x = 0; x < o->pw; x++) {
            const uint8_t *px = o->base + (size_t)y * o->stride + (size_t)x * o->bpp;
            unsigned char rgb[3];
            if (o->bpp == 4) {
                const uint32_t v = *(const uint32_t *)px;
                rgb[0] = (unsigned char)(v >> 16), rgb[1] = (unsigned char)(v >> 8), rgb[2] = (unsigned char)v;
            } else { /* standard RGB565 (the --ppm buffer's layout) */
                const uint16_t v = *(const uint16_t *)px;
                rgb[0] = (unsigned char)((v >> 11) << 3), rgb[1] = (unsigned char)(((v >> 5) & 0x3F) << 2);
                rgb[2] = (unsigned char)((v & 0x1F) << 3);
            }
            fwrite(rgb, 1, 3, f);
        }
    return fclose(f) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ main */

static void usage(void)
{
    fputs("usage: app-contract-probe [--report [--no-fb]] [--ppm FILE [--ppm-bpp 16|32]] [--tag NAME] [--fb DEV]"
          " [--timeout S]\n"
          "see tools/dev/app-contract-probe/README.md\n",
          stderr);
}

int main(int argc, char **argv)
{
    int report_only = 0, use_fb = 1, timeout_s = 300, ppm_bpp = 32;
    const char *forced_fb = NULL, *ppm = NULL;
    probe_t p;
    memset(&p, 0, sizeof(p));
    p.tag = "probe";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--report") == 0) report_only = 1;
        else if (strcmp(argv[i], "--no-fb") == 0) use_fb = 0;
        else if (strcmp(argv[i], "--tag") == 0 && i + 1 < argc) p.tag = argv[++i];
        else if (strcmp(argv[i], "--fb") == 0 && i + 1 < argc) forced_fb = argv[++i];
        else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) ppm = argv[++i];
        else if (strcmp(argv[i], "--ppm-bpp") == 0 && i + 1 < argc) ppm_bpp = atoi(argv[++i]);
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) timeout_s = atoi(argv[++i]);
        else {
            usage();
            return 2;
        }
    }
    for (const char *c = p.tag; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' ||
              *c == '_')) {
            fputs("app-contract-probe: --tag takes letters, digits, - and _\n", stderr);
            return 2;
        }

    char buf[REPORT_MAX];
    text_t t = {buf, 0, sizeof(buf)};
    buf[0] = 0;

    if (report_only) {
        probe_read(&p, forced_fb, use_fb);
        report(&p, &t);
        fputs(buf, stdout);
        return 0;
    }

    if (ppm) {
        probe_read(&p, forced_fb, 0);
        if (p.canvas_w <= 0 || p.canvas_h <= 0) {
            fputs("app-contract-probe: no canvas size (no contract, no screen.state)\n", stderr);
            return 1;
        }
        /* the memory the panel would hold: fullscreen with the panel's rotation, compat upright */
        const int rot = p.mode == CP0_APP_MODE_COMPAT ? 0 : p.rot;
        const int turned = rot == 90 || rot == 270;
        const int pw = turned ? p.canvas_h : p.canvas_w, ph = turned ? p.canvas_w : p.canvas_h;
        const int bytes = ppm_bpp == 16 ? 2 : 4;
        uint8_t *mem = calloc((size_t)pw * ph, (size_t)bytes);
        cp0_fb_out_t o;
        if (!mem || cp0_fbo_setup(&o, mem, (size_t)pw * ph * bytes, pw * bytes, bytes, pw, ph, rot, p.canvas_w,
                                  p.canvas_h, NULL) < 0) {
            free(mem);
            return 1;
        }
        draw(&o, &p);
        const int rc = write_ppm(ppm, &o);
        free(mem);
        return rc == 0 ? 0 : 1;
    }

    probe_read(&p, forced_fb, 1);
    report(&p, &t);
    fputs(buf, stderr);
    write_report_file(&p, buf);
    if (p.canvas_w <= 0 || p.canvas_h <= 0) {
        /* no contract, no file: take the framebuffer's own size (the degrade rule) */
        p.canvas_w = p.fb.xres, p.canvas_h = p.fb.yres;
        p.win_w = p.fb.xres, p.win_h = p.fb.yres;
    }
    fb_t fb;
    if (fb_open(fb_device(&p, forced_fb), &p, &fb) != 0) return 1;
    draw(&fb.out, &p);

    int keys[MAX_KEYBOARDS];
    const int nkeys = open_keyboards(&p, keys, MAX_KEYBOARDS);
    if (!nkeys) fputs("app-contract-probe: no keyboard found: exits on the timeout only\n", stderr);
    struct pollfd pfd[MAX_KEYBOARDS + 1];
    for (int i = 0; i < nkeys; i++) pfd[i].fd = keys[i], pfd[i].events = POLLIN;
    int npfd = nkeys;
    if (isatty(STDIN_FILENO)) pfd[npfd].fd = STDIN_FILENO, pfd[npfd].events = POLLIN, npfd++;

    const int64_t start = cp0_app_monotonic_s();
    int done = 0;
    while (!done) {
        const int ready = poll(pfd, (nfds_t)npfd, 1000);
        for (int i = 0; ready > 0 && i < npfd; i++) {
            if (!(pfd[i].revents & POLLIN)) continue;
            if (pfd[i].fd == STDIN_FILENO) {
                char c;
                if (read(STDIN_FILENO, &c, 1) == 1 && (c == 27 || c == 'q' || c == 'Q')) done = 1;
            } else if (exit_key(pfd[i].fd)) {
                done = 1;
            }
        }
        if (timeout_s > 0 && cp0_app_monotonic_s() - start >= timeout_s) done = 1;
        if (done) break;
        /* the state files: re-read at most once a second, redraw on a new gen or keyboard rule */
        probe_t q = p;
        cp0_app_screen_t s;
        cp0_app_input_t in;
        const int s_ok = cp0_app_screen_read(p.screen_path[0] ? p.screen_path : NULL, &s) == 0;
        const int in_ok = cp0_app_input_read(p.input_path[0] ? p.input_path : NULL, &in) == 0;
        const int osk = in_ok ? cp0_app_osk_allowed(&in, cp0_app_monotonic_s()) : -1;
        if (s_ok != p.screen_ok || (s_ok && s.gen != p.screen.gen) || in_ok != p.input_ok ||
            (in_ok && in.gen != p.input.gen) || osk != p.osk) {
            q.screen_ok = s_ok, q.input_ok = in_ok, q.osk = osk;
            if (s_ok) q.screen = s;
            if (in_ok) q.input = in;
            p = q;
            t.len = 0, buf[0] = 0;
            report(&p, &t);
            fprintf(stderr, "app-contract-probe: state changed (screen gen %u, input gen %u, osk %d)\n", p.screen.gen,
                    p.input.gen, p.osk);
            write_report_file(&p, buf);
            draw(&fb.out, &p);
        }
    }
    for (int i = 0; i < nkeys; i++) close(keys[i]);
    fb_close(&fb);
    return 0;
}
