/*
 * SPDX-License-Identifier: MIT
 *
 * The app contract of the CyberDeck Zero launcher (APPLaunch), version 1 (task 014 P5, report 023): what an app the
 * launcher starts can learn about the screen, the window it gets and the keyboards, without hard-coding 320x170.
 *
 * Pure C99, no LVGL, no cp0 runtime: copy this file and src/cp0_app_contract.c (plus cp0_ui_metrics.{h,c} for the
 * class names and the token fallback) into an app, or build in the launcher tree (ext_components/cp0_lvgl/include).
 *
 * 1. Environment set by the launcher for every app it starts (on the app's command line only):
 *      APPLAUNCH_CONTRACT=1          the contract version the launcher implements
 *      APPLAUNCH_APP_MODE            compat (the 320x170 virtual framebuffer, scaled into the compat window with the
 *                                    toolbar under it) | fullscreen (X-Fullscreen=true: the app owns the whole panel)
 *      APPLAUNCH_WINDOW=x,y,w,h      where the app's picture is on the screen (logical landscape coordinates)
 *      APPLAUNCH_CANVAS=WxH          the pixels the app draws (compat 320x170; fullscreen the logical screen)
 *      APPLAUNCH_CANVAS_SCALE=N      screen pixels per canvas pixel (compat 2 or 1; fullscreen 1)
 *      APPLAUNCH_STATE_DIR           the directory of screen.state and input.state (absent: see cp0_app_state_path)
 *    and, for every process of the session, the panel (never the app's canvas):
 *      APPLAUNCH_SCREEN_W / _H       logical (landscape) size of the panel in px
 *      APPLAUNCH_SCREEN_ROTATE       clockwise pre-rotation of the logical picture into the framebuffer: 0|90|180|270
 *      APPLAUNCH_SCREEN_PPMM         density, px per mm x100 (1130 = 11.3 px/mm)
 *      APPLAUNCH_SCREEN_CLASS        compact | standard | wide | square | large | portrait (cp0_ui_metrics.h)
 *      APPLAUNCH_SCREEN_FB           the framebuffer device the launcher draws to ("" = unknown)
 *      APPLAUNCH_SCREEN_TOUCH        the touch device the launcher reads ("" = none)
 *    The density of the canvas is APPLAUNCH_SCREEN_PPMM / APPLAUNCH_CANVAS_SCALE.
 *
 * 2. <state dir>/screen.state (written only by the launcher, by atomic rename; one key=value per line; readers
 *    ignore unknown keys; "v" changes only for an incompatible change):
 *      v=1 w= h= rot= ppmm= density=default|panel_mm|fbdev class= compat=x,y,w,h scale= toolbar=
 *      fb= bpp= touch=                         the panel's framebuffer, its depth, the touch device
 *      space=s,m,l radius=s,m,l header= row= target= dialog_max_w= text=title,heading,body,caption   tokens (px)
 *      kb_inset=0                              reserved, always 0 in contract 1
 *      gen=                                    increases on every write (the launcher rewrites it after each app)
 *
 * 3. <state dir>/input.state (same rules):
 *      v=1 keyboards=N (real keyboards present now) bt_keyboards=N (of them Bluetooth) last_seen= (unix s, the last
 *      time keyboards > 0) kb_gone= bt_gone= (monotonic s when the last keyboard / Bluetooth keyboard went away,
 *      0 = never) vkb=auto|off|force|zone (the on-screen keyboard rule in force: Settings > System > On-screen keyboard,
 *      APPLAUNCH_OSK, off when a desktop session owns the screen; zone while the PDA profile's writing zone is the
 *      text input, report 041: no on-screen keyboard then) pointer=on|off (Settings > System > Pointer
 *      navigation; missing = off) gen=
 *    An app that has text fields and no physical keyboard draws its own on-screen keyboard when
 *    cp0_app_osk_allowed() says so (the launcher never draws over a fullscreen app). An app linking cp0_lvgl that
 *    reads every keyboard (cp0_keyboard_set_read_all_keyboards) follows pointer= by itself (cp0_pointer_nav.h): a
 *    mouse or trackpoint then sends Up / Down / Left / Right, Enter (click) and Ctrl+O (held click, "options").
 *
 * 3b. <state dir>/status.state (same rules; apps' status strip, report 032): the launcher's Wi-Fi, Ethernet, Bluetooth and
 *    battery as it shows them. v=1 gen= pid=<launcher pid> wifi=0|1 wifi_pct=0..100 eth=0|1 bt=off|on|connected
 *    battery=-1|0..100 (-1 = none) battery_state=Charging|Discharging|Full|Not charging|Unknown, and (report 035)
 *    volume=-1|0..100 (-1 = no sound output) mute=0|1 brightness_pct=-1|0..100 (-1 = no backlight to drive): the levels
 *    an app draws its own bar from. Written only when a value changes. cp0_app_status_read() returns -1 (use sysfs)
 *    when the file is missing, not v=1, or its pid is gone.
 *
 * 3c. <state dir>/board.state (same rules; report 035): the facts of the board that do not change while the launcher
 *    runs (the sound output may: it is re-probed). cp0_app_board_read(): board, arch, ram_mb, battery, backlight,
 *    sound and its kind, and requests= (what launcher.sock answers on this board). A key the launcher does not know is
 *    absent (the field is -1 / ""), never 0. Requests from an app: cp0_app_client.h and docs/APP-CONTRACT.md section 14.
 *
 * 4. .desktop keys: X-Fullscreen=true (fullscreen mode); X-MinSize=WxH (the tile is hidden on a smaller screen; an
 *    older launcher ignores it). Full reference: docs/APP-CONTRACT.md.
 *
 * Failure modes: an app started another way (ssh, an older launcher, a launcher without the contract) has no contract env and
 * maybe no files: cp0_app_env_read() returns -1, the readers return -1, and the app uses its own probe (fbdev
 * VSCREENINFO) and cp0_app_screen_compute(). Stability: within contract 1 the names, units and meanings above never
 * change; keys and variables are only added.
 */
