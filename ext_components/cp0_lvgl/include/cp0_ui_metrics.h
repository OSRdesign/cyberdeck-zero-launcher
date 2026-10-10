/*
 * SPDX-License-Identifier: MIT
 *
 * Layout service of the responsive shell (task 014, report 022 sections 3.1-3.3 and 3.6): from the logical canvas
 * size and the panel density it computes
 *   - the screen class (structure: how many panes, where the actions go),
 *   - the density in px/mm (APPLAUNCH_PANEL_MM, else the framebuffer's mm, else 11.3 px/mm),
 *   - the design tokens in px (spacing, radii, header, list row 7 mm, touch target 9 mm, with pixel floors),
 *   - the text sizes, snapped to the builtin Montserrat sizes of the build (every even size 8..48),
 *   - where the 320x170 stock-app window goes and how tall the bottom toolbar is (D8: always a bottom bar; a fixed
 *     physical height, 9 mm keys plus margins; the window takes the largest integer scale that fits above it,
 *     centred there),
 *   - the geometry of today's home grid and toolbar ("shell"), so the native shell takes its numbers from here.
 *
 * Pinned presets: a 640x480 canvas (the deck) and a 480x320 canvas (the Pi 3A+) get exactly the numbers the shell
 * always drew (window placement, home grid, toolbar), whatever the density. Other sizes get the computed window
 * placement; their home grid is computed from the physical tokens (task 014 P1c: near-square tiles, columns and rows
 * from the available area; see shell_computed_grid in cp0_ui_metrics.c).
 *
 * Pure C, no LVGL and no system call in this header's core functions: unit-tested on the PC by
 * tests/test_ui_metrics.c. cp0_ui_metrics_lvgl.h adds the LVGL side (fonts, the native display's metrics).
 * cp0_ui_metrics_publish() / cp0_ui_state_write() (src/cp0/cp0_ui_state.c) are device builds only.
 * Internal for now (D10): the contract for apps is published in P5.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_UI_DEFAULT_PPMM_X100 1130 /* no density source: the deck's Waveshare 2.8" 640x480 (11.3 px/mm) */
#define CP0_UI_PPMM_MIN_X100 200      /* a source outside 2..40 px/mm is ignored (bogus driver values) */
#define CP0_UI_PPMM_MAX_X100 4000
#define CP0_UI_ROW_MM_X10 70           /* D5: list rows 7 mm ... */
#define CP0_UI_TARGET_MM_X10 90        /* ... primary buttons and toolbar keys 9 mm ... */
#define CP0_UI_ROW_FLOOR_PX 40         /* ... never smaller than 40 / 48 px on low-density panels */
#define CP0_UI_TARGET_FLOOR_PX 48
#define CP0_UI_COMPAT_W 320            /* the stock apps' canvas */
#define CP0_UI_COMPAT_H 170
#define CP0_UI_FONT_MIN_PX 8           /* builtin Montserrat: every even size 8..48 (lv_conf, all boards) */
#define CP0_UI_FONT_MAX_PX 48

/* Screen class from the logical size (w x h, a = w/h), first rule that matches:
 *   Portrait  a < 0.8                      (D12: not supported; computed so nothing breaks)
 *   Compact   h < 400                      480x320 (Pi 3A+), small squares, bars under 400 rows
 *   Square    a <= 1.25                    720x720, 480x480
 *   Large     h >= 700                     1280x720, 1280x800, 1024x768
 *   Wide      a >= 1.5                     800x480, 854x480, 1024x600, 1280x400
 *   Standard  otherwise (1.25 < a < 1.5)   640x480 (deck), 800x600 */
typedef enum {
    CP0_UI_COMPACT = 0,
    CP0_UI_STANDARD,
    CP0_UI_WIDE,
    CP0_UI_SQUARE,
    CP0_UI_LARGE,
    CP0_UI_PORTRAIT,
    CP0_UI_CLASS__COUNT
} cp0_ui_class_t;

typedef enum {
    CP0_UI_PRESET_COMPUTED = 0,
    CP0_UI_PRESET_DECK = 1,    /* 640x480 */
    CP0_UI_PRESET_COMPACT = 2, /* 480x320 (task 013 T3) */
} cp0_ui_preset_t;

typedef enum {
    CP0_UI_DENSITY_DEFAULT = 0, /* nothing usable: CP0_UI_DEFAULT_PPMM_X100 */
    CP0_UI_DENSITY_PANEL_MM,    /* APPLAUNCH_PANEL_MM=WxH (board.conf, written by install.sh for known panels) */
    CP0_UI_DENSITY_FBDEV,       /* the framebuffer's physical size (var.width / var.height, mm) */
} cp0_ui_density_src_t;

/* Design tokens, px (STATUS_PCT: percent of the deck's status strip). */
typedef enum {
    CP0_TOK_SPACE_S = 0,
    CP0_TOK_SPACE_M,
    CP0_TOK_SPACE_L,
    CP0_TOK_RADIUS_S,
    CP0_TOK_RADIUS_M,
    CP0_TOK_RADIUS_L,
    CP0_TOK_HEADER_H,    /* status bar / page header */
    CP0_TOK_STATUS_PCT,  /* clock / Wi-Fi / Bluetooth strip scale (D11: may exceed 100 on Large) */
    CP0_TOK_ROW_H,       /* list row: 7 mm, at least 40 px */
    CP0_TOK_TARGET,      /* primary button, toolbar key: 9 mm, at least 48 px */
    CP0_TOK_TOOLBAR_H,   /* stock-app toolbar: TARGET keys + the shell's padding (pinned: 140 deck, 100 Pi 3A+) */
    CP0_TOK_TILE_ICON,   /* home tile icon */
    CP0_TOK_DIALOG_MAX_W,
    CP0_TOK__COUNT
} cp0_ui_token_t;

/* Text styles, px of the builtin Montserrat size. */
typedef enum {
    CP0_TEXT_TITLE = 0,
    CP0_TEXT_HEADING,
    CP0_TEXT_BODY,
    CP0_TEXT_CAPTION,
    CP0_TEXT_SYMBOL, /* LV_SYMBOL_* glyphs on keys */
    CP0_TEXT__COUNT
} cp0_ui_text_t;

typedef struct {
    int ppmm_x100;              /* px per mm x100 (1130 = 11.3 px/mm) */
    cp0_ui_density_src_t src;
    int panel_mm_rejected;      /* APPLAUNCH_PANEL_MM was set but malformed or out of range (ignored) */
} cp0_ui_density_t;

/* The 320x170 stock-app window and the bottom toolbar (report 022 section 3.6, D8, task 014 toolbar rule). */
typedef struct {
    int scale;                /* integer upscale of the window */
    int scale_max;            /* what the rule allows before APPLAUNCH_COMPAT_SCALE lowers it */
    int x, y, w, h;           /* window, logical coordinates */
    int toolbar_y, toolbar_h; /* toolbar: full width, from toolbar_y to the bottom */
} cp0_ui_compat_t;

/* Today's home grid, status strip and stock-app toolbar (native_ui.cpp Layout). */
typedef struct {
    int cols, rows;       /* grid columns, rows that fit on screen */
    int bar_h;            /* status bar height */
    int pad;              /* grid padding and gap */
    int tile_w, tile_h;   /* rows fit exactly under the status bar (computed sizes: see cp0_ui_metrics.c) */
    int top_extra;        /* computed sizes: extra space above the first row (grid centred vertically), else 0 */
    int status_pct;       /* scale of the clock / Wi-Fi / Bluetooth strip */
    int status_top;       /* y of the clock pill inside the bar */
    int status_w;         /* width of the strip canvas */
    int title_x, title_y, title_px;
    int tile_radius, tile_border, tile_border_sel;
    int icon, icon_top;
    int label_px, label_bottom, label_inset;
    int tb_pad_x, tb_pad_y, tb_gap, tb_radius, tb_text_px, tb_symbol_px;
} cp0_ui_shell_t;

typedef struct {
    int w, h;                     /* logical (landscape) canvas */
    int rot;                      /* clockwise pre-rotation into the framebuffer (reported only) */
    int ppmm_x100;
    cp0_ui_density_src_t density_src;
    cp0_ui_class_t cls;
    cp0_ui_preset_t preset;       /* DECK / COMPACT for the pinned sizes, else COMPUTED */
    int tok[CP0_TOK__COUNT];
    int text_px[CP0_TEXT__COUNT];
    cp0_ui_compat_t compat;
    cp0_ui_preset_t shell_preset; /* which of today's two layouts the shell draws (DECK or COMPACT) */
    cp0_ui_shell_t shell;
} cp0_ui_metrics_t;

cp0_ui_class_t cp0_ui_classify(int w, int h);
const char *cp0_ui_class_name(cp0_ui_class_t cls);           /* "compact", "standard", ... */
const char *cp0_ui_density_src_name(cp0_ui_density_src_t s); /* "default", "panel_mm", "fbdev" */