#pragma once

#include "cp0_ui_metrics.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_APP_CONTRACT_VERSION 1

#define CP0_APP_ENV_CONTRACT "APPLAUNCH_CONTRACT"
#define CP0_APP_ENV_MODE "APPLAUNCH_APP_MODE"
#define CP0_APP_ENV_WINDOW "APPLAUNCH_WINDOW"
#define CP0_APP_ENV_CANVAS "APPLAUNCH_CANVAS"
#define CP0_APP_ENV_CANVAS_SCALE "APPLAUNCH_CANVAS_SCALE"
#define CP0_APP_ENV_STATE_DIR "APPLAUNCH_STATE_DIR"
#define CP0_APP_ENV_SCREEN_W "APPLAUNCH_SCREEN_W"
#define CP0_APP_ENV_SCREEN_H "APPLAUNCH_SCREEN_H"
#define CP0_APP_ENV_SCREEN_ROTATE "APPLAUNCH_SCREEN_ROTATE"
#define CP0_APP_ENV_SCREEN_PPMM "APPLAUNCH_SCREEN_PPMM"
#define CP0_APP_ENV_SCREEN_CLASS "APPLAUNCH_SCREEN_CLASS"
#define CP0_APP_ENV_SCREEN_FB "APPLAUNCH_SCREEN_FB"
#define CP0_APP_ENV_SCREEN_TOUCH "APPLAUNCH_SCREEN_TOUCH"

#define CP0_APP_STATE_SUBDIR "applaunch"      /* $XDG_RUNTIME_DIR/applaunch/ */
#define CP0_APP_SCREEN_FILE "screen.state"
#define CP0_APP_INPUT_FILE "input.state"
#define CP0_APP_STATUS_FILE "status.state"
#define CP0_APP_BOARD_FILE "board.state"
#define CP0_APP_STATE_FALLBACK_PREFIX "/tmp/applaunch-" /* no XDG_RUNTIME_DIR: /tmp/applaunch-screen.state */

/* F1 rules (task 012): a Bluetooth keyboard that went away counts as present (asleep) for this long, any keyboard
 * that went away for a short grace. */
#define CP0_APP_OSK_BT_ASLEEP_S 600
#define CP0_APP_OSK_GRACE_S 2

#define CP0_APP_PATH_MAX 256

typedef enum {
    CP0_APP_MODE_NONE = 0, /* not started by a contract launcher (or an unknown mode) */
    CP0_APP_MODE_COMPAT,
    CP0_APP_MODE_FULLSCREEN,
} cp0_app_mode_t;

typedef enum {
    CP0_APP_VKB_AUTO = 0, /* the F1 rule: only while no physical keyboard is present (cp0_app_osk_rule) */
    CP0_APP_VKB_OFF,      /* never */
    CP0_APP_VKB_FORCE,    /* always (development, HackBerry with APPLAUNCH_OSK=force) */
    CP0_APP_VKB_ZONE,     /* never: the PDA profile's writing zone is the text input (report 041). A reader built
                           * before it parses "zone" as unknown and keeps auto */
} cp0_app_vkb_t;

/* ------------------------------------------------------------------ environment */

typedef struct {
    int contract;                /* APPLAUNCH_CONTRACT, 0 when absent or malformed */
    cp0_app_mode_t mode;
    int win_x, win_y, win_w, win_h;
    int canvas_w, canvas_h, canvas_scale;
    char state_dir[CP0_APP_PATH_MAX];  /* "" when absent */
    /* the panel; 0 / "" when absent */
    int screen_w, screen_h, screen_rot, screen_ppmm_x100;
    cp0_ui_class_t screen_class; /* CP0_UI_CLASS__COUNT when absent or unknown */
    char screen_fb[CP0_APP_PATH_MAX];
    char screen_touch[CP0_APP_PATH_MAX];
} cp0_app_env_t;

/* Reads the environment above. Returns 0 when the app was started by a launcher of contract 1 or later (contract,
 * mode, window and canvas all present and well formed), else -1. `out` is always filled with what was found. */
int cp0_app_env_read(cp0_app_env_t *out);

const char *cp0_app_mode_name(cp0_app_mode_t mode); /* "compat", "fullscreen", "" */
cp0_app_mode_t cp0_app_mode_parse(const char *s);   /* CP0_APP_MODE_NONE when unknown */
/* "compact" ... "portrait" -> the class; CP0_UI_CLASS__COUNT when unknown */
cp0_ui_class_t cp0_app_class_parse(const char *s);

/* ------------------------------------------------------------------ state files */

/* Path of a state file ("screen.state" / "input.state"): $APPLAUNCH_STATE_DIR/<file>, else
 * $XDG_RUNTIME_DIR/applaunch/<file>, else /tmp/applaunch-<file>. 0, or -1 when buf is too small. */
int cp0_app_state_path(const char *file, char *buf, size_t size);

typedef struct {
    int w, h, rot, ppmm_x100;
    cp0_ui_density_src_t density_src;
    cp0_ui_class_t cls;
    char fb[CP0_APP_PATH_MAX];
    int bpp;
    char touch[CP0_APP_PATH_MAX];
    int compat_x, compat_y, compat_w, compat_h, compat_scale, toolbar_h;
    int space_s, space_m, space_l;
    int radius_s, radius_m, radius_l;
    int header_h, row_h, target, dialog_max_w;
    int text_title, text_heading, text_body, text_caption;
    int kb_inset;                /* reserved: 0 in contract 1 */
    unsigned gen;
    int from_file;               /* 1: read from screen.state; 0: cp0_app_screen_compute() */
} cp0_app_screen_t;

/* Parses screen.state text. 0 when it is a v=1 file with w and h; -1 otherwise (out cleared). Unknown keys are
 * ignored; a key that is missing (an older launcher) stays 0 / "". */
int cp0_app_screen_parse(const char *text, size_t len, cp0_app_screen_t *out);
/* Reads and parses `path` (NULL: cp0_app_state_path("screen.state")). 0 or -1. */
int cp0_app_screen_read(const char *path, cp0_app_screen_t *out);
/* Fallback without the file: the launcher's own layout service for a w x h logical canvas (ppmm_x100 <= 0: the
 * default 11.3 px/mm). Same numbers as the launcher computes for that panel. 0, or -1 when w or h is not positive. */
int cp0_app_screen_compute(int w, int h, int rot, int ppmm_x100, cp0_app_screen_t *out);
/* The same from metrics already computed (the launcher uses it to check its file). */
void cp0_app_screen_from_metrics(const cp0_ui_metrics_t *m, cp0_app_screen_t *out);

typedef struct {
    unsigned keyboards;
    unsigned bt_keyboards;
    int64_t last_seen;           /* unix s, 0 = never */
    int64_t kb_gone;             /* monotonic s, 0 = never */
    int64_t bt_gone;             /* monotonic s, 0 = never */
    cp0_app_vkb_t vkb;           /* missing line (an older launcher): CP0_APP_VKB_AUTO */
    int pointer_nav;             /* pointer=on: 1 (Settings > System > Pointer navigation); missing line or off: 0 */
    unsigned gen;
} cp0_app_input_t;

/* Parses input.state text. 0 when it is a v=1 file, else -1 (out cleared). */
int cp0_app_input_parse(const char *text, size_t len, cp0_app_input_t *out);
/* Reads and parses `path` (NULL: cp0_app_state_path("input.state")). 0 or -1. */
int cp0_app_input_read(const char *path, cp0_app_input_t *out);

typedef enum {
    CP0_APP_BT_OFF = 0,   /* adapter off (or no Bluetooth service) */
    CP0_APP_BT_ON,        /* powered, nothing connected */
    CP0_APP_BT_CONNECTED, /* powered, a device connected */
} cp0_app_bt_t;