/* "79x49" or "56.6x42.5" (mm, at most one decimal) -> 0 and the size in tenths of mm; -1 when malformed, zero or
 * above 10 m. */
int cp0_ui_parse_panel_mm(const char *s, int *w_x10, int *h_x10);

/* Density of a w x h px canvas whose panel is w_x10 x h_x10 tenths of mm: the mean of the two axes, px/mm x100,
 * rounded. 0 when an input is not positive. */
int cp0_ui_ppmm_from_mm(int w, int h, int w_x10, int h_x10);

/* Density for the logical canvas, first usable source:
 *   1. panel_mm (APPLAUNCH_PANEL_MM, landscape WxH; a portrait value is turned to the canvas' orientation),
 *   2. fb_mm_w x fb_mm_h as the driver reports them (buffer orientation: swapped for rot 90/270; 0 = unknown),
 *   3. CP0_UI_DEFAULT_PPMM_X100.
 * A source whose result is outside CP0_UI_PPMM_MIN/MAX is skipped. */
cp0_ui_density_t cp0_ui_density_resolve(int w, int h, int rot, const char *panel_mm, int fb_mm_w, int fb_mm_h);

/* round(mm_x10 / 10 * ppmm_x100 / 100), at least floor_px. */
int cp0_ui_mm_px(int ppmm_x100, int mm_x10, int floor_px);

/* The builtin Montserrat size used for px: even, CP0_UI_FONT_MIN_PX..CP0_UI_FONT_MAX_PX (an odd px rounds down). */
int cp0_ui_font_snap(int px);

/* Everything for one canvas. ppmm_x100 <= 0 means the default density. compat_cap (APPLAUNCH_COMPAT_SCALE, 0 = none)
 * lowers the window scale, never raises it. density_src is only recorded. Deterministic: the same inputs give a
 * byte-identical result. Returns 0, or -1 (out untouched) when w or h is not positive. */
int cp0_ui_metrics_compute(int w, int h, int rot, int ppmm_x100, cp0_ui_density_src_t density_src, int compat_cap,
                           cp0_ui_metrics_t *out);

/* The metrics of the running native display, set once by the display manager (NULL before). */
void cp0_ui_metrics_set_current(const cp0_ui_metrics_t *m);
const cp0_ui_metrics_t *cp0_ui_metrics_current(void);

/* Screen state file published for apps: $XDG_RUNTIME_DIR/applaunch/screen.state (/tmp/applaunch-screen.state when
 * XDG_RUNTIME_DIR is unset), one key per line, readers ignore unknown keys:
 *   v=1  w=  h=  rot=  ppmm= (x100)  density=default|panel_mm|fbdev  class=  compat=x,y,w,h  scale=  toolbar= (px)
 *   kb_inset=0 (on-screen keyboard, later)  gen= (increases on every write) */
#define CP0_UI_STATE_VERSION 1
#define CP0_UI_STATE_SUBDIR "applaunch"
#define CP0_UI_STATE_FILE "screen.state"
#define CP0_UI_STATE_FALLBACK_PATH "/tmp/applaunch-screen.state"

int cp0_ui_state_path(const char *runtime_dir, char *buf, size_t size); /* 0, or -1 when buf is too small */
int cp0_ui_state_format(const cp0_ui_metrics_t *m, unsigned gen, char *buf, size_t size); /* length or -1 */
unsigned cp0_ui_state_parse_gen(const char *text, size_t len); /* gen= of a v=1 file, 0 when none */

/* Human-readable summary (render harness, logs). Returns the length or -1 when it does not fit. */
int cp0_ui_metrics_describe(const cp0_ui_metrics_t *m, char *buf, size_t size);

/* ---- device builds only (src/cp0/cp0_ui_state.c) ---- */

/* Atomic write of `text` to `path` (directory created, one level, 0755; file 0644). 0 or -1 (errno set). */
int cp0_ui_state_write(const char *path, const char *text, size_t len);

/* For the processes the launcher starts: APPLAUNCH_SCREEN_W, _H, _ROTATE, _PPMM (x100), _CLASS in the environment
 * (call before any thread reads the environment: display creation), and the state file under runtime_dir with
 * gen = the previous file's gen + 1. Returns 0, or -1 when the file could not be written (the environment is set
 * anyway). */
int cp0_ui_metrics_publish(const cp0_ui_metrics_t *m, const char *runtime_dir);

#ifdef __cplusplus
}
#endif