typedef struct {
    int pid;                     /* the launcher that wrote it */
    int wifi;                    /* connected */
    int wifi_pct;                /* 0..100 */
    int eth;                     /* a wired link is connected */
    cp0_app_bt_t bt;
    int battery;                 /* -1 = none, else 0..100 */
    int battery_charging;        /* battery_state=Charging */
    char battery_state[16];      /* "Charging", "Discharging", "Full", "Not charging", "Unknown"; "" when absent */
    int volume;                  /* -1 = no sound output (or an older launcher), else 0..100 */
    int mute;                    /* 1 = muted */
    int brightness_pct;          /* -1 = no backlight to drive (or an older launcher), else 0..100 */
    unsigned gen;
} cp0_app_status_t;

/* Parses status.state text. 0 when it is a v=1 file, else -1 (out cleared). A missing key stays 0 (battery, volume and
 * brightness_pct -1, bt off).
 * Does not look at pid. */
int cp0_app_status_parse(const char *text, size_t len, cp0_app_status_t *out);
/* Reads and parses `path` (NULL: cp0_app_state_path("status.state")), without the liveness check. 0 or -1. */
int cp0_app_status_load(const char *path, cp0_app_status_t *out);
/* cp0_app_status_load() plus: pid must be a live process (kill(pid, 0)), so the file of a launcher that is gone is not
 * trusted. 0 when the feed is good, else -1 (out cleared): the app falls back to sysfs (cp0_statusbar_read_state). */
int cp0_app_status_read(const char *path, cp0_app_status_t *out);

/* board.state (report 035). Every int is -1 and every string "" when the key is absent. */
typedef struct {
    int pid;                     /* the launcher that wrote it */
    unsigned gen;
    char launcher[24];           /* launcher version, e.g. "0.8.6" */
    char board[64];              /* APPLAUNCH_BOARD; "" = no profile (the deck) or unknown */
    char arch[16];               /* uname -m: aarch64, x86_64 */
    int ram_mb;                  /* MemTotal in MB */
    int battery;                 /* 0 none, 1 present, 2 not set up (a hint says there should be one) */
    char battery_source[48];     /* "power_supply:<name>" or "gauge:max17048" */
    char backlight[8];           /* "none", "onoff", "levels" */
    int backlight_steps;         /* the steps the launcher offers: 10, 2 (on / off), 0 */
    int sound;                   /* 0 none, 1 present, 2 not set up */
    char sound_kind[12];         /* "alsa" (a local card), "server" (a sound-server sink), "bluetooth" */
    int sound_guess;             /* 1: sound_kind "bluetooth" comes from the sink's name only */
    char requests[160];          /* the verbs launcher.sock answers on this board, comma separated */
    char battery_note[96], backlight_note[96], sound_note[96]; /* the board profile's hint text, for display only */
} cp0_app_board_t;

/* Parses board.state text. 0 when it is a v=1 file, else -1 (out cleared, all fields absent). */
int cp0_app_board_parse(const char *text, size_t len, cp0_app_board_t *out);
/* Reads and parses `path` (NULL: cp0_app_state_path("board.state")), without the liveness check. 0 or -1. */
int cp0_app_board_load(const char *path, cp0_app_board_t *out);
/* cp0_app_board_load() plus the pid liveness rule of cp0_app_status_read(). 0 when good, else -1 (out cleared): the
 * app assumes nothing and hides what depends on it. */
int cp0_app_board_read(const char *path, cp0_app_board_t *out);
/* 1 when `verb` is in board->requests. */
int cp0_app_board_has_request(const cp0_app_board_t *board, const char *verb);

const char *cp0_app_vkb_name(cp0_app_vkb_t vkb);              /* "auto", "off", "force", "zone" */
int cp0_app_vkb_parse(const char *s, cp0_app_vkb_t *out);     /* 0, or -1 when unknown (out untouched) */

/* ------------------------------------------------------------------ on-screen keyboard rule (F1) */

/* 1 when an on-screen keyboard may show for a focused text field: off and zone -> never, force -> always, auto -> only while no
 * physical keyboard is present, not within CP0_APP_OSK_BT_ASLEEP_S of a Bluetooth keyboard going away (it sleeps)
 * and not within CP0_APP_OSK_GRACE_S of any keyboard going away. Times in monotonic seconds; 0 = never. The
 * launcher's own keyboard uses exactly this rule (cp0_ui_osk_policy_allows). */
int cp0_app_osk_rule(cp0_app_vkb_t vkb, unsigned keyboards, int64_t now_s, int64_t kb_gone_s, int64_t bt_gone_s);
/* cp0_app_osk_rule() with an input.state and the app's monotonic clock (cp0_app_monotonic_s()). */
int cp0_app_osk_allowed(const cp0_app_input_t *in, int64_t now_mono_s);
/* CLOCK_MONOTONIC in seconds (the clock of kb_gone / bt_gone; the same in every process of the boot). */
int64_t cp0_app_monotonic_s(void);

#ifdef __cplusplus
}
#endif
